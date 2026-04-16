// Port of MinusKelvin's pcf build.rs piece generation.

#include "pc_data.h"
#include <algorithm>
#include <mutex>
#include <vector>

namespace {

struct PieceShape {
  uint16_t row_bits[4]; // cell bits per bounding-box row (row 0 = bottom)
  uint8_t width;
  uint8_t height; // number of rows in bounding box
  uint8_t piece_idx;
};

// 19 canonical piece shapes (matching pcf's build.rs definitions).
// S/Z/I have 2 shapes each (horizontal/vertical — group-2 rotational symmetry).
// J/L/T have 4 shapes each (one per rotation).
// O has 1 shape (rotational symmetry).
// clang-format off
static constexpr PieceShape kShapes[] = {
    // S-horizontal: (0,0)(1,0)(1,1)(2,1)
    {{0b011, 0b110, 0, 0}, 3, 2, /*S*/ 3},
    // S-vertical: (1,0)(1,1)(0,1)(0,2)
    {{0b10, 0b11, 0b01, 0}, 2, 3, /*S*/ 3},
    // Z-horizontal: (1,0)(2,0)(0,1)(1,1)
    {{0b110, 0b011, 0, 0}, 3, 2, /*Z*/ 4},
    // Z-vertical: (0,0)(0,1)(1,1)(1,2)
    {{0b01, 0b11, 0b10, 0}, 2, 3, /*Z*/ 4},
    // I-horizontal: (0,0)(1,0)(2,0)(3,0)
    {{0b1111, 0, 0, 0}, 4, 1, /*I*/ 0},
    // I-vertical: (0,0)(0,1)(0,2)(0,3)
    {{0b1, 0b1, 0b1, 0b1}, 1, 4, /*I*/ 0},
    // J-north: (0,0)(1,0)(2,0)(0,1)
    {{0b111, 0b001, 0, 0}, 3, 2, /*J*/ 5},
    // T-north: (0,0)(1,0)(2,0)(1,1)
    {{0b111, 0b010, 0, 0}, 3, 2, /*T*/ 2},
    // L-north: (0,0)(1,0)(2,0)(2,1)
    {{0b111, 0b100, 0, 0}, 3, 2, /*L*/ 6},
    // J-south: (2,0)(0,1)(1,1)(2,1)
    {{0b100, 0b111, 0, 0}, 3, 2, /*J*/ 5},
    // T-south: (1,0)(0,1)(1,1)(2,1)
    {{0b010, 0b111, 0, 0}, 3, 2, /*T*/ 2},
    // L-south: (0,0)(0,1)(1,1)(2,1)
    {{0b001, 0b111, 0, 0}, 3, 2, /*L*/ 6},
    // J-east: (0,0)(0,1)(0,2)(1,2)
    {{0b01, 0b01, 0b11, 0}, 2, 3, /*J*/ 5},
    // T-east: (0,0)(0,1)(1,1)(0,2)
    {{0b01, 0b11, 0b01, 0}, 2, 3, /*T*/ 2},
    // L-east: (0,0)(1,0)(0,1)(0,2)
    {{0b11, 0b01, 0b01, 0}, 2, 3, /*L*/ 6},
    // J-west: (0,0)(1,0)(1,1)(1,2)
    {{0b11, 0b10, 0b10, 0}, 2, 3, /*J*/ 5},
    // T-west: (0,1)(1,0)(1,1)(1,2)
    {{0b10, 0b11, 0b10, 0}, 2, 3, /*T*/ 2},
    // L-west: (1,0)(1,1)(0,2)(1,2)
    {{0b10, 0b10, 0b11, 0}, 2, 3, /*L*/ 6},
    // O: (0,0)(1,0)(0,1)(1,1)
    {{0b11, 0b11, 0, 0}, 2, 2, /*O*/ 1},
};
// clang-format on

struct StateTable {
  std::vector<PcPieceState> buckets[6][7][6]; // [height-1][piece][cell_y]
};

static StateTable g_table;
static std::once_flag g_init_flag;

void gen_hurdled(const PieceShape &shape, int row_idx, int row_pos,
                 uint64_t bitboard, uint64_t below, uint8_t hurdles,
                 int first_y) {
  int h = shape.height;
  if (row_idx == h) {
    // Finished placing all rows — record the state.
    int total_height = row_pos; // one past the last used row

    // Find first y where column 0 has a cell.
    int cell_y = -1;
    for (int y = 0; y < 6; ++y) {
      if (bitboard & (1ULL << (y * 10))) {
        cell_y = y;
        break;
      }
    }
    if (cell_y < 0)
      return; // no cell at column 0 (shouldn't happen)

    PcPieceState state{};
    state.board = bitboard;
    state.below_mask = below;
    state.width = shape.width;
    state.y = static_cast<uint8_t>(first_y);
    state.hurdles = hurdles;
    state.piece_idx = shape.piece_idx;

    // Add to all valid height buckets.
    for (int height = total_height; height <= 6; ++height)
      g_table.buckets[height - 1][shape.piece_idx][cell_y].push_back(state);

    return;
  }

  // How many rows are left to place (including this one)?
  int rows_left = h - row_idx;
  int max_gap = 6 - row_pos - rows_left;

  for (int gap = 0; gap <= max_gap; ++gap) {
    int actual_row = row_pos + gap;
    uint64_t new_bb =
        bitboard | (static_cast<uint64_t>(shape.row_bits[row_idx])
                    << (10 * actual_row));

    // Below mask: cells directly below this piece row.
    uint64_t new_below = below;
    if (row_idx == 0 && gap > 0) {
      // First row not on floor — below is at (gap - 1).
      new_below |= static_cast<uint64_t>(shape.row_bits[0])
                   << (10 * (gap - 1));
    }
    if (row_idx > 0) {
      // Below is at (row_pos - 1) before the gap.
      new_below |= static_cast<uint64_t>(shape.row_bits[row_idx])
                   << (10 * (row_pos - 1));
    }

    // Hurdles: gap rows between piece rows are hurdles.
    uint8_t new_hurdles = hurdles;
    if (row_idx > 0) {
      for (int j = 0; j < gap; ++j)
        new_hurdles |= 1 << (row_pos + j);
    }

    int new_first_y = (row_idx == 0) ? gap : first_y;

    gen_hurdled(shape, row_idx + 1, actual_row + 1, new_bb, new_below,
                new_hurdles, new_first_y);
  }
}

} // namespace

void pc_data_init() {
  std::call_once(g_init_flag, [] {
    for (const auto &shape : kShapes)
      gen_hurdled(shape, 0, 0, 0, 0, 0, 0);

    // Sort each bucket by width for early-exit optimization.
    for (auto &h : g_table.buckets)
      for (auto &p : h)
        for (auto &c : p)
          std::ranges::sort(c, {}, &PcPieceState::width);
  });
}

std::span<const PcPieceState> pc_piece_states(int height, int piece_idx,
                                              int cell_y) {
  auto &v = g_table.buckets[height - 1][piece_idx][cell_y];
  return {v.data(), v.size()};
}
