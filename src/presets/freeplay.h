#pragma once

#include "game_mode.h"

class FreeplayMode : public GameMode {
public:
  FreeplayMode() = default;

  // Customizable tuning for INI overrides
  void set_gravity_interval(std::chrono::milliseconds v) {
    gravity_interval_ = v;
  }
  void set_lock_delay(std::chrono::milliseconds v) { lock_delay_ = v; }
  void set_garbage_delay(std::chrono::milliseconds v) { garbage_delay_ = v; }
  void set_hard_drop_delay(std::chrono::milliseconds v) {
    hard_drop_delay_ = v;
  }
  void set_max_lock_resets(int v) { max_lock_resets_ = v; }
  void set_infinite_hold(bool v) { infinite_hold_ = v; }

  std::string title() const override { return "Freeplay"; }
  std::chrono::milliseconds gravity_interval() const override {
    return gravity_interval_;
  }
  std::chrono::milliseconds lock_delay() const override { return lock_delay_; }
  std::chrono::milliseconds garbage_delay() const override {
    return garbage_delay_;
  }
  std::chrono::milliseconds hard_drop_delay() const override {
    return hard_drop_delay_;
  }
  int max_lock_resets() const override { return max_lock_resets_; }
  bool infinite_hold() const override { return infinite_hold_; }

private:
  std::chrono::milliseconds gravity_interval_{10000};
  std::chrono::milliseconds lock_delay_{5000};
  std::chrono::milliseconds garbage_delay_{250};
  std::chrono::milliseconds hard_drop_delay_{50};
  int max_lock_resets_ = 15;
  bool infinite_hold_ = false;
};
