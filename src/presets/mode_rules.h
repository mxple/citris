#pragma once

#include "engine/piece_queue.h"
#include "engine/piece_source.h"
#include <chrono>
#include <memory>

class Board;

class ModeRules {
public:
  virtual ~ModeRules() = default;

  virtual std::chrono::milliseconds gravity_interval() const {
    return std::chrono::milliseconds{10000};
  }
  virtual std::chrono::milliseconds lock_delay() const {
    return std::chrono::milliseconds{5000};
  }
  virtual std::chrono::milliseconds garbage_delay() const {
    return std::chrono::milliseconds{250};
  }
  virtual int max_lock_resets() const { return 15; }
  virtual bool infinite_hold() const { return false; }
  virtual bool hold_allowed() const { return true; }
  virtual bool undo_allowed() const { return true; }
  virtual PieceQueue create_queue(unsigned seed) const {
    return PieceQueue(std::make_unique<SevenBagSource>(seed));
  }
  virtual int queue_visible() const { return 5; }
  virtual bool auto_restart() const { return false; }

  virtual void setup_board(Board &) {}
};
