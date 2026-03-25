#pragma once

#include "board.h"
#include "piece.h"
#include "vec2.h"
#include <array>
#include <optional>

// SRS kick offsets. Indexed by [from_rotation][test_index].
// The actual kick for a from->to transition is:
//   offset[from][i] - offset[to][i]

// JLSTZ offset data
inline constexpr std::array<std::array<Vec2, 5>, 4> kOffsetJLSTZ = {{
    {{{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}},      // North
    {{{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}}},    // East
    {{{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}},      // South
    {{{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}}}, // West
}};

// I offset data
inline constexpr std::array<std::array<Vec2, 5>, 4> kOffsetI = {{
    {{{0, 0}, {-1, 0}, {2, 0}, {-1, 0}, {2, 0}}},     // North
    {{{-1, 0}, {0, 0}, {0, 0}, {0, -1}, {0, 2}}},     // East
    {{{-1, -1}, {1, -1}, {-2, -1}, {1, 0}, {-2, 0}}}, // South
    {{{0, -1}, {0, -1}, {0, -1}, {0, 1}, {0, -2}}},   // West
}};

// SRS+ 180-degree kick tables.
// Indexed by [from_rotation][test_index].
// Source: https://tetris.wiki/SRS#180_rotation

// JLSTZ 180 kicks (y-up: positive y = up)
inline constexpr std::array<std::array<Vec2, 6>, 4> kKick180_JLSTZ = {{
    // North -> South
    {{{0, 0}, {1, 0}, {2, 0}, {1, -1}, {2, -1}, {-1, 0}}},
    // East -> West
    {{{0, 0}, {0, -1}, {0, -2}, {-1, -1}, {-1, -2}, {0, 1}}},
    // South -> North
    {{{0, 0}, {-1, 0}, {-2, 0}, {-1, 1}, {-2, 1}, {1, 0}}},
    // West -> East
    {{{0, 0}, {0, 1}, {0, 2}, {1, 1}, {1, 2}, {0, -1}}},
}};

// I 180 kicks (y-up: positive y = up)
inline constexpr std::array<std::array<Vec2, 6>, 4> kKick180_I = {{
    // North -> South
    {{{-1, 0}, {-2, 0}, {1, 0}, {2, 0}, {0, 1}, {0, 0}}},
    // East -> West
    {{{0, -1}, {0, -2}, {0, 1}, {0, 2}, {-1, 0}, {0, 0}}},
    // South -> North
    {{{1, 0}, {2, 0}, {-1, 0}, {-2, 0}, {0, -1}, {0, 0}}},
    // West -> East
    {{{0, 1}, {0, 2}, {0, -1}, {0, -2}, {1, 0}, {0, 0}}},
}};

// Try to rotate a piece on the given board.
// Returns the successfully kicked piece, or nullopt if all tests fail.
std::optional<Piece> try_rotate(const Board &board, const Piece &piece,
                                Rotation target);

inline Rotation rotate_cw(Rotation r) {
  return static_cast<Rotation>((static_cast<int>(r) + 1) % 4);
}
inline Rotation rotate_ccw(Rotation r) {
  return static_cast<Rotation>((static_cast<int>(r) + 3) % 4);
}
inline Rotation rotate_180(Rotation r) {
  return static_cast<Rotation>((static_cast<int>(r) + 2) % 4);
}
