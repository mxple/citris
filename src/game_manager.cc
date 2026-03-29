#include "game_manager.h"
#include "human_player.h"
#include "profiler.h"
#include <SFML/Window/Keyboard.hpp>
#include <chrono>
#include <iostream>

static constexpr auto kBusyWaitThreshold = std::chrono::microseconds(500);

GameManager::GameManager(GameMode mode, const Settings &settings)
    : mode_(mode), settings_(settings) {
  window_ = sf::RenderWindow(
      sf::VideoMode({Renderer::kWindowW, Renderer::kWindowH}), "Tetris");
  window_.setKeyRepeatEnabled(false);
  game_ = std::make_unique<Game>(settings_);
  player_ = std::make_unique<HumanPlayer>(settings_);
  renderer_ = std::make_unique<Renderer>(window_);
}

void GameManager::reset() {
  game_ = std::make_unique<Game>(settings_);
  // player_ = std::make_unique<HumanPlayer>(settings_);
  // renderer_ = std::make_unique<Renderer>(window_);
}

void GameManager::run() {
  while (window_.isOpen()) {
    auto now = std::chrono::steady_clock::now();
    {
      handle_window_events();
      player_->tick(now);
      drain_player_inputs();
      game_->update(now);

      if (mode_ != GameMode::Solo)
        route_garbage();
    }

    // TODO skip if < 1 frame passed (wait vsync)
    if (game_->state_dirty()) {
      renderer_->draw(game_->state());
      game_->clear_dirty();
    }

    // Sleep until next wakeup or SFML event, whichever comes first.
    {
      auto wake = next_wakeup();
      auto timeout = sf::Time::Zero; // Zero = block indefinitely
      if (wake) {
        now = std::chrono::steady_clock::now();
        auto remaining = *wake - now;
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            remaining - kBusyWaitThreshold);
        timeout = sf::microseconds(us.count());
      }

      if (auto event = window_.waitEvent(timeout)) {
        dispatch_event(*event);
      } 
    }
  }
}

void GameManager::handle_window_events() {
  while (auto event = window_.pollEvent()) {
    dispatch_event(*event);
  }
}

void GameManager::dispatch_event(const sf::Event &event) {
  if (event.is<sf::Event::Closed>()) {
    window_.close();
    return;
  }
  if (auto *kp = event.getIf<sf::Event::KeyPressed>()) {
    if (kp->code == sf::Keyboard::Key::Grave) {
      reset();
      return;
    }
    player_->on_key_pressed(kp->code);
  }
  if (auto *kr = event.getIf<sf::Event::KeyReleased>()) {
    player_->on_key_released(kr->code);
  }
}

void GameManager::drain_player_inputs() {
  auto state = game_->state();
  while (auto input = player_->poll(state)) {
    game_->post_event(InputEvent{*input});
  }
}

void GameManager::route_garbage() {
  if (!game2_)
    return;

  int attack1 = game_->drain_attack();
  if (attack1 > 0) {
    // TODO: random gap column
    game2_->post_event(GarbageEvent{attack1, 0});
  }

  int attack2 = game2_->drain_attack();
  if (attack2 > 0) {
    game_->post_event(GarbageEvent{attack2, 0});
  }
}

std::optional<TimePoint> GameManager::next_wakeup() const {
  auto game_wake = game_->next_wakeup();
  auto player_wake = player_->next_wakeup();

  if (!game_wake)
    return player_wake;
  if (!player_wake)
    return game_wake;
  return std::min(*game_wake, *player_wake);
}
