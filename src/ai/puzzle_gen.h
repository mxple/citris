#pragma once

#include "ai/placement.h"
#include "engine/board.h"
#include "engine/piece.h"
#include <optional>
#include <random>
#include <vector>

// Reverse-construction puzzle generator — ported from the `downstack-practice`
// reference.
//
// Workflow (identical across modes):
//   1. Generate the final map: random garbage mesa with a mode-specific
//      keyhole stamped in, plus optional cheese rows.
//   2. Reverse-construct N pieces on top, each one "uncarving" itself from
//      regular garbage. Every cell is uncarvable — nothing is protected —
//      so reverse construction tends to uncarve the overhang cap, hiding
//      the setup from the initial board. The player has to REBUILD the
//      setup through downstacking.
//   3. Reverse placement order → playable queue with reserved pieces at tail.
//
// The mode only determines step 1's keyhole shape + cheese budget. Steps 2-3
// and all predicates (smoothness, stability, hole count) are shared.
//
// References:
//   TSD:              script7.js (1-to-1 port, currently implemented)
//   DTCannon/Cspin/Fractal: script6.js (ports in progress)

enum class PuzzleMode {
  TSD,       // script7.js — TSD keyhole (wall + overhang + slot + shoulder)
  DTCannon,  // script6.js mode='dt'     — 7-row keyhole for TSD + TST
  Cspin,     // script6.js mode='cspin'  — 6-row keyhole for TSD + TST
  Fractal,   // script6.js mode='fractal'— two-hole structure for 2x TSD
};

struct PuzzleRequest {
  PuzzleMode mode = PuzzleMode::TSD;
  int num_pieces = 5;           // downstack pieces (excludes reserved)
  bool allow_skims = true;      // interleave cheese rows between placements
  bool smooth_surface = true;   // reference is_smooth + sorted outlier clip
  int unique_pieces = 1;        // max copies of each piece type (1 or 2)
  int max_non_cheese_holes = 0; // reference mdhole_ind; 0 = strict
  int garbage_below = 0;        // cheese lines beneath keyhole (exact count)
  int max_height = 17;          // reject stacks taller than this
  int max_attempts = 200;       // outer retry budget
  // Pieces appended to the queue tail in order. They are excluded from the
  // reverse-construction candidate pool so the player is guaranteed to
  // receive them as the final pieces. Default: {T} (TSD). {T, I} = tsdquad.
  std::vector<PieceType> reserved = {PieceType::T};
};

struct PuzzleResult {
  Board board;

  // Queue with `num_pieces` downstack pieces + reserved pieces appended,
  // then permuted by a hold-equivalent shuffle. The player must use hold
  // operations to recover the play order that solves the puzzle.
  std::vector<PieceType> queue;

  // Placements for the downstack portion (size == num_pieces) in PLAY
  // order — independent of `queue`'s permutation. solution[i] is the
  // i-th piece the player should place. Reserved pieces (e.g. final T
  // for TSD) have no placement here — the player solves them.
  std::vector<Placement> solution;
};

std::optional<PuzzleResult> generate_puzzle(const PuzzleRequest &req,
                                            std::mt19937 &rng);
