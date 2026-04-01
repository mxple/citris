#pragma once

#include <chrono>

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
