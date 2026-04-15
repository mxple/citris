#include "piece.h"

Piece::Piece(PieceType type) : type(type) {
  auto [xx, yy] = spawn_position(type);
  x = xx, y = yy;
}

Piece::Piece(PieceType type, Rotation rotation, int x, int y)
    : type(type), rotation(rotation), x(x), y(y) {}

std::array<Vec2, 4> Piece::cells_relative() const {
  return kPieceCells[static_cast<int>(type)][static_cast<int>(rotation)];
}

std::array<Vec2, 4> Piece::cells_absolute() const {
  auto relative = this->cells_relative();
  std::array<Vec2, 4> absolute;
  for (int i = 0; i < 4; ++i) {
    absolute[i] = {relative[i].x + this->x, relative[i].y + this->y};
  }
  return absolute;
}

