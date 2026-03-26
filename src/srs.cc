#include "srs.h"

std::optional<Piece> try_rotate(const Board &board, const Piece &piece,
                                Rotation target) {
  if (piece.type == PieceType::O)
    return std::nullopt;

  int from = static_cast<int>(piece.rotation);
  int to = static_cast<int>(target);
  bool is_i = (piece.type == PieceType::I);
  int diff = (to - from + 4) % 4;

  if (diff == 2) {
    const auto &table = is_i ? kKick180_I : kKick180_JLSTZ;
    for (auto &kick : table[from]) {
      Piece test = {piece.type, target, piece.x + kick.x, piece.y + kick.y};
      if (!board.collides(test))
        return test;
    }
  } else if (diff == 1) {
    const auto &table = is_i ? kKickCW_I : kKickCW_JLSTZ;
    for (auto &kick : table[from]) {
      Piece test = {piece.type, target, piece.x + kick.x, piece.y + kick.y};
      if (!board.collides(test))
        return test;
    }
  } else if (diff == 3) {
    const auto &table = is_i ? kKickCCW_I : kKickCCW_JLSTZ;
    for (auto &kick : table[from]) {
      Piece test = {piece.type, target, piece.x + kick.x, piece.y + kick.y};
      if (!board.collides(test))
        return test;
    }
  }

  return std::nullopt;
}
