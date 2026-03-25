#include "piece.h"

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

constexpr Vec2 spawn_position(PieceType type) {
  // Origin = bottom-left of bounding box.
  // Top of bounding box should be at row 20 (just above visible rows 0-19).
  // 3-tall (T,S,Z,J,L): y = 18  (rows 18,19,20)
  // 4-tall (I):         y = 17  (rows 17,18,19,20)
  // 2-tall (O):         y = 19  (rows 19,20)
  switch (type) {
    case PieceType::I: return {3, 17};
    case PieceType::O: return {4, 19};
    default:           return {3, 18}; // T, S, Z, J, L
  }
}
