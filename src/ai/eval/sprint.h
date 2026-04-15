#pragma once

#include "eval.h"

// Evaluator for sprint mode — minimize inputs while clearing 40 lines.
class SprintEvaluator : public Evaluator {
public:
  float board_eval(const BoardBitset &board) const override {
    // Sprint wants a clean, flat, low board to enable quads.
    return board_eval_default(board, w_);
  }

  float tactical_eval(const Placement &move, int lines_cleared, int attack,
                      const SearchState &parent) const override {
    float score = 0.0f;
    // Strongly reward quads — they clear 4 lines per piece (optimal rate).
    // Singles/doubles/triples are progressively less efficient.
    if (lines_cleared == 4)
      score += 50.0f;
    else if (lines_cleared == 3)
      score += 20.0f;
    else if (lines_cleared == 2)
      score += 8.0f;
    else if (lines_cleared == 1)
      score += 2.0f;
    // Penalize input count — fewer inputs means faster sprint time.
    score -= static_cast<float>(parent.input_count) * 0.15f;
    return score;
  }

  float composite(float board_score, float tactical_score,
                  int depth) const override {
    return board_score + tactical_score;
  }

  bool accumulate_tactical() const override { return true; }

  int move_ordering_key(const Placement &move,
                        const SearchState &) const override {
    // Prefer moves closer to the well column (rightmost by convention).
    // This helps the search find quads faster.
    return -static_cast<int>(move.x);
  }

private:
  // Sprint weights: strong well preference, low bumpiness, minimal height.
  BoardWeights w_{
      .holes = -4.0f,
      .cell_coveredness = -0.5f,
      .height = -0.1f,
      .height_upper_half = -0.8f,
      .height_upper_quarter = -5.0f,
      .bumpiness = -0.4f,
      .bumpiness_sq = -0.15f,
      .row_transitions = -0.2f,
      .well_depth = 0.5f, // strong well bonus — we want quads
      .tsd_overhang = 0.0f,
  };
};
