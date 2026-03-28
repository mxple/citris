#pragma once

#include "game.h"
#include "player.h"
#include "renderer.h"
#include "settings.h"
#include <SFML/Graphics/RenderWindow.hpp>
#include <memory>

enum class GameMode { Solo, VsAI, VsNetwork };

class GameManager {
public:
  GameManager(GameMode mode, const Settings &settings);

  void run();
  void reset();

private:
  void handle_window_events();
  void dispatch_event(const sf::Event &event);
  void drain_player_inputs();
  void route_garbage();

  // Returns the earliest wakeup across game timers and player timers.
  std::optional<TimePoint> next_wakeup() const;

  sf::RenderWindow window_;
  Settings settings_;
  GameMode mode_;

  std::unique_ptr<Game> game_;
  std::unique_ptr<IPlayer> player_;
  std::unique_ptr<Renderer> renderer_;

  std::unique_ptr<Game> game2_;
  std::unique_ptr<IPlayer> player2_;
};
