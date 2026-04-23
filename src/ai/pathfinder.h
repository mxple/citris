#pragma once

#include "board_bitset.h"
#include "command.h"
#include "placement.h"
#include <vector>

// Pathfinder overview
// -------------------
// Both entry points below are thin wrappers around one templated BFS in
// pathfinder.cc (`pathfind_impl`). The BFS walks (x, rotation, y) states
// through an alphabet of shifts, soft/sonic drops, and SRS-kicked rotations,
// and terminates when a hard-drop from the current state lands on the
// target's canonical cells — subject to the match_spin gate described below.
//
// The two entry points differ along two axes:
//   * Collision source. count_inputs works over a precomputed CollisionMap
//     (O(1) bit-test) built from a BoardBitset; find_path works over a live
//     engine Board via Board::collides.
//   * Output shape. count_inputs returns only an input count (no vector
//     allocation on the hot path); find_path returns the full GameInput
//     sequence — including the player-style macro alphabet (LLeft / RRight
//     / SonicDrop) that the count-only path omits.
//
// match_spin
// ----------
// When match_spin is true, termination is gated against the engine's
// detect_spin semantics (engine runs detect_spin only if the lock's last
// input was a rotation):
//   - target.spin != None: require via_rot. Position is already verified
//     by movegen's label — cell-match uniquely determines Mini vs TSpin vs
//     AllSpin because the engine's classifier is purely position-based.
//   - target.spin == None: allow either arrival, EXCEPT for T pieces where
//     arriving via rotation at a 3-corner position would be upgraded to
//     Mini/TSpin by the engine. For non-T, 4-way immobility implies the
//     position was only reachable via rotation during flood-fill, so
//     movegen would have labeled it AllSpin — it cannot be a None target.
//     The T 3-corner case is checked at termination via is_filled.
//
// Fuzzy mode (false) terminates on any cell-matching state regardless of
// how it was arrived at. Fine for cost estimation, wrong for replay of
// spin-tagged placements.
//
// allow_180
// ---------
// When false, Rotate180 (and its kick table) is not explored. Placements
// that rely on 180-only kicks may become unreachable.
//
// sonic_only
// ----------
// When true, the atomic SoftDrop edge (y -> y-1) is suppressed; vertical
// motion is only available via SonicDrop (teleport to ghost). Mirrors
// movegen's sonic_only flag — useful when soft-drop tucks should not be
// considered (e.g. evaluating "can this be reached without tucking?").

int count_inputs(const BoardBitset &board, const Placement &target,
                          bool allow_180 = true, bool match_spin = false,
                          bool sonic_only = false);

std::vector<GameInput> find_path(const Board &board, const Placement &target,
                                 int start_x, int start_y,
                                 Rotation start_rot, bool allow_180 = true,
                                 bool match_spin = false,
                                 bool sonic_only = false);
