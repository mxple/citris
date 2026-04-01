#pragma once

#include "input.h"
#include <chrono>

class Stats {
public:
  Stats() : start_time_(std::chrono::steady_clock::now()) {}

  void add_lines(int n) { lines_ += n; }
  void add_attack(int n) { attack_ += n; }
  void add_piece() { pieces_++; }
  void add_perfect_clear() { pcs_++; }
  void add_input() { inputs_++; }

  void set_combo(int c) { combo_ = c; }
  void set_b2b(int b) { b2b_ = b; }

  struct Snapshot {
    int b2b, combo, lines, attack, pcs, inputs, pieces;
    float elapsed_s;
  };

  Snapshot snapshot(TimePoint now) const {
    return {
        .b2b = b2b_,
        .combo = combo_,
        .lines = lines_,
        .attack = attack_,
        .pcs = pcs_,
        .inputs = inputs_,
        .pieces = pieces_,
        .elapsed_s =
            std::chrono::duration<float>(now - start_time_).count(),
    };
  }

  void reset() {
    lines_ = attack_ = pcs_ = inputs_ = pieces_ = 0;
    combo_ = b2b_ = 0;
    start_time_ = std::chrono::steady_clock::now();
  }

private:
  int lines_ = 0, attack_ = 0, pcs_ = 0, inputs_ = 0, pieces_ = 0;
  int combo_ = 0, b2b_ = 0;
  TimePoint start_time_;
};
