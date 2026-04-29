#include "game_manager.h"
#include "controller/ai_controller.h"
#include "controller/player_controller.h"
#include "controller/tbp_controller.h"
#include "controller/tool_controller.h"
#include "tbp/bot.h"
#include "tbp/internal_bot.h"
#include "ui/game_ui.h"
#include "ui/import_modal.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

GameManager::GameManager(SDL_Renderer *renderer, SDL_Window *window,
                         const Settings &settings,
                         std::unique_ptr<GameMode> mode,
                         std::unique_ptr<GameMode> mode2)
    : renderer_(renderer), window_(window), settings_(settings) {
  p1_.mode = std::move(mode);
  p2_.mode = std::move(mode2);

  Board board;
  p1_.mode->setup_board(board);
  p1_.game = std::make_unique<Game>(*p1_.mode, std::move(board));

  auto p1_bot = p1_.mode->make_player_bot(0);
  auto tool_ctrl = std::make_unique<ToolController>(settings_, *p1_.mode, ai_state_);
  tool_controller_ = tool_ctrl.get();
  if (p1_bot) {
    p1_.controllers.push_back(std::move(tool_ctrl));
    p1_.controllers.push_back(std::make_unique<TbpController>(
        std::move(p1_bot), p1_.mode->think_time_ms(0)));
  } else {
    auto ai_ctrl = std::make_unique<AIController>();
    ai_controller_ = ai_ctrl.get();
    p1_.controllers.push_back(std::make_unique<PlayerController>(settings_));
    p1_.controllers.push_back(std::move(tool_ctrl));
    p1_.controllers.push_back(std::move(ai_ctrl));
  }
  game_renderer_ = std::make_unique<Renderer>(renderer_, settings_);

  auto now = SdlClock::now();
  p1_.mode->on_start(now);
  p1_.game->drain_events();

  if (p2_.mode) {
    Board board2;
    p2_.mode->setup_board(board2);
    p2_.game = std::make_unique<Game>(*p2_.mode, std::move(board2));
    p2_.mode->on_start(now);
    p2_.game->drain_events();
    auto bot = p1_.mode->make_player_bot(1);
    if (!bot) bot = std::make_unique<tbp::InternalTbpBot>();
    p2_.controllers.push_back(std::make_unique<TbpController>(
        std::move(bot), p1_.mode->think_time_ms(1)));
  }
}

void GameManager::reset() {
  p1_.stats.reset();
  p2_.stats.reset();
  p1_.cmds.clear();
  p2_.cmds.clear();
  match_state_ = MatchState{};

  ai_state_.clear_search_state();
  if (ai_state_.active)
    ai_state_.needs_search = true;

  Board board;
  p1_.mode->setup_board(board);
  p1_.game = std::make_unique<Game>(*p1_.mode, std::move(board));

  for (auto &ctrl : p1_.controllers)
    ctrl->reset();

  auto now = SdlClock::now();
  p1_.mode->on_start(now);

  for (auto &ev : p1_.game->drain_events())
    p1_.stats.process_event(ev, now);

  if (p2_.game) {
    Board board2;
    p2_.mode->setup_board(board2);
    p2_.game = std::make_unique<Game>(*p2_.mode, std::move(board2));
    p2_.mode->on_start(now);
    p2_.game->drain_events();
    for (auto &ctrl : p2_.controllers)
      ctrl->reset();
  }
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
        if (!ev.key.repeat && !io.WantCaptureKeyboard
            && !imp::import_modal_is_open())
          input_events.push_back(
              KeyDown{ev.key.key, TimePoint(Duration(ev.key.timestamp))});
        break;
      case SDL_EVENT_KEY_UP:
        if (!io.WantCaptureKeyboard && !imp::import_modal_is_open())
          input_events.push_back(
              KeyUp{ev.key.key, TimePoint(Duration(ev.key.timestamp))});
        break;
      }
    }

    // 2. Controller timers + input processing
    auto game_state = p1_.game->state();
    for (auto &ctrl : p1_.controllers)
      ctrl->tick(now, game_state, p1_.cmds);
    if (p2_.game) {
      auto game2_state = p2_.game->state();
      for (auto &ctrl : p2_.controllers)
        ctrl->tick(now, game2_state, p2_.cmds);
    }
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
      for (auto &ctrl : p1_.controllers)
        ctrl->handle_event(iev, event_time, game_state, p1_.cmds);
    }
    input_events.clear();

    // 3. Engine apply + tick
    p1_.game->apply(p1_.cmds);
    p1_.game->tick(now);
    p1_.cmds.clear();
    if (p2_.game) {
      p2_.game->apply(p2_.cmds);
      p2_.game->tick(now);
      p2_.cmds.clear();
    }

    // 4. Drain engine events + pump AI
    CommandBuffer rule_cmds;
    CommandBuffer rule_cmds2;
    process_events(p1_, now, rule_cmds);
    if (p2_.game)
      process_events(p2_, now, rule_cmds2);
    pump_ai(now, p1_.game->state());

    // 4b. Route garbage between games, then snapshot winner state.
    if (p2_.game) {
      route_garbage_between(*p1_.game, *p2_.game, rule_cmds, rule_cmds2, gap_rng_);
      update_match_state(match_state_, *p1_.game, p2_.game.get());
    }

    if (!rule_cmds.empty()) {
      p1_.game->apply(rule_cmds);
      CommandBuffer discard;
      process_events(p1_, now, discard);
    }
    if (p2_.game && !rule_cmds2.empty()) {
      p2_.game->apply(rule_cmds2);
      CommandBuffer discard2;
      process_events(p2_, now, discard2);
    }

    // Auto-restart on win for training modes
    if (p1_.game->state().game_over && p1_.game->state().won &&
        p1_.mode->auto_restart()) {
      reset();
      goto run_start;
    }

    // Mode-requested restart (e.g. debug-window buttons).
    if (p1_.mode->consume_restart_request()) {
      reset();
      goto run_start;
    }

    // 5. Render: ImGui frame + embedded board texture
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    for (auto &ctrl : p1_.controllers)
      ctrl->draw_imgui();
    p1_.mode->draw_imgui();

    if (p2_.game) {
      VersusViewModel vvm = build_versus_view_model(now);
      draw_versus_ui(*game_renderer_, window_, vvm, settings_);
    } else {
      ViewModel vm = build_view_model(now);
      std::vector<IController *> ctrl_ptrs;
      ctrl_ptrs.reserve(p1_.controllers.size());
      for (auto &c : p1_.controllers)
        ctrl_ptrs.push_back(c.get());
      draw_game_ui(*game_renderer_, window_, vm, settings_, p1_.mode.get(),
                   ctrl_ptrs, &ai_state_, ai_controller_,
                   tool_controller_->debug());
    }

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

void GameManager::process_events(Side &side, TimePoint now,
                                 CommandBuffer &rule_cmds) {
  bool is_p1 = (&side == &p1_);
  auto events = side.game->drain_events();

  for (auto &ev : events) {
    auto st = side.game->state();
    for (auto &ctrl : side.controllers)
      ctrl->notify(ev, now, st);

    bool undo = side.stats.process_event(ev, now);

    if (auto *pl = std::get_if<eng::PieceLocked>(&ev)) {
      if (is_p1)
        ai_state_.on_piece_locked(*pl, side.game->state());
      side.mode->on_piece_locked(*pl, side.game->state(), rule_cmds);
    } else if (auto *gm = std::get_if<eng::GarbageMaterialized>(&ev)) {
      if (is_p1)
        ai_state_.on_garbage(gm->lines, side.game->state());
    } else if (auto *n = std::get_if<Notification>(&ev)) {
      if (auto *ir = std::get_if<note::InputRegistered>(n))
        side.mode->on_input_registered(ir->input, side.game->state());
    } else if (undo && is_p1) {
      for (auto &ctrl : side.controllers)
        ctrl->reset();
      side.mode->on_undo(side.game->state());
      ai_state_.on_undo();
    }
  }

  auto post_state = side.game->state();
  for (auto &ctrl : side.controllers)
    ctrl->post_hook(now, post_state);

  side.mode->on_tick(now, side.game->state(), rule_cmds);
}

void GameManager::pump_ai(TimePoint now, const GameState &state) {
  if (!ai_state_.active)
    return;

  if (ai_state_.poll_search()) {
    if (ai_state_.plan_computed())
      ai_state_.needs_search = false;
    else
      ai_state_.needs_search = true;
  }

  if (ai_state_.needs_search && !ai_state_.searching()) {
    ai_state_.start_search(state);
    ai_state_.needs_search = false;
  }

  if (ai_state_.autoplay && ai_state_.plan_computed() &&
      ai_state_.current_plan().feasible && !ai_state_.current_plan().empty() &&
      ai_controller_->idle()) {
    auto &step = *ai_state_.current_plan().front();
    bool uses_hold = state.current_piece.type != step.placement.type;
    ai_controller_->set_placement(step.placement, uses_hold);
  }
}

ViewModel GameManager::build_view_model(TimePoint now) {
  ViewModel vm;
  vm.state = p1_.game->state();
  vm.stats = p1_.stats.snapshot(now);

  HudData hud;
  p1_.mode->fill_hud(hud, vm.state, now);
  if (p2_.game) hud.pending_garbage_lines = p1_.game->pending_garbage_lines();
  if (!hud.center_text.empty() || !hud.game_over_label.empty() ||
      hud.pending_garbage_lines > 0)
    vm.hud = std::move(hud);

  p1_.mode->fill_plan_overlay(vm, vm.state);
  ai_state_.fill_plan_overlay(vm, vm.state);

  for (auto &ctrl : p1_.controllers)
    ctrl->fill_plan_overlay(vm, vm.state);

  return vm;
}

VersusViewModel GameManager::build_versus_view_model(TimePoint now) {
  VersusViewModel vvm;
  vvm.left = build_view_model(now);
  {
    HudData &hud = vvm.left.hud ? *vvm.left.hud : vvm.left.hud.emplace();
    hud.center_text = player_name(0);
  }

  ViewModel &right = vvm.right;
  right.state = p2_.game->state();
  right.stats = p2_.stats.snapshot(now);
  HudData hud;
  p2_.mode->fill_hud(hud, right.state, now);
  hud.pending_garbage_lines = p2_.game->pending_garbage_lines();
  hud.center_text = player_name(1);
  right.hud = std::move(hud);
  p2_.mode->fill_plan_overlay(right, right.state);
  for (auto &ctrl : p2_.controllers)
    ctrl->fill_plan_overlay(right, right.state);

  vvm.match = match_state_;
  return vvm;
}

std::string GameManager::player_name(int idx) const {
  const auto &list = idx == 0 ? p1_.controllers : p2_.controllers;
  for (const auto &c : list) {
    if (auto *tc = dynamic_cast<TbpController *>(c.get()))
      return tc->bot().info().name;
  }
  return "HUMAN";
}
