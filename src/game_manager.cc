#include "game_manager.h"
#include "controller/ai_controller.h"
#include "controller/player_controller.h"
#include "controller/tbp_controller.h"
#include "controller/tool_controller.h"
#include "tbp/bot.h"
#include "tbp/internal_bot.h"
#include "ui/game_ui.h"

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
    : renderer_(renderer), window_(window), settings_(settings),
      mode_(std::move(mode)), mode2_(std::move(mode2)) {
  Board board;
  mode_->setup_board(board);
  game_ = std::make_unique<Game>(*mode_, std::move(board));

  // P1 wiring depends on whether the mode declares p1 as a bot. Default
  // (single-player + versus-with-human-p1) keeps the existing Player +
  // Tool + AIController stack. If the mode returns a bot for p1, we swap
  // PlayerController + AIController for a TbpController and leave the
  // ai_state_/ai_controller_ inert (ai_state_.active stays false).
  auto p1_bot = mode_->make_player_bot(0);
  auto tool_ctrl = std::make_unique<ToolController>(settings_, *mode_, ai_state_);
  tool_controller_ = tool_ctrl.get();
  if (p1_bot) {
    controllers_.push_back(std::move(tool_ctrl));
    controllers_.push_back(std::make_unique<TbpController>(
        std::move(p1_bot), mode_->think_time_ms(0)));
  } else {
    auto ai_ctrl = std::make_unique<AIController>();
    ai_controller_ = ai_ctrl.get();
    controllers_.push_back(std::make_unique<PlayerController>(settings_));
    controllers_.push_back(std::move(tool_ctrl));
    controllers_.push_back(std::move(ai_ctrl));
  }
  game_renderer_ = std::make_unique<Renderer>(renderer_, settings_);

  auto now = SdlClock::now();
  mode_->on_start(now);
  game_->drain_events();

  if (mode2_) {
    Board board2;
    mode2_->setup_board(board2);
    game2_ = std::make_unique<Game>(*mode2_, std::move(board2));
    mode2_->on_start(now);
    game2_->drain_events();
    // The P1-side mode decides who drives player 2 (VersusMode routes this
    // through its PlayerConfig). Fallback: in-process AI so a versus mode
    // that doesn't override the hook still gets a playable opponent.
    auto bot = mode_->make_player_bot(1);
    if (!bot) bot = std::make_unique<tbp::InternalTbpBot>();
    controllers2_.push_back(std::make_unique<TbpController>(
        std::move(bot), mode_->think_time_ms(1)));
  }
}

void GameManager::reset() {
  stats_.reset();
  stats2_.reset();
  cmds_.clear();
  cmds2_.clear();
  match_state_ = MatchState{};

  ai_state_.clear_search_state();
  if (ai_state_.active)
    ai_state_.needs_search = true;

  Board board;
  mode_->setup_board(board);
  game_ = std::make_unique<Game>(*mode_, std::move(board));

  for (auto &ctrl : controllers_)
    ctrl->reset();

  auto now = SdlClock::now();
  mode_->on_start(now);

  for (auto &ev : game_->drain_events())
    stats_.process_event(ev, now);

  if (mode2_) {
    Board board2;
    mode2_->setup_board(board2);
    game2_ = std::make_unique<Game>(*mode2_, std::move(board2));
    mode2_->on_start(now);
    game2_->drain_events();
    // Reset after the new game exists so TbpController's next tick re-starts
    // the bot against the fresh board.
    for (auto &ctrl : controllers2_)
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
    if (game2_) {
      auto game2_state = game2_->state();
      for (auto &ctrl : controllers2_)
        ctrl->tick(now, game2_state, cmds2_);
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
      for (auto &ctrl : controllers_)
        ctrl->handle_event(iev, event_time, game_state, cmds_);
    }
    input_events.clear();

    // 3. Engine apply + tick
    game_->apply(cmds_);
    game_->tick(now);
    cmds_.clear();
    if (game2_) {
      game2_->apply(cmds2_);
      game2_->tick(now);
      cmds2_.clear();
    }

    // 4. Drain engine events + pump AI
    CommandBuffer rule_cmds;
    CommandBuffer rule_cmds2;
    process_engine_events(now, rule_cmds);
    if (game2_)
      process_p2_events(now, rule_cmds2);
    pump_ai(now, game_->state());

    // 4b. Route garbage between games (after both have drained), then snapshot
    // winner state. Both rule_cmds buffers may receive an AddGarbage command
    // here — applied alongside any mode-generated rule commands below.
    if (game2_) {
      route_garbage_between(*game_, *game2_, rule_cmds, rule_cmds2, gap_rng_);
      update_match_state(match_state_, *game_, game2_.get());
    }

    if (!rule_cmds.empty()) {
      game_->apply(rule_cmds);
      CommandBuffer discard;
      process_engine_events(now, discard);
    }
    if (game2_ && !rule_cmds2.empty()) {
      game2_->apply(rule_cmds2);
      CommandBuffer discard2;
      process_p2_events(now, discard2);
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

    if (game2_) {
      VersusViewModel vvm = build_versus_view_model(now);
      draw_versus_ui(*game_renderer_, window_, vvm, settings_);
    } else {
      ViewModel vm = build_view_model(now);
      std::vector<IController *> ctrl_ptrs;
      ctrl_ptrs.reserve(controllers_.size());
      for (auto &c : controllers_)
        ctrl_ptrs.push_back(c.get());
      draw_game_ui(*game_renderer_, window_, vm, settings_, mode_.get(),
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

void GameManager::process_engine_events(TimePoint now,
                                        CommandBuffer &rule_cmds) {
  auto events = game_->drain_events();

  for (auto &ev : events) {
    auto st = game_->state();
    for (auto &ctrl : controllers_)
      ctrl->notify(ev, now, st);

    bool undo = stats_.process_event(ev, now);

    if (auto *pl = std::get_if<eng::PieceLocked>(&ev)) {
      ai_state_.on_piece_locked(*pl);
      mode_->on_piece_locked(*pl, game_->state(), rule_cmds);
    } else if (auto *gm = std::get_if<eng::GarbageMaterialized>(&ev)) {
      ai_state_.on_garbage(gm->lines);
    } else if (auto *n = std::get_if<Notification>(&ev)) {
      if (auto *ir = std::get_if<note::InputRegistered>(n))
        mode_->on_input_registered(ir->input, game_->state());
    } else if (undo) {
      for (auto &ctrl : controllers_)
        ctrl->reset();
      mode_->on_undo(game_->state());
      ai_state_.on_undo();
    }
  }

  // End-of-batch hook — controllers that need to react to a completed turn
  // as a whole (rather than per-event) run their post-notify work here on
  // the final post-apply state.
  auto post_state = game_->state();
  for (auto &ctrl : controllers_)
    ctrl->post_hook(now, post_state);

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

void GameManager::process_p2_events(TimePoint now, CommandBuffer &rule_cmds) {
  // Drain events from game2, forward to controllers2_ and stats2_, let
  // mode2_ react. No AIState / undo handling yet (p2 is AI-driven today).
  auto events = game2_->drain_events();
  for (auto &ev : events) {
    auto st = game2_->state();
    for (auto &ctrl : controllers2_)
      ctrl->notify(ev, now, st);
    stats2_.process_event(ev, now);
    if (auto *pl = std::get_if<eng::PieceLocked>(&ev))
      mode2_->on_piece_locked(*pl, game2_->state(), rule_cmds);
  }
  auto post_state = game2_->state();
  for (auto &ctrl : controllers2_)
    ctrl->post_hook(now, post_state);
  mode2_->on_tick(now, game2_->state(), rule_cmds);
}

ViewModel GameManager::build_view_model(TimePoint now) {
  ViewModel vm;
  vm.state = game_->state();
  vm.stats = stats_.snapshot(now);

  HudData hud;
  mode_->fill_hud(hud, vm.state, now);
  // In versus mode, surface queued incoming garbage so the UI meter can
  // render it. In single-player the value is always zero.
  if (game2_) hud.pending_garbage_lines = game_->pending_garbage_lines();
  if (!hud.center_text.empty() || !hud.game_over_label.empty() ||
      hud.pending_garbage_lines > 0)
    vm.hud = std::move(hud);

  mode_->fill_plan_overlay(vm, vm.state);
  ai_state_.fill_plan_overlay(vm, vm.state);

  for (auto &ctrl : controllers_)
    ctrl->fill_plan_overlay(vm, vm.state);

  return vm;
}

VersusViewModel GameManager::build_versus_view_model(TimePoint now) {
  VersusViewModel vvm;
  vvm.left = build_view_model(now);
  // Label the left board with its player's name. fill_hud on FreeplayMode
  // leaves center_text empty, so we own this slot in versus.
  {
    HudData &hud = vvm.left.hud ? *vvm.left.hud : vvm.left.hud.emplace();
    hud.center_text = player_name(0);
  }

  // Build right side in-place — mirrors build_view_model but against game2_.
  ViewModel &right = vvm.right;
  right.state = game2_->state();
  right.stats = stats2_.snapshot(now);
  HudData hud;
  mode2_->fill_hud(hud, right.state, now);
  hud.pending_garbage_lines = game2_->pending_garbage_lines();
  hud.center_text = player_name(1);
  right.hud = std::move(hud);
  mode2_->fill_plan_overlay(right, right.state);
  for (auto &ctrl : controllers2_)
    ctrl->fill_plan_overlay(right, right.state);

  vvm.match = match_state_;
  return vvm;
}

std::string GameManager::player_name(int idx) const {
  const auto &list = idx == 0 ? controllers_ : controllers2_;
  for (const auto &c : list) {
    if (auto *tc = dynamic_cast<TbpController *>(c.get()))
      return tc->bot().info().name;
  }
  return "HUMAN";
}
