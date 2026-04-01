#pragma once

#include "attack.h"
#include "piece.h"
#include <array>
#include <cstdint>
#include <sys/types.h>

enum class CellColor : uint8_t { Empty, I, O, T, S, Z, J, L, Garbage };

inline CellColor piece_to_cell_color(PieceType type) {
  // CellColor values mirror PieceType offset by 1 (Empty = 0).
  return static_cast<CellColor>(static_cast<int>(type) + 1);
}

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

  CellColor cell_color(int col, int row) const { return cells_[row][col]; }
  bool is_empty() const;

private:
  bool row_full(int row) const;

  std::array<std::array<CellColor, kWidth>, kTotalHeight> cells_{};
};
