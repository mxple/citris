#pragma once

#include <SFML/Window/Keyboard.hpp>
#include <variant>

struct KeyDown {
  sf::Keyboard::Key key;
};
struct KeyUp {
  sf::Keyboard::Key key;
};
struct WindowClose {};
struct WindowResize {
  unsigned w, h;
};

using InputEvent = std::variant<KeyDown, KeyUp, WindowClose, WindowResize>;
