#pragma once

#include "presets/game_mode.h"
#include "settings.h"
#include <SFML/Graphics.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class Menu {
public:
  explicit Menu(sf::RenderWindow &window, Settings &settings);

  std::unique_ptr<GameMode> run();

private:
  enum class State { Main, PresetSelect };

  std::unique_ptr<GameMode> handle_key(sf::Keyboard::Key key);
  std::unique_ptr<GameMode> make_selected_mode();
  void draw();
  void draw_items(const std::vector<std::string> &items);

  sf::RenderWindow &window_;
  Settings &settings_;
  sf::Font &font_;

  State state_ = State::Main;
  int cursor_ = 0;
  std::vector<std::unique_ptr<GameMode>> modes_;
};
