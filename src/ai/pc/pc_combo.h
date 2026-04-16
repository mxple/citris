#pragma once

// Port of MinusKelvin's pcf combination search.

#include "pc_data.h"
#include <cstdint>
#include <functional>
#include <vector>

// Multiset of piece types (count per type).
struct PcPieceSet {
  uint8_t counts[7]{};

  bool contains(int piece_idx) const { return counts[piece_idx] > 0; }

  PcPieceSet without(int piece_idx) const {
    PcPieceSet s = *this;
    --s.counts[piece_idx];
    return s;
  }
};

// A piece placement on the PC board: a PcPieceState placed at column x.
struct PcPlacement {
  PcPieceState state;
  uint8_t x;

  PcBoard board() const { return {state.board << x}; }
  bool overlaps(PcBoard b) const { return board().overlaps(b); }

  // Can this piece be supported without any line clears?
  bool supported_without_clears(PcBoard on) const {
    if (PcBoard{kHurdleMasks[state.hurdles]}.remove(on) != PcBoard{0})
      return false;
    return state.y == 0 || on.overlaps({state.below_mask << x});
  }

  // Can this piece be supported after full lines are cleared?
  bool supported_after_clears(PcBoard on) const {
    if (PcBoard{kHurdleMasks[state.hurdles]}.remove(on) != PcBoard{0})
      return false;
    // Simulate line clears: replace each full row with the row below it.
    for (int y = 1; y < 6; ++y) {
      if (on.line_filled(y)) {
        uint64_t row_mask = 0x3FFULL << (10 * y);
        on.bits &= (on.bits << 10) | ~row_mask;
      }
    }
    return state.y == 0 || on.overlaps({state.below_mask << x});
  }
};

// Callback receives each valid combination. Return false to stop searching.
using ComboCallback = std::function<bool(const std::vector<PcPlacement> &)>;

// Find all sets of piece placements that perfectly fill the board to `height`.
// Searches using leftmost-empty-column ordering with vertical parity and
// cyclic dependency pruning.
void find_combinations(PcPieceSet pieces, PcBoard board, int height,
                       const ComboCallback &on_combo);
