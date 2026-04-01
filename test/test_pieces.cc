#include "piece.h"
#include <cstdio>

const char *piece_name(PieceType t) {
  switch (t) {
  case PieceType::I:
    return "I";
  case PieceType::O:
    return "O";
  case PieceType::T:
    return "T";
  case PieceType::S:
    return "S";
  case PieceType::Z:
    return "Z";
  case PieceType::J:
    return "J";
  case PieceType::L:
    return "L";
  }
  return "?";
}

const char *rot_name(Rotation r) {
  switch (r) {
  case Rotation::North:
    return "N";
  case Rotation::East:
    return "E";
  case Rotation::South:
    return "S";
  case Rotation::West:
    return "W";
  }
  return "?";
}

int main() {
  for (int t = 0; t < kPieceTypeN; ++t) {
    auto type = static_cast<PieceType>(t);
    int size = (type == PieceType::I) ? 4 : (type == PieceType::O) ? 2 : 3;

    printf("=== %s ===\n", piece_name(type));

    for (int r = 0; r < 4; ++r) {
      Piece p{type, static_cast<Rotation>(r), 0, 0};
      auto cells = p.cells_relative();

      char grid[4][4];
      for (int row = 0; row < size; ++row)
        for (int col = 0; col < size; ++col)
          grid[row][col] = '.';

      for (auto [c, row] : cells)
        grid[row][c] = '#';

      printf("  %s:\n", rot_name(static_cast<Rotation>(r)));
      // print top-down (highest row first)
      for (int row = size - 1; row >= 0; --row) {
        printf("    ");
        for (int col = 0; col < size; ++col)
          printf("%c ", grid[row][col]);
        printf("\n");
      }
    }
    printf("\n");
  }
}
