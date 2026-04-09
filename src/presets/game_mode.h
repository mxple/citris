#pragma once

#include "command.h"
#include "engine_event.h"
#include "engine/game_state.h"
#include "render/view_model.h"
#include <chrono>
#include <string>

class Board;

class GameMode {
public:
  virtual ~GameMode() = default;

  virtual std::string title() const { return "Freeplay"; }

  // Tuning
  virtual std::chrono::milliseconds gravity_interval() const {
    return std::chrono::milliseconds{10000};
  }
  virtual std::chrono::milliseconds lock_delay() const {
    return std::chrono::milliseconds{5000};
  }
  virtual std::chrono::milliseconds garbage_delay() const {
    return std::chrono::milliseconds{250};
  }
  virtual std::chrono::milliseconds hard_drop_delay() const {
    return std::chrono::milliseconds{50};
  }
  virtual int max_lock_resets() const { return 15; }
  virtual bool infinite_hold() const { return false; }
  virtual bool hold_allowed() const { return true; }
  virtual bool undo_allowed() const { return true; }

  // Lifecycle
  virtual void on_start(TimePoint now) { start_time_ = now; end_time_.reset(); }
  virtual void setup_board(Board &) {}

  // Event subscribers
  virtual void on_piece_locked(const eng::PieceLocked &, const GameState &,
                               CommandBuffer &) {}
  virtual void on_tick(TimePoint, const GameState &, CommandBuffer &) {}

  // View model population
  virtual void fill_hud(HudData &, const GameState &, TimePoint) {}

protected:
  TimePoint start_time_;
  std::optional<TimePoint> end_time_;
};
