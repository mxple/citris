#pragma once

#include "board_bitset.h"
#include "placement.h"
#include <memory>
#include <vector>

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
  int queue_draws = 0;  // total pop() calls at root — for bag boundary calc
  PieceCounts used_counts{};
};

// ---------------------------------------------------------------------------
// Evaluator trait — pluggable scoring for beam search.
// ---------------------------------------------------------------------------

struct Evaluator {
  virtual ~Evaluator() = default;
  // Takes the full post-move state so evaluators can gate features by
  // resources (hold, bag_remaining) — e.g. AtkEvaluator only rewards T-slots
  // when a T is reachable in the current bag or hold.
  virtual float board_eval(const SearchState &state) const = 0;
  virtual float tactical_eval(const Placement &move, int lines_cleared,
                              int attack, const SearchState &parent) const = 0;
  virtual float composite(float board_score, float tactical_score,
                          int depth) const = 0;
  virtual int move_ordering_key(const Placement &,
                                const SearchState &) const { return 0; }
  virtual bool is_loud(const SearchState &) const { return false; }
  virtual bool accumulate_tactical() const { return false; }
};


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
};

float evaluate_board(const BoardBitset &board, const EvalWeights &weights);
