#pragma once

#include "piece.h"
#include <array>
#include <cstdint>
#include <sys/types.h>

class Board {
public:
  static constexpr int kWidth = 10;
  static constexpr int kVisibleHeight = 20;
  static constexpr int kTotalHeight = 40; // includes buffer zone above visible

  Board() = default;

  // Check if placing piece would collide with walls or filled cells.
  bool collides(const Piece &piece) const;

  // Place piece onto the board (permanently fill cells).
  void place(const Piece &piece);

  // Clear completed lines. Returns number of lines cleared.
  int clear_lines();

  // Add garbage rows from the bottom with a gap column.
  // Pushes existing rows up. If rows overflow top, they are lost.
  void add_garbage(uint count, uint gap_col);

  // Check if a specific cell is filled.
  // TODO: test bit in rows_[row]
  bool filled(uint col, uint row) const;

  // Get raw row data (for renderer).
  uint16_t row(uint r) const { return rows_[r]; }

private:
  // Each row is a 10-bit bitfield (bit 0 = col 0, bit 9 = col 9).
  // Row 0 = bottom of board.
  std::array<uint16_t, kTotalHeight> rows_{};
};
