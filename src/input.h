#pragma once

#include <chrono>
#include <variant>

using TimePoint = std::chrono::steady_clock::time_point;
using Duration = std::chrono::steady_clock::duration;

// Low-level game inputs (no DAS/ARR — those live in HumanPlayer).
enum class Input {
  Left,
  Right,
  RotateCW,
  RotateCCW,
  Rotate180,
  SoftDrop,
  HardDrop,
  Hold
};

enum class TimerKind { Gravity, LockDelay, GarbageDelay, N };

struct InputEvent {
  Input input;
};

struct TimerEvent {
  TimerKind kind;
};

struct GarbageEvent {
  int lines;
  int gap_col;
};

using GameEvent = std::variant<InputEvent, TimerEvent, GarbageEvent>;
