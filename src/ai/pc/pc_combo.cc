// Port of MinusKelvin's pcf combination.rs.

#include "pc_combo.h"

namespace {

// Check that the vertical (column) parity can be corrected with remaining
// pieces. L and J change parity by 1, vertical T by 1, vertical I by 2.
bool vertical_parity_ok(PcBoard board, PcPieceSet remaining, int height) {
  int remaining_pieces =
      (PcBoard::filled(height).remove(board).popcount()) / 4;

  // Pieces that never change vertical parity (in any orientation).
  uint32_t non_lj = remaining.counts[3] // S
                    + remaining.counts[4] // Z
                    + remaining.counts[2] // T
                    + remaining.counts[0] // I
                    + remaining.counts[1]; // O

  // Count filled cells in even vs odd columns.
  constexpr uint64_t even_cols =
      0b0101010101'0101010101'0101010101'0101010101'0101010101'0101010101ULL;
  uint32_t even = std::popcount(board.bits & even_cols);
  uint32_t odd = std::popcount(board.bits & ~even_cols);
  uint32_t vparity = (even > odd ? even - odd : odd - even) / 2;

  // Maximum parity change possible with remaining pieces.
  uint32_t can_change = remaining.counts[6] // L
                        + remaining.counts[5] // J
                        + remaining.counts[2] // T
                        + 2u * remaining.counts[0]; // I

  if (vparity > can_change)
    return false;

  // If forced L/J exactly consumes all change budget, parity must match.
  uint32_t must_change =
      static_cast<uint32_t>(remaining_pieces) -
      std::min(non_lj, static_cast<uint32_t>(remaining_pieces));
  if (can_change == must_change && ((vparity ^ must_change) & 1) != 0)
    return false;

  return true;
}

// Detect cyclic placement dependencies. If pieces form a circular support
// chain (A needs B placed first, B needs A placed first), the combination
// is impossible regardless of ordering.
bool has_cyclic_dependency(PcBoard inverse_placed,
                           const std::vector<PcPlacement> &placements,
                           int height) {
  // Start with unfilled cells as potential support (they might be filled by
  // future placements that are themselves supportable).
  PcBoard supports = inverse_placed;

  for (;;) {
    bool progress = false;
    for (const auto &p : placements) {
      PcBoard pb = p.board();
      if (supports.overlaps(pb))
        continue; // already accounted for
      if (p.supported_without_clears(supports)) {
        supports = supports.combine(pb);
        progress = true;
      }
    }
    if (!progress)
      break;
  }

  return supports != PcBoard::filled(height);
}

// Core recursive combination search.
bool find_combos(PcBoard board, PcBoard inverse_placed, PcPieceSet pieces,
                 int height, std::vector<PcPlacement> &placements,
                 const ComboCallback &on_combo) {
  int x = board.leftmost_empty_column(height);
  if (x >= 10)
    return true; // shouldn't happen (board full handled before recursing)

  // Lowest empty cell in the leftmost empty column.
  int y = 0;
  for (int i = 0; i < height; ++i) {
    if (!board.cell_filled(x, i)) {
      y = i;
      break;
    }
  }

  for (int piece_idx = 0; piece_idx < 7; ++piece_idx) {
    if (!pieces.contains(piece_idx))
      continue;

    for (const auto &state : pc_piece_states(height, piece_idx, y)) {
      if (x + state.width > 10)
        break; // sorted by width — all further states too wide

      PcPlacement placement{state, static_cast<uint8_t>(x)};
      PcBoard piece_board = placement.board();

      if (piece_board.overlaps(board))
        continue;

      PcBoard new_board = board.combine(piece_board);
      PcBoard new_inverse = inverse_placed.remove(piece_board);
      PcPieceSet new_pieces = pieces.without(piece_idx);

      placements.push_back(placement);

      if (has_cyclic_dependency(new_inverse, placements, height)) {
        // skip
      } else if (new_board == PcBoard::filled(height)) {
        if (!on_combo(placements)) {
          placements.pop_back();
          return false; // caller wants to stop
        }
      } else if (vertical_parity_ok(new_board, new_pieces, height)) {
        if (!find_combos(new_board, new_inverse, new_pieces, height,
                         placements, on_combo)) {
          placements.pop_back();
          return false;
        }
      }

      placements.pop_back();
    }
  }

  return true; // continue searching
}

} // namespace

void find_combinations(PcPieceSet pieces, PcBoard board, int height,
                       const ComboCallback &on_combo) {
  pc_data_init();
  std::vector<PcPlacement> placements;
  find_combos(board, PcBoard::filled(height), pieces, height, placements,
              on_combo);
}
