#include "game_manager.h"
#include "human_player.h"
#include <chrono>

static constexpr auto kStatsInterval = std::chrono::milliseconds(16);

GameManager::GameManager(GameMode mode, const Settings &settings)
    : mode_(mode), settings_(settings) {
  window_ = sf::RenderWindow(
      sf::VideoMode({RenderLayout::kWindowW, RenderLayout::kWindowH}),
      "Tetris");
  window_.setKeyRepeatEnabled(false);
  game_ = std::make_unique<Game>(settings_, stats_, timers_);
  player_ = std::make_unique<HumanPlayer>(settings_, stats_, timers_);
  renderer_ = std::make_unique<Renderer>(window_, settings_, stats_);

  auto now = std::chrono::steady_clock::now();
  timers_.schedule(TimerKind::StatsRefresh, now + kStatsInterval,
                   StatsRefresh{});
}

void GameManager::reset() {
  stats_.reset();
  timers_.clear();
  game_ = std::make_unique<Game>(settings_, stats_, timers_);
  player_ = std::make_unique<HumanPlayer>(settings_, stats_, timers_);

  auto now = std::chrono::steady_clock::now();
  timers_.schedule(TimerKind::StatsRefresh, now + kStatsInterval,
                   StatsRefresh{});
}

void GameManager::run() {
  auto wrap_sfml = [this](const sf::Event &sfml_ev) {
    if (sfml_ev.is<sf::Event::Closed>())
      pending_.push_back(WindowClosed{});
    else if (auto *r = sfml_ev.getIf<sf::Event::Resized>())
      pending_.push_back(WindowResized{r->size.x, r->size.y});
    else if (auto *kp = sfml_ev.getIf<sf::Event::KeyPressed>())
      pending_.push_back(KeyPressed{kp->code});
    else if (auto *kr = sfml_ev.getIf<sf::Event::KeyReleased>())
      pending_.push_back(KeyReleased{kr->code});
  };

  while (window_.isOpen()) {
    auto now = std::chrono::steady_clock::now();

    // 1. Poll SFML events
    while (auto sfml_ev = window_.pollEvent())
      wrap_sfml(*sfml_ev);

    // 2. Collect expired timers
    timers_.collect_expired(now, pending_);

    // 3. Dispatch loop (handlers may add to pending_)
    for (int i = 0; i < pending_.size(); ++i) {
      Event ev = pending_[i];
      if (player_->process(ev, now, pending_))
        continue;
      if (game_->process(ev, now))
        continue;
      if (this->process(ev))
        continue;
    }
    pending_.clear();

    // 4. Route garbage (multiplayer)
    if (mode_ != GameMode::Solo)
      route_garbage(now);

    // 5. Render if dirty
    bool board_changed = game_->dirty();
    if (board_changed)
      game_->clear_dirty();

    if (board_changed) {
      renderer_->draw(game_->state());
    } else if (stats_dirty_) {
      renderer_->draw_stats();
    }
    stats_dirty_ = false;

    // 6. Sleep until next timer or SFML event
    if (auto wake = timers_.next_deadline()) {
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

bool GameManager::process(const Event &ev) {
  if (std::holds_alternative<WindowClosed>(ev)) {
    window_.close();
    return true;
  }
  if (auto *r = std::get_if<WindowResized>(&ev)) {
    renderer_->handle_resize(r->w, r->h);
    renderer_->draw(game_->state());
    return true;
  }
  if (std::holds_alternative<StatsRefresh>(ev)) {
    stats_dirty_ = true;
    auto now = std::chrono::steady_clock::now();
    timers_.schedule(TimerKind::StatsRefresh, now + kStatsInterval,
                     StatsRefresh{});
    return true;
  }
  if (auto *kp = std::get_if<KeyPressed>(&ev)) {
    if (kp->key == sf::Keyboard::Key::Grave) {
      reset();
      return true;
    }
    if (kp->key == settings_.undo && mode_ == GameMode::Solo) {
      if (game_->undo()) {
        player_->reset_input_state();
      }
      return true;
    }
  }
  return false;
}

void GameManager::route_garbage(TimePoint now) {
  if (!game2_)
    return;

  int attack1 = game_->drain_attack();
  if (attack1 > 0)
    game2_->process(GarbageReceived{attack1, 0}, now);

  int attack2 = game2_->drain_attack();
  if (attack2 > 0)
    game_->process(GarbageReceived{attack2, 0}, now);
}
