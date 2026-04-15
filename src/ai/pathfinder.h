#pragma once

#include "board_bitset.h"
#include "command.h"
#include "placement.h"
#include <vector>

// Result of pathfinding — the shortest input sequence from spawn to target.
struct InputSequence {
  int length = 0;                 // number of inputs (for PQ ordering in search)
  // TODO: store actual GameInput list for visualization/replay
};

// BFS from spawn position to target placement.
// Recovers the shortest input sequence (shift, rotate, soft-drop, hard-drop)
// that reaches the target.  Used to compute input count for beam search move
// ordering (prefer simpler placements at equal eval score).
//
// Algorithm (from fusion/src/pathfinder.rs):
//   1. Build CollisionMap for the piece type on the board.
//   2. BFS state: searched[col][rotation] = u64 bitboard of visited Y positions.
//   3. Seed queue with spawn position (spawn_col, spawn_row, North, 0 inputs).
//   4. For each state in queue:
//      a. Hard-drop check: if col == target.x and drop_y == target.y and
//         rotation matches → found, return input count.
//      b. Expand: rotate CW/CCW/180 (with SRS kicks), shift left/right,
//         soft-drop — each costs 1 input.
//      c. DAS finesse: slide to wall in one input (optional).
//   5. Return InputSequence with length, or length=INT_MAX if unreachable.
InputSequence find_inputs(const BoardBitset &board, const Placement &target);

// PQ search from (start_x, start_y, start_rot) to target placement.
// Uses Board::collides directly (same collision as game engine).
// Returns the sequence of GameInputs (including final HardDrop) to reach
// target. Empty vector if unreachable.
std::vector<GameInput> find_path(const Board &board, const Placement &target,
                                 int start_x, int start_y,
                                 Rotation start_rot);
