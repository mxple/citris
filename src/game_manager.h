#pragma once

#include "event.h"
#include "game.h"
#include "player.h"
#include "renderer.h"
#include "settings.h"
#include "stats.h"
#include "timer_manager.h"
#include <SFML/Graphics/RenderWindow.hpp>
#include <memory>
#include <vector>

enum class GameMode { Solo, VsAI, VsNetwork };

class GameManager {
public:
  GameManager(GameMode mode, const Settings &settings);

  void run();

private:
  bool process(const Event &ev);
  void reset();
  void route_garbage(TimePoint now);

  sf::RenderWindow window_;
  Settings settings_;
  GameMode mode_;

  Stats stats_;
  TimerManager timers_;
  std::vector<Event> pending_;

  std::unique_ptr<Game> game_;
  std::unique_ptr<IPlayer> player_;
  std::unique_ptr<Renderer> renderer_;

  bool stats_dirty_ = false;

  std::unique_ptr<Game> game2_;
  std::unique_ptr<IPlayer> player2_;
};
