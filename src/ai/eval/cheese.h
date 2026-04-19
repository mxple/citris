#pragma once

#include "eval.h"

// Evaluator for cheese/downstack mode — rewards line clears and clean boards.
class CheeseEvaluator : public Evaluator {
public:
  float board_eval(const SearchState &state) const override {
    // Cheese boards start with holes — heavily penalize remaining holes
    // and covered cells to guide the AI toward clearing garbage lines.
    return board_eval_default(state.board, w_);
  }

  float tactical_eval(const Placement &move, int lines_cleared, int attack,
                      const SearchState &parent) const override {
    float score = static_cast<float>(lines_cleared) * 4.0f;

    // Reward placements that touch the bottom row — means all garbage is cleared.
    for (auto [cx, cy] : move.cells())
      if (cy == 0) { score += 50.0f; break; }

    return score;
  }

  float composite(float board_score, float tactical_score,
                  int depth) const override {
    // Fewer pieces used = better. Penalize depth (piece count) directly.
    return board_score + tactical_score - static_cast<float>(depth) * 2.0f;
  }

  bool accumulate_tactical() const override { return true; }

private:
  // Heavier hole/coveredness penalties than default — cheese is all about
  // uncovering and clearing garbage.
  BoardWeights w_{
      .holes = -6.0f,
      .cell_coveredness = -1.0f,
      .height = -0.3f,
      .height_upper_half = -1.5f,
      .height_upper_quarter = -5.0f,
      .bumpiness = -0.2f,
      .bumpiness_sq = -0.05f,
      .row_transitions = -0.4f,
      .well_depth = 0.1f,
      .tsd_overhang = 0.0f, // don't care about T-spin setups in cheese
  };
};
