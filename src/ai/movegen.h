#pragma once

#include "board_bitset.h"
#include "placement.h"
#include "engine/piece.h"
#include <array>
#include <cstdint>

// Stack-allocated move buffer
struct MoveBuffer {
  static constexpr int kMaxMoves = 256;
  std::array<Placement, kMaxMoves> moves{};
  int count = 0;

  void push(Placement m) {
    if (count < kMaxMoves)
      moves[count++] = m;
  }

  void clear() { count = 0; }

  Placement *begin() { return moves.data(); }
  Placement *end() { return moves.data() + count; }
  const Placement *begin() const { return moves.data(); }
  const Placement *end() const { return moves.data() + count; }
};

// Precomputed collision map for a piece type on a board.
// For each (column, rotation), stores a u64 bitboard where bit b is set if
// placing the piece BB-corner at (col, y=b-kYShift) in that rotation would
// collide with an occupied cell or go out of bounds.
//
// X is shifted by kXShift to allow negative BB-corner x values (e.g., I-East
// at x=-2 places cells at column 0). Array index = col + kXShift.
struct CollisionMap {
  static constexpr int kXShift = 3;
  static constexpr int kXWidth = 13; // x from -3 to 9
  static constexpr int kYShift = 3;  // bit b represents y = b - kYShift
  static constexpr int kMaxBit = kYShift + 40; // bits above this are blocked

  uint64_t data[kXWidth][4]; // [col + kXShift][rotation]

  CollisionMap() = default;
  CollisionMap(const BoardBitset &board, PieceType type);

  uint64_t get(int col, Rotation rot) const {
    int idx = col + kXShift;
    if (idx < 0 || idx >= kXWidth)
      return ~uint64_t(0);
    return data[idx][static_cast<int>(rot)];
  }
};

// Generate all legal placements for a piece type on a board.
//
// Uses bitboard flood-fill:
//   1. Build CollisionMap from board. For each (col, rotation), OR together
//      column bitboards shifted by piece cell offsets.
//   2. Initialize to_search[col][rot] with surface positions reachable from
//      spawn by gravity.
//   3. Flood-fill loop:
//      - Soft drop:  (to_search >> 1) & ~collision  (propagate downward)
//      - Hard drop:  to_search & ((collision << 1) | 1)  (grounded = move)
//      - Lateral:    copy to_search[x] → to_search[x±1] where not searched
//      - Rotation:   apply SRS kick offsets, shift bitboards, mask against
//                    destination collision map
//   4. Detect T-spins/allspins during rotation expansion.
//   5. Emit moves from grounded bitboards.
//
// Performance: TODO benchmark
//
// When sonic_only is true, only hard-drop landing positions are generated
// (no intermediate soft-drop tucks). This drastically reduces the move count.
void generate_moves(const BoardBitset &board, MoveBuffer &out, PieceType type,
                    bool sonic_only = false);
