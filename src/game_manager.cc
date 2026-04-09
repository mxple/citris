#include "game_manager.h"
#include "controller/player_controller.h"
#include "controller/tool_controller.h"
#include <chrono>

static constexpr auto kStatsInterval = std::chrono::milliseconds(16);

GameManager::GameManager(sf::RenderWindow &window, const Settings &settings,
                         std::unique_ptr<GameMode> mode)
    : window_(window), settings_(settings), mode_(std::move(mode)) {
  Board board;
  mode_->setup_board(board);
  game_ = std::make_unique<Game>(*mode_, std::move(board));
  controllers_.push_back(std::make_unique<PlayerController>(settings_));
  controllers_.push_back(std::make_unique<ToolController>(settings_, *mode_));
  renderer_ = std::make_unique<Renderer>(window_, settings_);

  auto now = std::chrono::steady_clock::now();
  mode_->on_start(now);
  next_stats_refresh_ = now + kStatsInterval;

  // Drain initial events (PieceSpawned from constructor)
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
  next_stats_refresh_ = now + kStatsInterval;

  // Drain initial events — push stats snapshot for first piece
  for (auto &ev : game_->drain_events())
    stats_.process_event(ev, now);
}

void GameManager::run() {
  auto sz = window_.getSize();
  handle_resize(window_, sz.x, sz.y, settings_.auto_scale);

run_start:
  std::vector<InputEvent> input_events;

  auto wrap_sfml = [&](const sf::Event &sfml_ev) {
    if (sfml_ev.is<sf::Event::Closed>())
      input_events.push_back(WindowClose{});
    else if (auto *r = sfml_ev.getIf<sf::Event::Resized>())
      input_events.push_back(WindowResize{r->size.x, r->size.y});
    else if (auto *kp = sfml_ev.getIf<sf::Event::KeyPressed>())
      input_events.push_back(KeyDown{kp->code});
    else if (auto *kr = sfml_ev.getIf<sf::Event::KeyReleased>())
      input_events.push_back(KeyUp{kr->code});
  };

  while (window_.isOpen()) {
    auto now = std::chrono::steady_clock::now();

    // 1. Collect external input events
    while (auto sfml_ev = window_.pollEvent())
      wrap_sfml(*sfml_ev);

    // 2. Controller timers + input processing
    for (auto &ctrl : controllers_)
      ctrl->check_timers(now, cmds_);

    auto game_state = game_->state();
    for (auto &ev : input_events) {
      // window events
      if (std::holds_alternative<WindowClose>(ev)) {
        window_.close();
        return;
      } else if (auto *wr = std::get_if<WindowResize>(&ev)) {
        handle_resize(window_, wr->w, wr->h, settings_.auto_scale);
      }

      // keyboard events
      if (auto *kd = std::get_if<KeyDown>(&ev)) {
        if (kd->key == sf::Keyboard::Key::Backspace) {
          return;
        } else if (kd->key == sf::Keyboard::Key::Grave) {
          reset();
          goto run_start;
        }
      }
      for (auto &ctrl : controllers_)
        ctrl->update(ev, now, game_state, cmds_);
    }
    input_events.clear();

    // 3. Engine apply + tick
    game_->apply(cmds_);
    game_->tick(now);
    cmds_.clear();

    // 4. Drain engine events -> stats + mode hooks
    CommandBuffer rule_cmds;
    process_engine_events(now, rule_cmds);

    // Apply rule-generated commands (e.g. SetGameOver from presets)
    if (!rule_cmds.empty()) {
      game_->apply(rule_cmds);
      CommandBuffer discard;
      process_engine_events(now, discard);
    }

    // Route garbage (multiplayer)
    // if (game2_) {
    //   CommandBuffer garbage_cmds;
    //   route_garbage(now, garbage_cmds);
    //   // Apply garbage to opponent engines...
    // }

    // Render
    bool board_changed = game_->dirty();
    if (board_changed)
      game_->clear_dirty();

    bool stats_due = now >= next_stats_refresh_;
    if (stats_due)
      next_stats_refresh_ = now + kStatsInterval;

    if (board_changed) {
      renderer_->draw(build_view_model(now));
    } else if (stats_due) {
      renderer_->draw_stats(build_view_model(now));
    }

    // Sleep until next deadline
    std::optional<TimePoint> wake;
    auto consider = [&](std::optional<TimePoint> tp) {
      if (tp && (!wake || *tp < *wake))
        wake = tp;
    };
    consider(game_->next_deadline());
    for (auto &ctrl : controllers_)
      consider(ctrl->next_deadline());
    consider(next_stats_refresh_);

    if (wake) {
      now = std::chrono::steady_clock::now();
      auto remaining = *wake - now;
      auto us =
          std::chrono::duration_cast<std::chrono::microseconds>(remaining);
      auto timeout = sf::microseconds(std::max(us.count(), int64_t{1}));
      if (auto ev = window_.waitEvent(timeout))
        wrap_sfml(*ev);
    } else {
      if (auto ev = window_.waitEvent())
        wrap_sfml(*ev);
    }
  }
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
