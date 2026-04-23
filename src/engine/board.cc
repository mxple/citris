#include "board.h"
#include <algorithm>
#include <cassert>

bool Board::collides(const Piece &piece) const {
  return std::ranges::any_of(piece.cells_absolute(), [this](Vec2 cell) {
    return filled(cell.x, cell.y);
  });
}

void Board::place(const Piece &piece) {
  auto color = piece_to_cell_color(piece.type);
  for (auto [x, y] : piece.cells_absolute()) {
    cells_[y][x] = color;
  }
}

int Board::clear_lines() {
  int count = 0;
  for (int i = 0; i < kTotalHeight; i++) {
    if (row_full(i)) {
      std::move(cells_.begin() + i + 1, cells_.end(), cells_.begin() + i);
      cells_.back().fill(CellColor::Empty);
      count++;
      i--;
    }
  }
  return count;
}

void Board::add_garbage(unsigned count, unsigned gap_col) {
  assert(count < kTotalHeight);
  assert(gap_col < kWidth);

  std::move_backward(cells_.begin(), cells_.end() - count, cells_.end());

  for (unsigned i = 0; i < count; i++) {
    cells_[i].fill(CellColor::Garbage);
    cells_[i][gap_col] = CellColor::Empty;
  }
}

SpinKind Board::detect_spin(const Piece &piece) const {
  if (piece.type == PieceType::T) {
    bool a = filled(piece.x, piece.y + 2);
    bool b = filled(piece.x + 2, piece.y + 2);
    bool c = filled(piece.x, piece.y);
    bool d = filled(piece.x + 2, piece.y);

    int count = a + b + c + d;
    if (count < 3)
      return SpinKind::None;

    bool front;
    switch (piece.rotation) {
    case Rotation::North:
      front = a && b;
      break;
    case Rotation::East:
      front = b && d;
      break;
    case Rotation::South:
      front = c && d;
      break;
    case Rotation::West:
      front = a && c;
      break;
    }
    return front ? SpinKind::TSpin : SpinKind::Mini;
  }

  Piece test = piece;
  test.x -= 1;
  if (!collides(test))
    return SpinKind::None;
  test.x += 2;
  if (!collides(test))
    return SpinKind::None;
  test.x -= 1;
  test.y -= 1;
  if (!collides(test))
    return SpinKind::None;
  test.y += 2;
  if (!collides(test))
    return SpinKind::None;
  return SpinKind::AllSpin;
}

bool Board::filled(int col, int row) const {
  return col < 0 || col >= kWidth || row < 0 || row >= kTotalHeight ||
         cells_[row][col] != CellColor::Empty;
}

bool Board::is_empty() const {
  for (int row = 0; row < kTotalHeight; ++row)
    for (int col = 0; col < kWidth; ++col)
      if (cells_[row][col] != CellColor::Empty)
        return false;
  return true;
}

bool Board::row_full(int row) const {
  return std::ranges::none_of(
      cells_[row], [](CellColor c) { return c == CellColor::Empty; });
}
