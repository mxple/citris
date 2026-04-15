#pragma once

#include "board_bitset.h"
#include "placement.h"
#include <memory>
#include <vector>

struct Checkpoint;

enum class EvalType : int { Tsd, Sprint, Cheese, Default };

// ---------------------------------------------------------------------------
// Board quality weights — reusable by any evaluator.
// ---------------------------------------------------------------------------

struct BoardWeights {
  float holes = -4.0f;
  float cell_coveredness = -0.5f;
  float height = -0.2f;
  float height_upper_half = -1.0f;
  float height_upper_quarter = -5.0f;
  float bumpiness = -0.3f;
  float bumpiness_sq = -0.1f;
  float row_transitions = -0.3f;
  float well_depth = 0.2f;
  float tsd_overhang = 6.0f;
};

// Standalone board quality function — reusable by any evaluator.
float board_eval_default(const BoardBitset &board, const BoardWeights &w);

// ---------------------------------------------------------------------------
// SearchState — what the evaluator sees per node.
// ---------------------------------------------------------------------------

struct SearchState {
  BoardBitset board;
  AttackState attack;
  int lines_cleared = 0;      // cumulative
  int total_attack = 0;       // cumulative
  int input_count = 0;        // cumulative keystrokes
  int depth = 0;              // pieces placed since root
  std::optional<PieceType> hold;
  bool hold_available = true;
  uint8_t bag_remaining = 0x7F;
  int queue_idx = 0;
  int bag_draws = 0;  // total next() calls at root — for bag boundary calc
  PieceCounts used_counts{};
};

// ---------------------------------------------------------------------------
// Evaluator trait — pluggable scoring for beam search.
// ---------------------------------------------------------------------------

struct Evaluator {
  virtual ~Evaluator() = default;
  virtual float board_eval(const BoardBitset &board) const = 0;
  virtual float tactical_eval(const Placement &move, int lines_cleared,
                              int attack, const SearchState &parent) const = 0;
  virtual float composite(float board_score, float tactical_score,
                          int depth) const = 0;
  virtual int move_ordering_key(const Placement &,
                                const SearchState &) const { return 0; }
  virtual bool is_loud(const SearchState &) const { return false; }
  virtual bool accumulate_tactical() const { return false; }
};

// ---------------------------------------------------------------------------
// Legacy checkpoint eval API — kept for backward compatibility until Phase 2.
// ---------------------------------------------------------------------------

struct EvalWeights {
  // Board quality
  float holes = -4.0f;
  float cell_coveredness = -0.5f;
  float height = -0.2f;
  float height_upper_half = -1.0f;
  float height_upper_quarter = -5.0f;
  float bumpiness = -0.3f;
  float bumpiness_sq = -0.1f;
  float row_transitions = -0.3f;
  float well_depth = 0.2f;
  float tsd_overhang = 6.0f;

  // Checkpoint matching
  float checkpoint_match = 10.0f;
  float checkpoint_missing = -15.0f;
  float checkpoint_extra = -20.0f;

  // Annotation scoring
  float annotation_match = 5.0f;
  float annotation_mismatch = -8.0f;
};

float evaluate_board(const BoardBitset &board, const EvalWeights &weights);

float evaluate_checkpoint(const BoardBitset &board, const Checkpoint &target,
                          const EvalWeights &weights);
float evaluate_checkpoint(const BoardBitset &board,
                          const std::vector<const Checkpoint *> &targets,
                          const EvalWeights &weights);
float evaluate_checkpoint_preclr(const BoardBitset &board,
                                 const std::vector<const Checkpoint *> &targets,
                                 const EvalWeights &weights,
                                 const Placement &placement);
