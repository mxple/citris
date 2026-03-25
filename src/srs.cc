#include "srs.h"

static bool is_180(Rotation from, Rotation to) {
  return ((static_cast<int>(from) + 2) % 4) == static_cast<int>(to);
}

std::optional<Piece> try_rotate(const Board &board, const Piece &piece,
                                Rotation target) {
  if (piece.type == PieceType::O)
    return std::nullopt;

  int from = static_cast<int>(piece.rotation);

  if (is_180(piece.rotation, target)) {
    const auto &table =
        (piece.type == PieceType::I) ? kKick180_I : kKick180_JLSTZ;
    for (auto &kick : table[from]) {
      Piece test = {piece.type, target, piece.x + kick.x, piece.y + kick.y};
      if (!board.collides(test))
        return test;
    }
    return std::nullopt;
  }

  int to = static_cast<int>(target);
  const auto &table =
      (piece.type == PieceType::I) ? kOffsetI : kOffsetJLSTZ;
  for (int i = 0; i < 5; i++) {
    Vec2 kick = table[from][i] - table[to][i];
    Piece test = {piece.type, target, piece.x + kick.x, piece.y + kick.y};
    if (!board.collides(test))
      return test;
  }
  return std::nullopt;
}
