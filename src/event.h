#pragma once

#include "input.h"
#include <SFML/Window/Keyboard.hpp>
#include <variant>

// SFML-level
struct KeyPressed {
  sf::Keyboard::Key key;
};
struct KeyReleased {
  sf::Keyboard::Key key;
};
struct WindowClosed {};
struct WindowResized {
  unsigned w, h;
};

// Player-level (fired by timers)
struct DASCharged {
  Input direction;
};
struct ARRTick {
  Input direction;
};
struct SoftDropTick {};

// Game-level
struct MoveInput {
  Input input;
};
struct Gravity {};
struct LockDelayExpired {};
struct GarbageDelayExpired {};
struct GarbageReceived {
  int lines;
  int gap_col;
};

// Instant-rate state signaling (ARR=0, soft drop interval=0)
struct ARRActive {
  Input direction;
};
struct ARRInactive {};
struct SoftDropActive {};
struct SoftDropInactive {};

// Rendering
struct StatsRefresh {};

// Control
struct Restart {};

using Event =
    std::variant<KeyPressed, KeyReleased, WindowClosed, WindowResized,
                 DASCharged, ARRTick, SoftDropTick, MoveInput, Gravity,
                 LockDelayExpired, GarbageDelayExpired, GarbageReceived,
                 ARRActive, ARRInactive, SoftDropActive, SoftDropInactive,
                 StatsRefresh, Restart>;
