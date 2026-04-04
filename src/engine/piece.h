#pragma once

#include "vec2.h"
#include <array>

enum class PieceType { I, O, T, S, Z, J, L };

const int kPieceTypeN = 7;

enum class Rotation { North, East, South, West };

struct Piece {
  PieceType type;
  Rotation rotation = Rotation::North;
  int x = 0; // column of bounding box bottom-left
  int y = 0; // row of bounding box bottom-left (0 = :oard bottom)

  Piece(PieceType type);
  Piece(PieceType type, Rotation rotation, int x, int y);

  // Return the 4 cell offsets {col, row} for this piece in its current
  // rotation. Uses kPieceCells lookup.
  std::array<Vec2, 4> cells_relative() const;
  std::array<Vec2, 4> cells_absolute() const;
};

using RotationStates = std::array<std::array<Vec2, 4>, 4>;

// Cell offsets for each (PieceType, Rotation) pair.
// kPieceCells[type][rotation][i] = {col_offset, row_offset}
// Row offsets use board convention: positive = up (row 0 = bottom).
// Origin is the bottom-left corner of the bounding box.
// Absolute position: board_col = piece.x + col_offset
//                    board_row = piece.y + row_offset
inline constexpr std::array<RotationStates, kPieceTypeN> kPieceCells = {
    {// I (4x4 bounding box)
     {{
         {{{0, 2}, {1, 2}, {2, 2}, {3, 2}}}, // North
         {{{2, 0}, {2, 1}, {2, 2}, {2, 3}}}, // East
         {{{0, 1}, {1, 1}, {2, 1}, {3, 1}}}, // South
         {{{1, 0}, {1, 1}, {1, 2}, {1, 3}}}  // West
     }},
     // O (2x2 bounding box)
     {{{{{0, 0}, {1, 0}, {0, 1}, {1, 1}}},
       {{{0, 0}, {1, 0}, {0, 1}, {1, 1}}},
       {{{0, 0}, {1, 0}, {0, 1}, {1, 1}}},
       {{{0, 0}, {1, 0}, {0, 1}, {1, 1}}}}},
     // T (3x3 bounding box)
     {{
         {{{1, 2}, {0, 1}, {1, 1}, {2, 1}}}, // North
         {{{1, 2}, {1, 1}, {2, 1}, {1, 0}}}, // East
         {{{0, 1}, {1, 1}, {2, 1}, {1, 0}}}, // South
         {{{1, 2}, {0, 1}, {1, 1}, {1, 0}}}  // West
     }},
     // S (3x3 bounding box)
     {{
         {{{1, 2}, {2, 2}, {0, 1}, {1, 1}}}, // North
         {{{1, 2}, {1, 1}, {2, 1}, {2, 0}}}, // East
         {{{1, 1}, {2, 1}, {0, 0}, {1, 0}}}, // South
         {{{0, 2}, {0, 1}, {1, 1}, {1, 0}}}  // West
     }},
     // Z (3x3 bounding box)
     {{
         {{{0, 2}, {1, 2}, {1, 1}, {2, 1}}}, // North
         {{{2, 2}, {1, 1}, {2, 1}, {1, 0}}}, // East
         {{{0, 1}, {1, 1}, {1, 0}, {2, 0}}}, // South
         {{{1, 2}, {0, 1}, {1, 1}, {0, 0}}}  // West
     }},
     // J (3x3 bounding box)
     {{
         {{{0, 2}, {0, 1}, {1, 1}, {2, 1}}}, // North
         {{{1, 2}, {2, 2}, {1, 1}, {1, 0}}}, // East
         {{{0, 1}, {1, 1}, {2, 1}, {2, 0}}}, // South
         {{{1, 2}, {1, 1}, {0, 0}, {1, 0}}}  // West
     }},
     // L (3x3 bounding box)
     {{
         {{{2, 2}, {0, 1}, {1, 1}, {2, 1}}}, // North
         {{{1, 2}, {1, 1}, {1, 0}, {2, 0}}}, // East
         {{{0, 1}, {1, 1}, {2, 1}, {0, 0}}}, // South
         {{{0, 2}, {1, 2}, {1, 1}, {1, 0}}}  // West
     }}}};

// Spawn position for each piece type (col, row)
constexpr Vec2 spawn_position(PieceType type);
