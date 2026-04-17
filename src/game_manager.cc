#include "game_manager.h"
#include "controller/ai_controller.h"
#include "controller/player_controller.h"
#include "controller/tool_controller.h"
#include "ui/game_ui.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

GameManager::GameManager(SDL_Renderer *renderer, SDL_Window *window,
                         const Settings &settings,
                         std::unique_ptr<GameMode> mode)
    : renderer_(renderer), window_(window), settings_(settings),
      mode_(std::move(mode)) {
  Board board;
  mode_->setup_board(board);
  game_ = std::make_unique<Game>(*mode_, std::move(board));
  auto ai_ctrl = std::make_unique<AIController>();
  ai_controller_ = ai_ctrl.get();
  controllers_.push_back(std::make_unique<PlayerController>(settings_));
  controllers_.push_back(std::make_unique<ToolController>(settings_, *mode_, ai_state_));
  controllers_.push_back(std::move(ai_ctrl));
  game_renderer_ = std::make_unique<Renderer>(renderer_, settings_);

  auto now = SdlClock::now();
  mode_->on_start(now);

  game_->drain_events();
}

void GameManager::reset() {
  stats_.reset();
  cmds_.clear();

  bool was_active = ai_state_.active;
  auto mode = ai_state_.mode;
  auto overrides = ai_state_.overrides;
  bool was_autoplay = ai_state_.autoplay;
  bool show_debug = ai_state_.show_debug_window;
  int max_vis = ai_state_.max_visible;
  int lookahead = ai_state_.queue_lookahead;
  int interval = ai_controller_->interval_ms();
  AIInputMode input_mode = ai_controller_->input_mode();

  ai_state_.deactivate();
  ai_state_.mode = mode;
  ai_state_.overrides = overrides;
  ai_state_.active = was_active;
  ai_state_.autoplay = was_autoplay;
  ai_state_.show_debug_window = show_debug;
  ai_state_.max_visible = max_vis;
  ai_state_.queue_lookahead = lookahead;
  if (was_active)
    ai_state_.needs_search = true;

  Board board;
  mode_->setup_board(board);
  game_ = std::make_unique<Game>(*mode_, std::move(board));
  controllers_.clear();
  auto ai_ctrl = std::make_unique<AIController>();
  ai_controller_ = ai_ctrl.get();
  ai_controller_->set_interval_ms(interval);
  ai_controller_->set_input_mode(input_mode);
  controllers_.push_back(std::make_unique<PlayerController>(settings_));
  controllers_.push_back(std::make_unique<ToolController>(settings_, *mode_, ai_state_));
  controllers_.push_back(std::move(ai_ctrl));

  auto now = SdlClock::now();
  mode_->on_start(now);

  for (auto &ev : game_->drain_events())
    stats_.process_event(ev, now);
}

bool GameManager::run() {
run_start:
  std::vector<InputEvent> input_events;

  while (running_) {
    auto now = SdlClock::now();

    // 1. Collect SDL events → feed ImGui first, then wrap for controllers.
    ImGuiIO &io = ImGui::GetIO();
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      ImGui_ImplSDL3_ProcessEvent(&ev);

      switch (ev.type) {
      case SDL_EVENT_QUIT:
        input_events.push_back(WindowClose{});
        break;
      case SDL_EVENT_WINDOW_RESIZED:
        input_events.push_back(
            WindowResize{static_cast<unsigned>(ev.window.data1),
                         static_cast<unsigned>(ev.window.data2)});
        break;
      case SDL_EVENT_KEY_DOWN:
        if (!ev.key.repeat && !io.WantCaptureKeyboard)
          input_events.push_back(
              KeyDown{ev.key.key, TimePoint(Duration(ev.key.timestamp))});
        break;
      case SDL_EVENT_KEY_UP:
        if (!io.WantCaptureKeyboard)
          input_events.push_back(
              KeyUp{ev.key.key, TimePoint(Duration(ev.key.timestamp))});
        break;
      }
    }

    // 2. Controller timers + input processing
    auto game_state = game_->state();
    for (auto &ctrl : controllers_)
      ctrl->tick(now, game_state, cmds_);
    for (auto &iev : input_events) {
      if (std::holds_alternative<WindowClose>(iev)) {
        running_ = false;
        return running_;
      }
      if (auto *kd = std::get_if<KeyDown>(&iev)) {
        if (kd->key == settings_.exit_to_menu) {
          return running_;
        } else if (kd->key == settings_.reset_game) {
          reset();
          goto run_start;
        }
      }
      TimePoint event_time = now;
      if (auto *kd2 = std::get_if<KeyDown>(&iev))
        event_time = kd2->timestamp;
      else if (auto *ku = std::get_if<KeyUp>(&iev))
        event_time = ku->timestamp;
      for (auto &ctrl : controllers_)
        ctrl->handle_event(iev, event_time, game_state, cmds_);
    }
    input_events.clear();

    // 3. Engine apply + tick
    game_->apply(cmds_);
    game_->tick(now);
    cmds_.clear();

    // 4. Drain engine events + pump AI
    CommandBuffer rule_cmds;
    process_engine_events(now, rule_cmds);
    pump_ai(now, game_->state());

    if (!rule_cmds.empty()) {
      game_->apply(rule_cmds);
      CommandBuffer discard;
      process_engine_events(now, discard);
    }

    // Auto-restart on win for training modes
    if (game_->state().game_over && game_->state().won &&
        mode_->auto_restart()) {
      reset();
      goto run_start;
    }

    // Mode-requested restart (e.g. debug-window buttons).
    if (mode_->consume_restart_request()) {
      reset();
      goto run_start;
    }

    // 5. Render: ImGui frame + embedded board texture
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    for (auto &ctrl : controllers_)
      ctrl->draw_imgui();
    mode_->draw_imgui();

    ViewModel vm = build_view_model(now);
    std::vector<IController *> ctrl_ptrs;
    ctrl_ptrs.reserve(controllers_.size());
    for (auto &c : controllers_)
      ctrl_ptrs.push_back(c.get());
    draw_game_ui(*game_renderer_, window_, vm, settings_, mode_.get(),
                 ctrl_ptrs, &ai_state_, ai_controller_);

    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
    SDL_RenderPresent(renderer_);
#ifdef __EMSCRIPTEN__
    emscripten_sleep(0);
#endif
  }
  return running_;
}

void GameManager::process_engine_events(TimePoint now,
                                        CommandBuffer &rule_cmds) {
  auto events = game_->drain_events();

  for (auto &ev : events) {
    for (auto &ctrl : controllers_)
      ctrl->notify(ev, now);

    bool undo = stats_.process_event(ev, now);

    if (auto *pl = std::get_if<eng::PieceLocked>(&ev)) {
      ai_state_.on_piece_locked(*pl);
      mode_->on_piece_locked(*pl, game_->state(), rule_cmds);
    } else if (auto *gm = std::get_if<eng::GarbageMaterialized>(&ev)) {
      ai_state_.on_garbage(gm->lines);
    } else if (undo) {
      for (auto &ctrl : controllers_)
        ctrl->reset_input_state();
      mode_->on_undo(game_->state());
      ai_state_.on_undo();
    }
  }

  mode_->on_tick(now, game_->state(), rule_cmds);
}

void GameManager::pump_ai(TimePoint now, const GameState &state) {
  if (!ai_state_.active)
    return;

  ai_state_.poll_search();

  if (ai_state_.needs_search && !ai_state_.searching()) {
    ai_state_.start_search(state);
    ai_state_.needs_search = false;
  }

  // Feed placement to AIController when plan is ready and autoplay is on
  if (ai_state_.autoplay && ai_state_.plan_computed() &&
      !ai_state_.current_plan().complete() && /*!ai_state_.searching() &&*/
      ai_controller_->idle()) {
    auto &step = *ai_state_.current_plan().current();
    ai_controller_->set_placement(step.placement, step.uses_hold);
  }
}

void GameManager::route_garbage(TimePoint now, CommandBuffer &cmds) {
  if (!game2_)
    return;

  int attack1 = game_->drain_attack();
  if (attack1 > 0)
    cmds.push(cmd::AddGarbage{attack1, 0});
}

ViewModel GameManager::build_view_model(TimePoint now) {
  ViewModel vm;
  vm.state = game_->state();
  vm.stats = stats_.snapshot(now);

  HudData hud;
  mode_->fill_hud(hud, vm.state, now);
  if (!hud.center_text.empty() || !hud.game_over_label.empty())
    vm.hud = std::move(hud);

  mode_->fill_plan_overlay(vm, vm.state);
  ai_state_.fill_plan_overlay(vm, vm.state);

  for (auto &ctrl : controllers_)
    ctrl->fill_plan_overlay(vm, vm.state);

  return vm;
}
