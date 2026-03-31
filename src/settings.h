#pragma once
#include <SFML/Window/Keyboard.hpp>
#include <chrono>
#include <string>

using Duration = std::chrono::steady_clock::duration;

struct Settings {
  // Rendering
  std::string skin_path = "assets/skin.png";

  // Controls
  sf::Keyboard::Key move_left = sf::Keyboard::Key::J;
  sf::Keyboard::Key move_right = sf::Keyboard::Key::L;
  sf::Keyboard::Key rotate_cw = sf::Keyboard::Key::D;
  sf::Keyboard::Key rotate_ccw = sf::Keyboard::Key::S;
  sf::Keyboard::Key rotate_180 = sf::Keyboard::Key::A;
  sf::Keyboard::Key hard_drop = sf::Keyboard::Key::Space;
  sf::Keyboard::Key soft_drop = sf::Keyboard::Key::K;
  sf::Keyboard::Key hold = sf::Keyboard::Key::LShift;

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

  // Load from INI file. Missing keys keep defaults. Returns false on file error.
  bool load(const std::string &path);
};
