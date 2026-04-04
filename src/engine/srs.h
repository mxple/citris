#pragma once

#include "board.h"
#include "piece.h"
#include "vec2.h"
#include <array>
#include <optional>

// Direct SRS wall kick tables (y-up, from tetris.wiki/Super_Rotation_System).
// Indexed by [from_rotation][test_index]. 5 tests for 90, 6 for 180.

// --- JLSTZ 90 kicks ---

// CW: from → (from+1)%4
inline constexpr std::array<std::array<Vec2, 5>, 4> kKickCW_JLSTZ = {{
    {{{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}}}, // N→E
    {{{0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2}}},     // E→S
    {{{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}}},    // S→W
    {{{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}}},  // W→N
}};

// CCW: from → (from+3)%4
inline constexpr std::array<std::array<Vec2, 5>, 4> kKickCCW_JLSTZ = {{
    {{{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}}},    // N→W
    {{{0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2}}},     // E→N
    {{{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}}}, // S→E
    {{{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}}},  // W→S
}};

// --- I 90 kicks ---

inline constexpr std::array<std::array<Vec2, 5>, 4> kKickCW_I = {{
    {{{0, 0}, {-2, 0}, {1, 0}, {-2, -1}, {1, 2}}}, // N→E
    {{{0, 0}, {-1, 0}, {2, 0}, {-1, 2}, {2, -1}}}, // E→S
    {{{0, 0}, {2, 0}, {-1, 0}, {2, 1}, {-1, -2}}}, // S→W
    {{{0, 0}, {1, 0}, {-2, 0}, {1, -2}, {-2, 1}}}, // W→N
}};

inline constexpr std::array<std::array<Vec2, 5>, 4> kKickCCW_I = {{
    {{{0, 0}, {-1, 0}, {2, 0}, {-1, 2}, {2, -1}}}, // N→W
    {{{0, 0}, {2, 0}, {-1, 0}, {2, 1}, {-1, -2}}}, // E→N
    {{{0, 0}, {1, 0}, {-2, 0}, {1, -2}, {-2, 1}}}, // S→E
    {{{0, 0}, {-2, 0}, {1, 0}, {-2, -1}, {1, 2}}}, // W→S
}};

// --- 180 kicks (SRS+) ---

// inline constexpr std::array<std::array<Vec2, 6>, 4> kKick180_JLSTZ = {{
//     {{{0, 0}, {1, 0}, {2, 0}, {1, -1}, {2, -1}, {-1, 0}}},    // N→S
//     {{{0, 0}, {0, -1}, {0, -2}, {-1, -1}, {-1, -2}, {0, 1}}}, // E→W
//     {{{0, 0}, {-1, 0}, {-2, 0}, {-1, 1}, {-2, 1}, {1, 0}}},   // S→N
//     {{{0, 0}, {0, 1}, {0, 2}, {1, 1}, {1, 2}, {0, -1}}},      // W→E
// }};

inline constexpr std::array<std::array<Vec2, 6>, 4> kKick180_JLSTZ = {{
    {{{0, 0}, {0, 1}, {1, 1}, {-1, 1}, {1, 0}, {-1, 0}}},    // N→S
    {{{0, 0}, {0, -1}, {-1, -1}, {1, -1}, {-1, 0}, {1, 0}}}, // E→W
    {{{0, 0}, {1, 0}, {1, 2}, {1, 1}, {0, 2}, {0, 1}}},   // S→N
    {{{0, 0}, {-1, 0}, {-1, 2}, {-1, 1}, {0, 2}, {0, 1}}},      // W→E
}};

inline constexpr std::array<std::array<Vec2, 6>, 4> kKick180_I = {{
    {{{0, 0}, {-1, 0}, {-2, 0}, {1, 0}, {2, 0}, {0, 1}}},  // N→S
    {{{0, 0}, {0, -1}, {0, -2}, {0, 1}, {0, 2}, {-1, 0}}}, // E→W
    {{{0, 0}, {1, 0}, {2, 0}, {-1, 0}, {-2, 0}, {0, -1}}}, // S→N
    {{{0, 0}, {0, 1}, {0, 2}, {0, -1}, {0, -2}, {1, 0}}},  // W→E
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
