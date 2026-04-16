#pragma once

#include "board_bitset.h"
#include "plan.h"
#include "render/view_model.h"
#include <array>
#include <span>
#include <vector>

// Build plan overlay placements using a row-index map so that line clears
// correctly shift subsequent pieces in the display frame.
inline std::vector<PlannedPlacement>
build_plan_overlay(BoardBitset board, std::span<const Plan::Step> steps,
                   int max_visible = 7) {
  // row_map[working_y] = display y in the original board frame
  std::array<int, BoardBitset::kHeight> row_map;
  for (int i = 0; i < BoardBitset::kHeight; ++i)
    row_map[i] = i;
  int active_rows = BoardBitset::kHeight;

  std::vector<PlannedPlacement> out;
  int count = std::min(max_visible, static_cast<int>(steps.size()));

  for (int i = 0; i < count; ++i) {
    auto &step = steps[i];
    auto raw_cells = step.placement.cells();

    PlannedPlacement pp;
    pp.type = step.placement.type;
    pp.step_number = i;
    for (int c = 0; c < 4; ++c) {
      int wy = raw_cells[c].y;
      int display_y = (wy >= 0 && wy < active_rows) ? row_map[wy] : wy;
      pp.cells[c] = {raw_cells[c].x, display_y};
    }
    out.push_back(pp);

    // Simulate placement and compact row_map for cleared lines
    board.place(step.placement.type, step.placement.rotation,
                step.placement.x, step.placement.y);

    int write = 0;
    for (int r = 0; r < active_rows; ++r) {
      if (!board.row_full(r)) {
        row_map[write] = row_map[r];
        ++write;
      }
    }
    active_rows = write;

    board.clear_lines();
  }

  return out;
}
