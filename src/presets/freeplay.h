#pragma once

#include "game_mode.h"
#include "settings.h"

class FreeplayMode : public GameMode {
public:
  FreeplayMode(const Settings &settings) {
    gravity_interval_ = settings.game.gravity_interval;
    lock_delay_ = settings.game.lock_delay;
    garbage_delay_ = settings.game.garbage_delay;
    max_lock_resets_ = settings.game.max_lock_resets;
    infinite_hold_ = settings.game.infinite_hold;
  }

  std::string title() const override { return "Freeplay"; }
  std::chrono::milliseconds gravity_interval() const override {
    return gravity_interval_;
  }
  std::chrono::milliseconds lock_delay() const override { return lock_delay_; }
  std::chrono::milliseconds garbage_delay() const override {
    return garbage_delay_;
  }
  int max_lock_resets() const override { return max_lock_resets_; }
  bool infinite_hold() const override { return infinite_hold_; }

private:
  std::chrono::milliseconds gravity_interval_{-1};
  std::chrono::milliseconds lock_delay_{-1};
  std::chrono::milliseconds garbage_delay_{250};
  int max_lock_resets_ = -1;
  bool infinite_hold_ = true;
};
