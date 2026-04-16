#pragma once

#include "board_bitset.h"
#include "placement.h"
#include <vector>

struct Plan {
  struct Step {
    Placement placement;
    bool uses_hold = false;
    int lines_cleared = 0;
    BoardBitset board_after;
  };

  std::vector<Step> steps;
  int current_step = 0;

  bool complete() const { return current_step >= (int)steps.size(); }
  const Step *current() const { return complete() ? nullptr : &steps[current_step]; }
  void advance() { if (!complete()) ++current_step; }
};
