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
// Origin = bottom-left of bounding box.
// Top of bounding box should be at row 20 (just above visible rows 0-19).
// 3-tall (T,S,Z,J,L): y = 18  (rows 18,19,20)
// 4-tall (I):         y = 17  (rows 17,18,19,20)
// 2-tall (O):         y = 19  (rows 19,20)
inline constexpr Vec2 spawn_position(PieceType type) {
  switch (type) {
  case PieceType::I:
    return {3, 17};
  case PieceType::O:
    return {4, 19};
  default:
    return {3, 18};
  }
}

// clang-format off
template <>
struct fmt::formatter<PieceType> : fmt::formatter<char> {
    constexpr auto parse(fmt::format_parse_context& ctx) {
        return ctx.begin();
    }
    template <typename FormatContext>
    auto format(PieceType type, FormatContext& ctx) const {
        char c = '?';
        switch (type) {
            case PieceType::I: c = 'I'; break;
            case PieceType::O: c = 'O'; break;
            case PieceType::T: c = 'T'; break;
            case PieceType::S: c = 'S'; break;
            case PieceType::Z: c = 'Z'; break;
            case PieceType::J: c = 'J'; break;
            case PieceType::L: c = 'L'; break;
        }
        return fmt::formatter<char>::format(c, ctx);
    }
};

template <>
struct fmt::formatter<Rotation> : fmt::formatter<char> {
      constexpr auto parse(fmt::format_parse_context& ctx) {
        return ctx.begin();
    }
    template <typename FormatContext>
    auto format(Rotation r, FormatContext& ctx) const {
        char c = '?';
        switch (r) {
            case Rotation::North: c = 'N'; break;
            case Rotation::East: c = 'E'; break;
            case Rotation::South: c = 'S'; break;
            case Rotation::West: c = 'W'; break;
        }
        return fmt::formatter<char>::format(c, ctx);
    }
};

// clang-format on
