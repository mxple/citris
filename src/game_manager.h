#pragma once

#include "ai_state.h"
#include "command.h"
#include "controller/controller.h"
#include "engine/game.h"
#include "input_event.h"
#include "presets/game_mode.h"
#include "render/renderer.h"
#include "render/view_model.h"
#include "settings.h"
#include "stats.h"
#include <SDL3/SDL.h>
#include <memory>
#include <vector>

class AIController;

class GameManager {
public:
  GameManager(SDL_Renderer *renderer, SDL_Window *window,
              const Settings &settings, std::unique_ptr<GameMode> mode);

  bool run();

private:
  void reset();
  void process_engine_events(TimePoint now, CommandBuffer &rule_cmds);
  void pump_ai(TimePoint now, const GameState &state);
  void route_garbage(TimePoint now, CommandBuffer &cmds);
  ViewModel build_view_model(TimePoint now);

  SDL_Renderer *renderer_;
  SDL_Window *window_;
  const Settings &settings_;
  std::unique_ptr<GameMode> mode_;
  bool running_ = true;

  Stats stats_;
  CommandBuffer cmds_;

  std::unique_ptr<Game> game_;
  std::vector<std::unique_ptr<IController>> controllers_;
  std::unique_ptr<Renderer> game_renderer_;

  // AI
  AIState ai_state_;
  AIController *ai_controller_ = nullptr; // owned by controllers_

  // Multiplayer (partial)
  std::unique_ptr<Game> game2_;
  std::vector<std::unique_ptr<IController>> controllers2_;
};
