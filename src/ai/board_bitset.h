#pragma once

#include "engine/board.h"
#include "engine/piece.h"
#include <array>
#include <bit>
#include <cstdint>

// Bitboard representation (no CellColor, just occupancy).
//
// Dual representation: row bitmasks for collision testing,
// column bitmasks for O(1) height queries.
//
// Coordinate convention matches engine: (0,0) = bottom-left, y-up.
struct BoardBitset {
  static constexpr int kWidth = 10;
  static constexpr int kHeight = 40;

  std::array<uint16_t, kHeight> rows{}; // rows[y] bit x set -> (x,y) occupied
  std::array<uint64_t, kWidth> cols{};  // cols[x] bit y set -> (x,y) occupied

  // Convert from engine Board (read cell_color != Empty for each cell).
  static BoardBitset from_board(const Board &board) {
    BoardBitset bb;
    for (int y = 0; y < Board::kTotalHeight; y++) {
      for (int x = 0; x < Board::kWidth; x++) {
        if (board.filled(x, y)) {
          bb.rows[y] |= 1 << x;
          bb.cols[x] |= 1 << y;
        }
      }
    }
    return bb;
  }

  bool occupied(int x, int y) const { return (rows[y] >> x) & 1; }

  // Height of column x = index of highest occupied cell + 1 (0 if empty).
  int column_height(int x) const {
    if (cols[x] == 0)
      return 0;
    return 64 - std::countl_zero(cols[x]);
  }

  // Tallest column's height
  int max_height() const {
    int h = 0;
    for (int x = 0; x < kWidth; x++)
      h = std::max(h, column_height(x));
    return h;
  }

  bool row_full(int y) const {
    return (rows[y] & 0x3FF) == 0x3FF; // lower 10 bits all set
  }

  // Place piece cells onto the board (updates both rows and cols).
  void place(PieceType type, Rotation rot, int px, int py) {
    const auto &cells =
        kPieceCells[static_cast<int>(type)][static_cast<int>(rot)];
    for (auto &c : cells) {
      int x = px + c.x;
      int y = py + c.y;
      rows[y] |= uint16_t(1) << x;
      cols[x] |= uint64_t(1) << y;
    }
  }

  // Clears full lines and returns number of lines cleared.
  int clear_lines() {
    int cleared = 0;
    int write = 0;
    for (int read = 0; read < kHeight; ++read) {
      if (!row_full(read)) {
        rows[write++] = rows[read];
      } else {
        ++cleared;
      }
    }
    for (int y = write; y < kHeight; ++y)
      rows[y] = 0;
    if (cleared)
      rebuild_cols();
    return cleared;
  }

private:
  void rebuild_cols() {
    cols.fill(0);
    for (int y = 0; y < kHeight; ++y) {
      uint16_t r = rows[y];
      while (r) {
        int x = std::countr_zero(static_cast<unsigned>(r));
        cols[x] |= uint64_t(1) << y;
        r &= r - 1; // clear lowest set bit
      }
    }
  }
};
