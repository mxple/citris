#include "ai/eval.h"
#include "ai/checkpoint.h"
#include <algorithm>
#include <bit>
#include <cmath>

// ---------------------------------------------------------------------------
// board_eval_default — shared board quality function (BoardWeights)
// ---------------------------------------------------------------------------

static float board_eval_impl(const BoardBitset &board, float w_holes,
                             float w_covered, float w_height,
                             float w_height_upper_half,
                             float w_height_upper_quarter, float w_bump,
                             float w_bump_sq, float w_row_trans,
                             float w_well, float w_tsd) {
  int heights[10];
  int max_h = 0;
  for (int x = 0; x < 10; ++x) {
    heights[x] = board.column_height(x);
    max_h = std::max(max_h, heights[x]);
  }
  if (max_h == 0)
    return 0.0f;

  int holes = 0, covered = 0;
  for (int x = 0; x < 10; ++x) {
    int topmost_hole = -1;
    for (int y = heights[x] - 1; y >= 0; --y) {
      if (!board.occupied(x, y)) {
        ++holes;
        topmost_hole = y;
      }
    }
    if (topmost_hole >= 0) {
      int cov = 0;
      for (int y = topmost_hole + 1; y < heights[x] && cov < 6; ++y)
        if (board.occupied(x, y))
          ++cov;
      covered += cov;
    }
  }

  float score = max_h * w_height;
  score += std::max(0, max_h - 10) * w_height_upper_half;
  score += std::max(0, max_h - 15) * w_height_upper_quarter;

  int well_col = -1, well_depth_val = 0;
  for (int x = 0; x < 10; ++x) {
    int left_h = (x > 0) ? heights[x - 1] : 40;
    int right_h = (x < 9) ? heights[x + 1] : 40;
    int depth = std::min(left_h, right_h) - heights[x];
    if (depth > well_depth_val) {
      well_depth_val = depth;
      well_col = x;
    }
  }

  int bump = 0, bump_sq = 0;
  for (int x = 0; x < 9; ++x) {
    if (x == well_col || x + 1 == well_col)
      continue;
    int diff = std::abs(heights[x] - heights[x + 1]);
    bump += diff;
    bump_sq += diff * diff;
  }

  int transitions = 0;
  for (int y = 0; y < max_h; ++y) {
    uint16_t row = board.rows[y] & 0x3FF;
    if (!(row & 1))
      ++transitions;
    transitions += std::popcount(static_cast<unsigned>(row ^ (row >> 1))) & 0xF;
    if (!(row & (1 << 9)))
      ++transitions;
  }

  int tsd_count = 0;
  for (int x = 0; x < 10; ++x) {
    int h = heights[x];
    if (h < 2)
      continue;
    if (board.occupied(x, h - 1) && !board.occupied(x, h - 2)) {
      int left_h = (x > 0) ? heights[x - 1] : 40;
      int right_h = (x < 9) ? heights[x + 1] : 40;
      if (left_h >= h && right_h >= h)
        ++tsd_count;
    }
  }

  score += holes * w_holes;
  score += covered * w_covered;
  score += bump * w_bump;
  score += bump_sq * w_bump_sq;
  score += transitions * w_row_trans;
  score += well_depth_val * w_well;
  score += tsd_count * w_tsd;
  return score;
}

float board_eval_default(const BoardBitset &board, const BoardWeights &w) {
  return board_eval_impl(board, w.holes, w.cell_coveredness, w.height,
                         w.height_upper_half, w.height_upper_quarter,
                         w.bumpiness, w.bumpiness_sq, w.row_transitions,
                         w.well_depth, w.tsd_overhang);
}

// ---------------------------------------------------------------------------
// Legacy evaluate_board (EvalWeights) — delegates to shared impl
// ---------------------------------------------------------------------------

float evaluate_board(const BoardBitset &board, const EvalWeights &w) {
  // 1. Column heights
  int heights[10];
  int max_h = 0;
  for (int x = 0; x < 10; ++x) {
    heights[x] = board.column_height(x);
    max_h = std::max(max_h, heights[x]);
  }

  if (max_h == 0)
    return 0.0f;

  // 2. Holes and cell coveredness
  int holes = 0;
  int covered = 0;
  for (int x = 0; x < 10; ++x) {
    int topmost_hole = -1;
    for (int y = heights[x] - 1; y >= 0; --y) {
      if (!board.occupied(x, y)) {
        ++holes;
        topmost_hole = y;
      }
    }
    if (topmost_hole >= 0) {
      int cov = 0;
      for (int y = topmost_hole + 1; y < heights[x] && cov < 6; ++y) {
        if (board.occupied(x, y))
          ++cov;
      }
      covered += cov;
    }
  }

  // 3. Height penalties
  float score = 0.0f;
  score += max_h * w.height;
  score += std::max(0, max_h - 10) * w.height_upper_half;
  score += std::max(0, max_h - 15) * w.height_upper_quarter;

  // 4. Well detection — find deepest well column
  int well_col = -1;
  int well_depth_val = 0;
  for (int x = 0; x < 10; ++x) {
    int left_h = (x > 0) ? heights[x - 1] : 40;
    int right_h = (x < 9) ? heights[x + 1] : 40;
    int depth = std::min(left_h, right_h) - heights[x];
    if (depth > well_depth_val) {
      well_depth_val = depth;
      well_col = x;
    }
  }

  // 5. Bumpiness (skip well column)
  int bump = 0;
  int bump_sq = 0;
  for (int x = 0; x < 9; ++x) {
    if (x == well_col || x + 1 == well_col)
      continue;
    int diff = std::abs(heights[x] - heights[x + 1]);
    bump += diff;
    bump_sq += diff * diff;
  }

  // 6. Row transitions
  int transitions = 0;
  for (int y = 0; y < max_h; ++y) {
    uint16_t row = board.rows[y] & 0x3FF;
    // Left wall transition
    if (!(row & 1))
      ++transitions;
    // Internal transitions
    transitions += std::popcount(static_cast<unsigned>(row ^ (row >> 1))) & 0xF;
    // Right wall transition
    if (!(row & (1 << 9)))
      ++transitions;
  }

  // 7. TSD overhang detection
  int tsd_count = 0;
  for (int x = 0; x < 10; ++x) {
    int h = heights[x];
    if (h < 2)
      continue;
    if (board.occupied(x, h - 1) && !board.occupied(x, h - 2)) {
      int left_h = (x > 0) ? heights[x - 1] : 40;
      int right_h = (x < 9) ? heights[x + 1] : 40;
      if (left_h >= h && right_h >= h)
        ++tsd_count;
    }
  }

  score += holes * w.holes;
  score += covered * w.cell_coveredness;
  score += bump * w.bumpiness;
  score += bump_sq * w.bumpiness_sq;
  score += transitions * w.row_transitions;
  score += well_depth_val * w.well_depth;
  score += tsd_count * w.tsd_overhang;

  return score;
}

float evaluate_checkpoint(const BoardBitset &board, const Checkpoint &target,
                          const EvalWeights &w) {
  auto ms = target.match_score(board);
  float cp_score =
      w.checkpoint_match * ms.matched + w.checkpoint_missing * ms.missing;
  if (target.match_mode == MatchMode::Strict)
    cp_score += w.checkpoint_extra * ms.extra;
  return cp_score + evaluate_board(board, w);
}

float evaluate_checkpoint(const BoardBitset &board,
                          const std::vector<const Checkpoint *> &targets,
                          const EvalWeights &w) {
  if (targets.empty())
    return evaluate_board(board, w);
  float best = -1e18f;
  for (auto *t : targets)
    best = std::max(best, evaluate_checkpoint(board, *t, w));
  return best;
}

// Detect checkpoints where the target board is entirely empty (perfect clear
// target). For these, the standard "extra cell" penalty gives a constant
// negative score at each depth that provides no search guidance. Instead we
// score by row-completion progress: filling rows toward 10/10.
static bool is_clear_target(const Checkpoint &cp) {
  if (cp.match_mode != MatchMode::Strict)
    return false;
  for (int y = 0; y < cp.height(); ++y)
    if (cp.rows[y] & 0x3FF)
      return false;
  return cp.height() > 0;
}

static float score_clear_target(const BoardBitset &board, const Checkpoint &cp,
                                const EvalWeights &w) {
  float score = 0.0f;
  for (int y = 0; y < cp.height(); ++y) {
    int filled = std::popcount(static_cast<unsigned>(board.rows[y] & 0x3FF));
    // Quadratic: heavily reward nearly-complete rows
    score += static_cast<float>(filled * filled) * 0.5f;
  }
  return score;
}

float evaluate_checkpoint_preclr(const BoardBitset &board,
                                 const std::vector<const Checkpoint *> &targets,
                                 const EvalWeights &w,
                                 const Placement &placement) {
  if (targets.empty())
    return 0.0f;

  auto cells = placement.cells();
  float best = -1e18f;

  for (auto *t : targets) {
    float cp_score;
    if (is_clear_target(*t)) {
      cp_score = score_clear_target(board, *t, w);
    } else {
      auto ms = t->match_score(board);
      cp_score =
          w.checkpoint_match * ms.matched + w.checkpoint_missing * ms.missing;
      if (t->match_mode == MatchMode::Strict)
        cp_score += w.checkpoint_extra * ms.extra;
    }

    if (t->has_piece_annotations()) {
      for (auto &c : cells) {
        int8_t expected = t->cell_piece(c.x, c.y);
        if (expected < 0)
          continue;
        if (expected == static_cast<int8_t>(placement.type))
          cp_score += w.annotation_match;
        else
          cp_score += w.annotation_mismatch;
      }
    }

    best = std::max(best, cp_score);
  }

  return best;
}
