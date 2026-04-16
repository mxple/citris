#include "game_manager.h"
#include "ai/pathfinder.h"
#include "controller/ai_controller.h"
#include "controller/player_controller.h"
#include "controller/tool_controller.h"
#include "ui/game_ui.h"



#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <chrono>

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
  controllers_.push_back(std::make_unique<PlayerController>(settings_));
  controllers_.push_back(
      std::make_unique<ToolController>(settings_, *mode_, ai_state_));
  auto ai_ctrl = std::make_unique<AIController>();
  ai_controller_ = ai_ctrl.get();
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
  auto eval_type = ai_state_.eval_type;
  bool was_autoplay = ai_state_.autoplay;
  int max_vis = ai_state_.max_visible;
  int interval = ai_state_.input_interval_ms;

  ai_state_ = AIState{};
  ai_state_.active = was_active;
  ai_state_.eval_type = eval_type;
  ai_state_.autoplay = was_autoplay;
  ai_state_.max_visible = max_vis;
  ai_state_.input_interval_ms = interval;
  if (was_active) {
    ai_state_.rebuild_ai();
    ai_state_.needs_search = true;
  }

  Board board;
  mode_->setup_board(board);
  game_ = std::make_unique<Game>(*mode_, std::move(board));
  controllers_.clear();
  controllers_.push_back(std::make_unique<PlayerController>(settings_));
  controllers_.push_back(
      std::make_unique<ToolController>(settings_, *mode_, ai_state_));
  auto ai_ctrl = std::make_unique<AIController>();
  ai_controller_ = ai_ctrl.get();
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
      ctrl->check_timers(now, game_state, cmds_);
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
        ctrl->update(iev, event_time, game_state, cmds_);
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

    // 5. Render: ImGui frame + embedded board texture
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    for (auto &ctrl : controllers_)
      ctrl->draw_imgui();

    ViewModel vm = build_view_model(now);
    draw_game_ui(*game_renderer_, window_, vm, settings_);

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

    if (auto *pl = std::get_if<eng::PieceLocked>(&ev))
      ai_state_.on_piece_locked(*pl); // always advance plan; search gated inside
    else if (ai_state_.active && (std::holds_alternative<eng::UndoPerformed>(ev) ||
             std::holds_alternative<eng::GarbageMaterialized>(ev)))
      ai_state_.needs_search = true;

    if (auto *pl = std::get_if<eng::PieceLocked>(&ev)) {
      mode_->on_piece_locked(*pl, game_->state(), rule_cmds);
    } else if (undo) {
      for (auto &ctrl : controllers_)
        ctrl->reset_input_state();
      mode_->on_undo(game_->state());
      if (ai_state_.active) {
        ai_state_.search_task.reset();
        ai_state_.plan = Plan{};
        ai_state_.plan_computed = false;
        ai_state_.needs_search = true;
      }
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

  // Feed path to AIController when plan is ready and autoplay is on
  if (ai_state_.autoplay && ai_state_.plan_computed &&
      !ai_state_.plan.complete() && !ai_state_.searching() &&
      ai_controller_->idle()) {
    auto &step = *ai_state_.plan.current();

    std::vector<GameInput> path;
    if (step.uses_hold) {
      path.push_back(GameInput::Hold);
      auto spawn = spawn_position(step.placement.type);
      auto moves = find_path(state.board, step.placement, spawn.x, spawn.y,
                             Rotation::North);
      path.insert(path.end(), moves.begin(), moves.end());
    } else {
      auto &piece = state.current_piece;
      path = find_path(state.board, step.placement, piece.x, piece.y,
                       piece.rotation);
    }

    ai_controller_->set_path(std::move(path), now,
                             ai_state_.input_interval_ms);
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

  for (auto &ctrl : controllers_)
    ctrl->fill_plan_overlay(vm, vm.state);

  return vm;
}
