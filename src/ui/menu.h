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
  enum class Screen { Main, PresetSelect, Settings, UserModeSelect };

  std::unique_ptr<GameMode> make_selected_mode(int index);

  SDL_Renderer *renderer_;
  SDL_Window *window_;
  Settings &settings_;

  Screen screen_ = Screen::Main;
  std::vector<std::unique_ptr<GameMode>> modes_;
  std::vector<std::unique_ptr<GameMode>> user_modes_;
};
