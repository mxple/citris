#pragma once

#include "presets/game_mode.h"
#include "sdl_types.h"
#include "settings.h"
#include "ui/widget.h"
#include <SDL3/SDL.h>
#include <memory>
#include <string>
#include <vector>

class Menu {
public:
  Menu(SDL_Renderer *renderer, SDL_Window *window, Settings &settings);

  std::unique_ptr<GameMode> run();

private:
  enum class State { Main, PresetSelect };

  void rebuild_buttons();
  std::unique_ptr<GameMode> activate_item(int index);
  std::unique_ptr<GameMode> make_selected_mode();
  void draw();
  void update_cursor_selection();

  SDL_Renderer *renderer_;
  SDL_Window *window_;
  Settings &settings_;

  State state_ = State::Main;
  int cursor_ = 0;
  bool running_ = true;
  bool dirty_ = true;
  std::vector<std::unique_ptr<GameMode>> modes_;
  std::vector<std::unique_ptr<MenuButtonWidget>> buttons_;
};
