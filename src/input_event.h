#pragma once

#include "sdl_types.h"
#include <variant>

struct KeyDown {
  KeyCode key;
};
struct KeyUp {
  KeyCode key;
};
struct WindowClose {};
struct WindowResize {
  unsigned w, h;
};

using InputEvent = std::variant<KeyDown, KeyUp, WindowClose, WindowResize>;
