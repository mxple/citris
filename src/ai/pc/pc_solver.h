#pragma once

// Two-phase perfect clear solver. Port of MinusKelvin's pcf.
//
// Phase 1: find piece-placement combinations that fill the board (ignoring
//          queue order), using leftmost-empty-column search with vertical
//          parity and cyclic dependency pruning.
// Phase 2: for each combination, find a placement order that respects
//          the queue + hold, physical support, and SRS+ reachability.

#include "beam_search.h" // BeamInput
#include "board_bitset.h"
#include "placement.h"
#include <memory>
#include <optional>
#include <vector>

struct PcConfig {
  int max_pieces = 11;   // max queue depth to consider
  int height_cap = 6;    // search up to this many rows (max 6)
  int max_solutions = 1; // stop after finding this many
  bool use_hold = true;
};

struct PcResult {
  bool found = false;
  std::vector<Placement> solution;
  bool hold_used = false;
};

// Find a perfect clear from the given board state and piece queue.
// Uses a two-phase approach: first finds piece combinations that fill
// the board, then checks each for a valid ordering given the queue.
// Synchronous — blocks until complete.
PcResult find_perfect_clear(const BoardBitset &board,
                            const std::vector<PieceType> &queue,
                            std::optional<PieceType> hold,
                            const PcConfig &config = {});

// ---------------------------------------------------------------------------
// Async PC search task
// ---------------------------------------------------------------------------

// Async handle for a running PC search.
// Note: cancel() blocks until the search completes (no interrupt support yet).
class PcTask {
public:
  ~PcTask();
  bool ready() const;
  PcResult get(); // call once after ready(); joins thread
  void cancel();

  PcTask(const PcTask &) = delete;
  PcTask &operator=(const PcTask &) = delete;

private:
  friend std::unique_ptr<PcTask> start_pc_search(BeamInput, PcConfig);
  PcTask(BeamInput input, PcConfig cfg);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Spawn a background PC search. Returns immediately.
std::unique_ptr<PcTask> start_pc_search(BeamInput input, PcConfig cfg = {});
