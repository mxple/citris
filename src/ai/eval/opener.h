#pragma once

#include "checkpoint.h"
#include "eval.h"
#include <vector>

struct OpenerWeights {
  BoardWeights board;
  float checkpoint_match = 10.0f;
  float checkpoint_missing = -15.0f;
  float checkpoint_extra = -20.0f;
  float annotation_match = 5.0f;
  float annotation_mismatch = -8.0f;
  float clear_row_progress = 0.5f;
};

// Evaluator for opener training — scores placements against checkpoint targets.
class OpenerEvaluator : public Evaluator {
public:
  OpenerEvaluator(std::vector<const Checkpoint *> targets, OpenerWeights w = {})
      : targets_(std::move(targets)), w_(w) {}

  void set_targets(std::vector<const Checkpoint *> targets) {
    targets_ = std::move(targets);
  }

  float board_eval(const BoardBitset &board) const override {
    return board_eval_default(board, w_.board);
  }

  float tactical_eval(const Placement &move, int lines_cleared, int attack,
                      const SearchState &parent) const override {
    if (targets_.empty())
      return 0.0f;

    BoardBitset preclr = parent.board;
    preclr.place(move.type, move.rotation, move.x, move.y);

    BoardBitset postclr = preclr;
    postclr.clear_lines();

    PieceCounts child_counts = parent.used_counts;
    child_counts[static_cast<int>(move.type)]++;

    float best = -1e18f;
    for (auto *t : targets_) {
      float score = score_checkpoint(preclr, *t, move);

      // Perfect match bonus: board matches checkpoint and pieces satisfy
      // constraint.
      if ((t->matches(preclr) || t->matches(postclr)) &&
          t->constraint.path_valid(child_counts)) {
        score += 100.0f;
      }

      best = std::max(best, score);
    }
    return best;
  }

  float composite(float board_score, float tactical_score,
                  int depth) const override {
    return board_score + tactical_score;
  }

private:
  float score_checkpoint(const BoardBitset &board, const Checkpoint &cp,
                         const Placement &move) const {
    if (is_clear_target(cp))
      return score_clear_target(board, cp);

    auto ms = cp.match_score(board);
    float score =
        w_.checkpoint_match * ms.matched + w_.checkpoint_missing * ms.missing;
    if (cp.match_mode == MatchMode::Strict)
      score += w_.checkpoint_extra * ms.extra;

    if (cp.has_piece_annotations()) {
      auto cells = move.cells();
      for (auto &c : cells) {
        int8_t expected = cp.cell_piece(c.x, c.y);
        if (expected < 0)
          continue;
        if (expected == static_cast<int8_t>(move.type))
          score += w_.annotation_match;
        else
          score += w_.annotation_mismatch;
      }
    }
    return score;
  }

  static bool is_clear_target(const Checkpoint &cp) {
    if (cp.match_mode != MatchMode::Strict)
      return false;
    for (int y = 0; y < cp.height(); ++y)
      if (cp.rows[y] & 0x3FF)
        return false;
    return cp.height() > 0;
  }

  float score_clear_target(const BoardBitset &board,
                           const Checkpoint &cp) const {
    float score = 0.0f;
    for (int y = 0; y < cp.height(); ++y) {
      int filled = std::popcount(static_cast<unsigned>(board.rows[y] & 0x3FF));
      score += static_cast<float>(filled * filled) * w_.clear_row_progress;
    }
    return score;
  }

  std::vector<const Checkpoint *> targets_;
  OpenerWeights w_;
};
