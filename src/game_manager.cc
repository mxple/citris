#include "game_manager.h"
#include "controller/player_controller.h"
#include "controller/tool_controller.h"
#include "ui/game_ui.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <chrono>

GameManager::GameManager(SDL_Renderer *renderer, SDL_Window *window,
                         const Settings &settings,
                         std::unique_ptr<GameMode> mode)
    : renderer_(renderer), window_(window), settings_(settings),
      mode_(std::move(mode)) {
  Board board;
  mode_->setup_board(board);
  game_ = std::make_unique<Game>(*mode_, std::move(board));
  controllers_.push_back(std::make_unique<PlayerController>(settings_));
  controllers_.push_back(std::make_unique<ToolController>(settings_, *mode_));
  game_renderer_ = std::make_unique<Renderer>(renderer_, settings_);

  auto now = std::chrono::steady_clock::now();
  mode_->on_start(now);

  game_->drain_events();
}

void GameManager::reset() {
  stats_.reset();
  cmds_.clear();

  Board board;
  mode_->setup_board(board);
  game_ = std::make_unique<Game>(*mode_, std::move(board));
  controllers_.clear();
  controllers_.push_back(std::make_unique<PlayerController>(settings_));
  controllers_.push_back(std::make_unique<ToolController>(settings_, *mode_));

  auto now = std::chrono::steady_clock::now();
  mode_->on_start(now);

  for (auto &ev : game_->drain_events())
    stats_.process_event(ev, now);
}

bool GameManager::run() {
run_start:
  std::vector<InputEvent> input_events;

  while (running_) {
    auto now = std::chrono::steady_clock::now();

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
          input_events.push_back(KeyDown{ev.key.key});
        break;
      case SDL_EVENT_KEY_UP:
        if (!io.WantCaptureKeyboard)
          input_events.push_back(KeyUp{ev.key.key});
        break;
      }
    }

    // 2. Controller timers + input processing
    for (auto &ctrl : controllers_)
      ctrl->check_timers(now, cmds_);

    auto game_state = game_->state();
    for (auto &iev : input_events) {
      if (std::holds_alternative<WindowClose>(iev)) {
        running_ = false;
        return running_;
      }
      if (auto *kd = std::get_if<KeyDown>(&iev)) {
        if (kd->key == SDLK_BACKSPACE) {
          return running_;
        } else if (kd->key == SDLK_GRAVE) {
          reset();
          goto run_start;
        }
      }
      for (auto &ctrl : controllers_)
        ctrl->update(iev, now, game_state, cmds_);
    }
    input_events.clear();

    // 3. Engine apply + tick
    game_->apply(cmds_);
    game_->tick(now);
    cmds_.clear();

    // 4. Drain engine events
    CommandBuffer rule_cmds;
    process_engine_events(now, rule_cmds);

    if (!rule_cmds.empty()) {
      game_->apply(rule_cmds);
      CommandBuffer discard;
      process_engine_events(now, discard);
    }

    // 5. Render: ImGui frame + embedded board texture
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    ViewModel vm = build_view_model(now);
    draw_game_ui(*game_renderer_, window_, vm, settings_);

    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
    SDL_RenderPresent(renderer_);
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
      mode_->on_piece_locked(*pl, game_->state(), rule_cmds);
    } else if (undo) {
      for (auto &ctrl : controllers_)
        ctrl->reset_input_state();
    }
  }

  mode_->on_tick(now, game_->state(), rule_cmds);
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

  return vm;
}
