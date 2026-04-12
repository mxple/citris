#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

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
