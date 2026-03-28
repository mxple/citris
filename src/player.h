#pragma once

#include "game_state.h"
#include "input.h"
#include <SFML/Window/Keyboard.hpp>
#include <optional>

class IPlayer {
public:
  virtual ~IPlayer() = default;

  virtual std::optional<Input> poll(const GameState &state) = 0;

  virtual void on_key_pressed(sf::Keyboard::Key key) {}
  virtual void on_key_released(sf::Keyboard::Key key) {}

  virtual void tick(TimePoint now) {}

  // Earliest time this player needs a wakeup (for DAS/ARR timers).
  virtual std::optional<TimePoint> next_wakeup() const { return std::nullopt; }
};
