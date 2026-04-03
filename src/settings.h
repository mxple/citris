#pragma once
#include <SFML/Window/Keyboard.hpp>
#include <chrono>
#include <string>

using Duration = std::chrono::steady_clock::duration;

struct Settings {
  // Rendering
  std::string skin_path = "assets/skin.png";
  std::string font_path = "assets/FreeMono.otf";

  // Controls
  sf::Keyboard::Key move_left = sf::Keyboard::Key::Left;
  sf::Keyboard::Key move_right = sf::Keyboard::Key::Right;
  sf::Keyboard::Key rotate_cw = sf::Keyboard::Key::Up;
  sf::Keyboard::Key rotate_ccw = sf::Keyboard::Key::Z;
  sf::Keyboard::Key rotate_180 = sf::Keyboard::Key::A;
  sf::Keyboard::Key hard_drop = sf::Keyboard::Key::Space;
  sf::Keyboard::Key soft_drop = sf::Keyboard::Key::Down;
  sf::Keyboard::Key hold = sf::Keyboard::Key::C;
  sf::Keyboard::Key undo = sf::Keyboard::Key::U;

  // Input tuning
  std::chrono::milliseconds das{110};
  std::chrono::milliseconds arr{0};
  std::chrono::milliseconds soft_drop_interval{0};
  bool das_preserve_charge =
      true; // preserve DAS charge when direction interrupted

  // Game tuning
  std::chrono::milliseconds gravity_interval{10000};
  std::chrono::milliseconds lock_delay{5000};
  std::chrono::milliseconds garbage_delay{250};
  int max_lock_resets = 15;
  bool infinite_hold = false;
  std::chrono::milliseconds hard_drop_delay{50};

  // Ghost piece rendering
  bool colored_ghost = true;
  uint8_t ghost_opacity = 100; // 0-255, converted from 0-100 in INI

  // Gridlines
  uint8_t grid_opacity = 40; // 0-255

  // Load from INI file. Missing keys keep defaults. Returns false on file
  // error.
  bool load(const std::string &path);
};
