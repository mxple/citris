#pragma once

#include "piece.h"
#include <random>
#include <vector>

// Hold-based queue shuffling.
//
// A bag B is "hold-equivalent" to a play order A when, starting with B as
// the upcoming pieces (current = B[0], hold empty), the player can use
// hold operations (one swap allowed per spawn) to actually play A in order.
//
// The puzzle generator uses this to disguise the solution queue: the player
// sees a permuted bag and has to figure out the hold operations that
// recover the intended play sequence.
//
// Reference: get_shuffled_holdable_queue (script7.js:824).

// Simulate `bag` against `target` under standard hold rules. Returns true
// iff the entire `target` sequence can be played out of `bag`.
bool is_hold_equivalent(const std::vector<PieceType> &bag,
                        const std::vector<PieceType> &target);

// Returns a permutation of `queue` that is hold-equivalent to `queue`. When
// multiple valid permutations exist, picks one uniformly at random. Returns
// the input unchanged when `queue.size()` is outside [2, 7] or no
// permutation other than the identity satisfies the constraints (the
// identity is always valid for queues of distinct pieces).
std::vector<PieceType>
shuffle_hold_equivalent(const std::vector<PieceType> &queue,
                        std::mt19937 &rng);
