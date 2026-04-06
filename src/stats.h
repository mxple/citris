#pragma once

#include "engine_event.h"

class Stats {
public:
  Stats() : start_time_(std::chrono::steady_clock::now()) {}


  int lines() const { return lines_; }
  TimePoint start_time() const { return start_time_; }

  struct Snapshot {
    int b2b, combo, lines, attack, pcs, inputs, pieces;
    float elapsed_s;
  };

  Snapshot snapshot(TimePoint now) const {
    TimePoint effective = end_time_.value_or(now);
    return {
        .b2b = b2b_,
        .combo = combo_,
        .lines = lines_,
        .attack = attack_,
        .pcs = pcs_,
        .inputs = inputs_,
        .pieces = pieces_,
        .elapsed_s =
            std::chrono::duration<float>(effective - start_time_).count(),
    };
  }

  // Returns true if an undo was performed (caller may need to reset controllers).
  bool process_event(const EngineEvent &ev, TimePoint now) {
    if (auto *pl = std::get_if<eng::PieceLocked>(&ev)) {
      pieces_++;
      lines_ += pl->lines_cleared;
      attack_ += pl->attack;
      combo_ = pl->new_combo;
      b2b_ = pl->new_b2b;
      if (pl->perfect_clear)
        pcs_++;
    } else if (std::holds_alternative<eng::GameOver>(ev)) {
      end_time_ = now;
    } else if (std::holds_alternative<eng::UndoPerformed>(ev)) {
      if (undo_stack_.size() > 1) {
        undo_stack_.pop_back();
        restore(undo_stack_.back());
      }
      return true;
    } else if (std::holds_alternative<eng::PieceSpawned>(ev)) {
      if (undo_stack_.size() >= kMaxUndoDepth)
        undo_stack_.pop_front();
      undo_stack_.push_back(save());
    } else if (auto *n = std::get_if<Notification>(&ev)) {
      if (std::holds_alternative<note::InputRegistered>(*n))
        inputs_++;
    }
    return false;
  }

  void reset() {
    lines_ = attack_ = pcs_ = inputs_ = pieces_ = 0;
    combo_ = b2b_ = 0;
    start_time_ = std::chrono::steady_clock::now();
    end_time_.reset();
    undo_stack_.clear();
  }

private:
  struct UndoState {
    int lines, attack, pcs, inputs, pieces;
    int combo, b2b;
    TimePoint start_time;
    std::optional<TimePoint> end_time;
  };

  UndoState save() const {
    return {lines_, attack_, pcs_, inputs_, pieces_, combo_, b2b_, start_time_,
            end_time_};
  }

  void restore(const UndoState &s) {
    lines_ = s.lines;
    attack_ = s.attack;
    pcs_ = s.pcs;
    inputs_ = s.inputs;
    pieces_ = s.pieces;
    combo_ = s.combo;
    b2b_ = s.b2b;
    start_time_ = s.start_time;
    end_time_ = s.end_time;
  }

  static constexpr size_t kMaxUndoDepth = 100;
  std::deque<UndoState> undo_stack_;

  int lines_ = 0, attack_ = 0, pcs_ = 0, inputs_ = 0, pieces_ = 0;
  int combo_ = 0, b2b_ = 0;
  TimePoint start_time_;
  std::optional<TimePoint> end_time_;
};
