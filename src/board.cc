#include "board.h"
#include <algorithm>
#include <cassert>

// Check if placing piece would collide with walls or filled cells.
bool Board::collides(const Piece &piece) const {
  return std::ranges::any_of(piece.cells_absolute(), [this](Vec2 cell) {
    return cell.x < 0 || cell.x >= kWidth ||
           cell.y < 0 || cell.y >= kTotalHeight ||
           filled(cell.x, cell.y);
  });
}

void Board::place(const Piece &piece) {
  for (auto [x, y] : piece.cells_absolute()) {
    rows_[y] |= (1 << x);
  }
}

int Board::clear_lines() {
  int count = 0;
  for (int i = 0; i < rows_.size(); i++) {
    if (rows_[i] == 0x3FF) {
      std::move(rows_.begin() + i + 1, rows_.end(), rows_.begin() + i);
      rows_.back() = 0;
      count++;
      i--; // recheck index due to shifting
    }
  }
  return count;
}

void Board::add_garbage(uint count, uint gap_col) {
  assert(count < kTotalHeight);
  assert(gap_col < kWidth);

  std::move_backward(rows_.begin(), rows_.end() - count, rows_.end());

  int16_t garbage_row = 0x3FF ^ (1 << gap_col);
  for (int i = 0; i < count; i++) {
    rows_[i] = garbage_row;
  }
}

bool Board::filled(uint col, uint row) const {
  return rows_[row] & (1 << col);
}
