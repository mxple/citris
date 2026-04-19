#pragma once

#include "board_bitset.h"
#include "eval/eval.h"
#include "placement.h"
#include <memory>
#include <optional>
#include <vector>

// ---------------------------------------------------------------------------
// Narrow-waist beam search API
// ---------------------------------------------------------------------------

struct BeamInput {
  BoardBitset board;
  std::vector<PieceType> queue; // queue[0] = current piece
  std::optional<PieceType> hold;
  bool hold_available = true;
  int queue_draws = 0;
  // Live attack state (combo, b2b) at search root. Without this the beam
  // plans as if b2b=0 — meaning it doesn't see B2B-breaking skims as bad
  // (no chain to break in the search's worldview).
  AttackState attack{};
};

struct BeamConfig {
  int width = 800;
  int depth = 0; // 0 = queue.size()
  bool sonic_only = true;
  bool extend_7bag = true;
  std::unique_ptr<Evaluator> evaluator; // required
};

struct BeamResult {
  std::vector<Placement> pv;
  float score = 0.0f;
  std::vector<std::pair<Placement, float>> root_scores;
  bool hold_used = false;
  int depth = 0;
};

// Async handle for a running beam search.
// Destructor cancels and joins the background thread.
class BeamTask {
public:
  ~BeamTask();
  bool ready() const;
  BeamResult get(); // call once after ready(); joins thread
  void cancel();

  BeamTask(const BeamTask &) = delete;
  BeamTask &operator=(const BeamTask &) = delete;

private:
  friend std::unique_ptr<BeamTask> start_beam_search(BeamInput, BeamConfig);
  BeamTask(BeamInput input, BeamConfig cfg);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Spawn a background beam search. Returns immediately.
std::unique_ptr<BeamTask> start_beam_search(BeamInput input, BeamConfig cfg);
