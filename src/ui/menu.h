#pragma once

#include "presets/game_mode.h"
#include "settings.h"
#include <SDL3/SDL.h>
#include <memory>
#include <vector>

class Menu {
public:
  Menu(SDL_Renderer *renderer, SDL_Window *window, Settings &settings);

  // Runs its own ImGui frame loop until a mode is selected or the user
  // quits. Returns the selected game mode (or nullptr on quit).
  std::unique_ptr<GameMode> run();

private:
  enum class Screen {
    Main,
    PresetSelect,
    Settings,
    TrainModeSelect,
    VersusSetup,
  };

  std::unique_ptr<GameMode> make_selected_play_mode(int index);
  std::unique_ptr<GameMode> make_selected_training_mode(int index);

  SDL_Renderer *renderer_;
  SDL_Window *window_;
  Settings &settings_;

  Screen screen_ = Screen::Main;
  std::vector<std::unique_ptr<GameMode>> modes_;
  std::vector<std::unique_ptr<GameMode>> training_modes_;

  // Versus setup scratch — lives on the Menu so it persists across menu
  // re-entries within a single menu session. 0 = Human, 1 = Citris AI,
  // 2 = External TBP bot.
  int p1_kind_ = 0;
  int p2_kind_ = 1;
  char p1_path_[512] = {};
  char p2_path_[512] = {"./build/bin/citris-tbp"};
  int p1_beam_ = 800, p1_depth_ = 14, p1_think_ms_ = 0;
  int p2_beam_ = 800, p2_depth_ = 14, p2_think_ms_ = 0;
};
