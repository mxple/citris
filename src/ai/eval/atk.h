#pragma once

#include "eval.h"

// Evaluator for T-spin double chains (20TSD mode).
class AtkEvaluator : public Evaluator {
public:
  float board_eval(const BoardBitset &board) const override {
    // TSD mode: heavily reward T-spin overhangs, keep board clean otherwise.
    return board_eval_default(board, w_);
  }

  float tactical_eval(const Placement &move, int lines_cleared, int attack,
                      const SearchState &parent) const override {
    float score = 0.0f;
    // T-spin doubles are the primary goal — huge bonus.
    if (move.spin == SpinKind::TSpin && lines_cleared == 2)
      score += 40.0f;
    // T-spin triples are even better if achieved.
    else if (move.spin == SpinKind::TSpin && lines_cleared == 3)
      score += 50.0f;
    // T-spin singles are decent — maintain B2B chain.
    else if (move.spin == SpinKind::TSpin && lines_cleared == 1)
      score += 12.0f;
    // T-spin mini — small reward for B2B maintenance.
    else if (move.spin == SpinKind::Mini && lines_cleared > 0)
      score += 5.0f;
    // Quads are acceptable but not the goal.
    else if (lines_cleared == 4)
      score += 15.0f;
    // Non-spin line clears break B2B — slight penalty relative to not clearing.
    else if (lines_cleared > 0 && move.spin == SpinKind::None)
      score -= 2.0f;
    // Reward attack and B2B maintenance.
    score += static_cast<float>(attack) * 2.0f;
    if (parent.attack.b2b > 0)
      score += 3.0f;
    return score;
  }

  float composite(float board_score, float tactical_score,
                  int depth) const override {
    return board_score + tactical_score;
  }

  bool accumulate_tactical() const override { return true; }

  bool is_loud(const SearchState &state) const override {
    // Extend search when the board has a TSD-ready overhang — the next
    // T-piece could score big, so don't cut the search short.
    return count_tsd_slots(state.board) > 0;
  }

private:
  // Count columns with a TSD-ready overhang (cell above a hole, flanked by
  // taller neighbors). Same logic as board_eval_impl's tsd_count.
  static int count_tsd_slots(const BoardBitset &board) {
    int count = 0;
    for (int x = 0; x < 10; ++x) {
      int h = board.column_height(x);
      if (h < 2)
        continue;
      if (board.occupied(x, h - 1) && !board.occupied(x, h - 2)) {
        int lh = (x > 0) ? board.column_height(x - 1) : 40;
        int rh = (x < 9) ? board.column_height(x + 1) : 40;
        if (lh >= h && rh >= h)
          ++count;
      }
    }
    return count;
  }

  // TSD weights: strong overhang reward, moderate hole penalty (overhangs
  // create intentional "holes"), keep board flat otherwise.
  BoardWeights w_{
      .holes = -3.0f, // relaxed — TSD setups have intentional holes
      .cell_coveredness = -0.3f,
      .height = -0.2f,
      .height_upper_half = -1.0f,
      .height_upper_quarter = -5.0f,
      .bumpiness = -0.3f,
      .bumpiness_sq = -0.1f,
      .row_transitions = -0.2f,
      .well_depth = 0.1f,
      .tsd_overhang = 10.0f, // heavily reward TSD-ready shapes
  };
};
