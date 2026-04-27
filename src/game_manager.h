#pragma once

#include "ai_state.h"
#include "command.h"
#include "controller/controller.h"
#include "engine/game.h"
#include "input_event.h"
#include "match.h"
#include "presets/game_mode.h"
#include "render/renderer.h"
#include "render/view_model.h"
#include "settings.h"
#include "stats.h"
#include <SDL3/SDL.h>
#include <memory>
#include <random>
#include <vector>

class AIController;
class ToolController;

class GameManager {
public:
  // mode2 is optional. When non-null, the manager runs in versus mode:
  // a second Game is constructed with mode2, garbage routes between the two
  // games, and a MatchState tracks the winner. Single-player code paths are
  // unchanged when mode2 is null.
  GameManager(SDL_Renderer *renderer, SDL_Window *window,
              const Settings &settings, std::unique_ptr<GameMode> mode,
              std::unique_ptr<GameMode> mode2 = nullptr);

  bool run();

private:
  void reset();
  void process_engine_events(TimePoint now, CommandBuffer &rule_cmds);
  void process_p2_events(TimePoint now, CommandBuffer &rule_cmds);
  void pump_ai(TimePoint now, const GameState &state);
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

  ToolController *tool_controller_ = nullptr;

  // Versus (null in single-player). Each player owns its own GameMode
  // instance because GameMode carries per-instance start_time_.
  std::unique_ptr<GameMode> mode2_;
  std::unique_ptr<Game> game2_;
  std::vector<std::unique_ptr<IController>> controllers2_;
  CommandBuffer cmds2_;
  Stats stats2_;
  MatchState match_state_;
  std::mt19937 gap_rng_{0xC17150};

  VersusViewModel build_versus_view_model(TimePoint now);
  // Returns the display name for the player at idx (0 = p1, 1 = p2). For a
  // TbpController-driven player, the bot's advertised name; otherwise
  // "HUMAN".
  std::string player_name(int idx) const;
};
