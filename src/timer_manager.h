#pragma once

#include "event.h"
#include <array>
#include <optional>
#include <vector>

enum class TimerKind {
  Gravity,
  LockDelay,
  GarbageDelay,
  DAS_Left,
  DAS_Right,
  ARR_Left,
  ARR_Right,
  SoftDrop,
  StatsRefresh,
  N
};

class TimerManager {
  struct Slot {
    TimePoint deadline;
    Event event;
  };
  std::array<std::optional<Slot>, static_cast<size_t>(TimerKind::N)> slots_;

public:
  void schedule(TimerKind kind, TimePoint deadline, Event event) {
    slots_[static_cast<size_t>(kind)] = Slot{deadline, std::move(event)};
  }

  void cancel(TimerKind kind) {
    slots_[static_cast<size_t>(kind)].reset();
  }

  bool active(TimerKind kind) const {
    return slots_[static_cast<size_t>(kind)].has_value();
  }

  void collect_expired(TimePoint now, std::vector<Event> &out) {
    for (auto &slot : slots_) {
      if (slot && slot->deadline <= now) {
        out.push_back(std::move(slot->event));
        slot.reset();
      }
    }
  }

  std::optional<TimePoint> next_deadline() const {
    std::optional<TimePoint> earliest;
    for (auto &slot : slots_) {
      if (slot && (!earliest || slot->deadline < *earliest))
        earliest = slot->deadline;
    }
    return earliest;
  }

  void clear() {
    for (auto &slot : slots_)
      slot.reset();
  }
};
