#pragma once
#include <SFML/Window/Keyboard.hpp>
#include <string>
#include <unordered_map>

struct Settings {
  // Controls
  sf::Keyboard::Key move_left = sf::Keyboard::Key::J;
  sf::Keyboard::Key move_right = sf::Keyboard::Key::L;
  sf::Keyboard::Key rotate_cw = sf::Keyboard::Key::D;
  sf::Keyboard::Key rotate_ccw = sf::Keyboard::Key::S;
  sf::Keyboard::Key rotate_180 = sf::Keyboard::Key::A;
  sf::Keyboard::Key hard_drop = sf::Keyboard::Key::Space;
  sf::Keyboard::Key soft_drop = sf::Keyboard::Key::K;
  sf::Keyboard::Key hold = sf::Keyboard::Key::LShift;

  // Tuning (milliseconds)
  int das = 130;
  int arr = 10;
  int soft_drop_interval = 50;
};
