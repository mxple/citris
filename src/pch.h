#pragma once

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <spdlog/fmt/fmt.h>
#include <string>
#include <variant>
#include <vector>

#include "log.h"

#include <SDL3/SDL_timer.h>

// Single clock source backed by SDL_GetTicksNS().
// All game timing flows through this — no steady_clock dependency,
// and SDL event timestamps can be used directly without conversion.
struct SdlClock {
  using rep = uint64_t;
  using period = std::nano;
  using duration = std::chrono::duration<rep, period>;
  using time_point = std::chrono::time_point<SdlClock>;
  static constexpr bool is_steady = true;
  static time_point now() noexcept {
    return time_point(duration(SDL_GetTicksNS()));
  }
};

using TimePoint = SdlClock::time_point;
using Duration = SdlClock::duration;

struct Color {
  uint8_t r = 0, g = 0, b = 0, a = 255;
  constexpr Color() = default;
  constexpr Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
      : r(r), g(g), b(b), a(a) {}
  static constexpr Color Transparent() { return {0, 0, 0, 0}; }
  static constexpr Color White() { return {255, 255, 255, 255}; }
  SDL_FColor to_sdl() const {
    return {r / 255.f, g / 255.f, b / 255.f, a / 255.f};
  }
};

using KeyCode = SDL_Keycode;
