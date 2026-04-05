#pragma once

#include "command.h"
#include "controller/controller.h"
#include "engine/game.h"
#include "input_event.h"
#include "presets/game_mode.h"
#include "render/renderer.h"
#include "render/view_model.h"
#include "settings.h"
#include "stats.h"
#include <SFML/Graphics/RenderWindow.hpp>
#include <memory>
#include <vector>

class GameManager {
public:
  GameManager(sf::RenderWindow &window, const Settings &settings,
              std::unique_ptr<GameMode> mode);

  void run();

private:
  void reset();
  void process_engine_events(TimePoint now, CommandBuffer &rule_cmds);
  void route_garbage(TimePoint now, CommandBuffer &cmds);
  ViewModel build_view_model(TimePoint now);

  sf::RenderWindow &window_;
  const Settings &settings_;
  std::unique_ptr<GameMode> mode_;

  Stats stats_;
  CommandBuffer cmds_;

  std::unique_ptr<Game> game_;
  std::vector<std::unique_ptr<IController>> controllers_;
  std::unique_ptr<Renderer> renderer_;

  TimePoint next_stats_refresh_{};

  // Multiplayer (partial)
  std::unique_ptr<Game> game2_;
  std::vector<std::unique_ptr<IController>> controllers2_;
};
