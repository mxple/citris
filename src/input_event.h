#pragma once

#include <variant>

struct KeyDown {
  KeyCode key;
  TimePoint timestamp{};
};
struct KeyUp {
  KeyCode key;
  TimePoint timestamp{};
};
struct WindowClose {};
struct WindowResize {
  unsigned w, h;
};

using InputEvent = std::variant<KeyDown, KeyUp, WindowClose, WindowResize>;
