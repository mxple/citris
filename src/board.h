#pragma once

#include "attack.h"
#include "piece.h"
#include <array>
#include <cstdint>
#include <optional>
#include <sys/types.h>

class Board {
public:
  static constexpr int kWidth = 10;
  static constexpr int kVisibleHeight = 20;
  static constexpr int kTotalHeight = 40;

  Board() = default;

  bool collides(const Piece &piece) const;
  void place(const Piece &piece);
  int clear_lines();
  void add_garbage(uint count, uint gap_col);
  SpinKind detect_spin(const Piece &piece) const;
  bool filled(int col, int row) const;
  uint16_t row(uint r) const { return rows_[r]; }

  // Color of a placed cell (for rendering). Only valid if filled(col, row).
  PieceType cell_color(int col, int row) const { return colors_[row][col]; }

private:
  std::array<uint16_t, kTotalHeight> rows_{};
  // Per-cell color (PieceType). Only meaningful where the corresponding bit is set.
  std::array<std::array<PieceType, kWidth>, kTotalHeight> colors_{};
};
