#include "board.h"
#include <algorithm>
#include <cassert>

// Check if placing piece would collide with walls or filled cells.
bool Board::collides(const Piece &piece) const {
  return std::ranges::any_of(piece.cells_absolute(), [this](Vec2 cell) {
    return filled(cell.x, cell.y);
  });
}

void Board::place(const Piece &piece) {
  for (auto [x, y] : piece.cells_absolute()) {
    rows_[y] |= (1 << x);
    colors_[y][x] = piece.type;
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

SpinKind Board::detect_spin(const Piece &piece) const {
  if (piece.type == PieceType::T) {
    // T-spin: 3-corner rule
    bool a = filled(piece.x,     piece.y + 2); // top-left
    bool b = filled(piece.x + 2, piece.y + 2); // top-right
    bool c = filled(piece.x,     piece.y);     // bottom-left
    bool d = filled(piece.x + 2, piece.y);     // bottom-right

    int count = a + b + c + d;
    if (count < 3) return SpinKind::None;

    // Front corners face the flat side of the T.
    bool front;
    switch (piece.rotation) {
      case Rotation::North: front = a && b; break;
      case Rotation::East:  front = b && d; break;
      case Rotation::South: front = c && d; break;
      case Rotation::West:  front = a && c; break;
    }
    return front ? SpinKind::TSpin : SpinKind::Mini;
  }

  // Allspin: immobile rule (can't move in any cardinal direction)
  Piece test = piece;
  test.x -= 1;
  if (!collides(test)) return SpinKind::None;
  test.x += 2;
  if (!collides(test)) return SpinKind::None;
  test.x -= 1; test.y -= 1;
  if (!collides(test)) return SpinKind::None;
  test.y += 2;
  if (!collides(test)) return SpinKind::None;
  return SpinKind::AllSpin;
}

bool Board::filled(int col, int row) const {
  return col < 0 || col >= kWidth || row < 0 || row >= kTotalHeight ||
         rows_[row] & (1 << col);
}
