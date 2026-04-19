#pragma once

#include "engine/piece.h"
#include "eval.h"
#include <algorithm>
#include <cmath>

// Evaluator for T-spin double chains (20TSD mode).
//
// Decision-making improvements over the original geometric eval:
//
// 1. Donation lookahead — instead of a static `tsd_overhang` shape bonus,
//    actually simulate placing a T into each detected slot and reward by
//    the lines it would clear. A pretty-looking slot that only clears 1
//    line scores far less than a true TSD setup.
//
// 2. T-accessibility gate — donation reward is multiplied by how reachable
//    a T is (in hold + in current bag, 0..2). With no T accessible, the
//    bot gets ZERO bonus for T-slot shapes — fixes the "build pretty TSD
//    setups when no T is coming" failure mode.
//
// 3. Larger lock-time waste penalties — wasting a T outside a spin is
//    heavily penalized; wasting an I outside a quad is also penalized.
//
// 4. Depth-discounted cumulative tactical — fusion-style sqrt(depth)
//    divisor on the cumulative tactical score so a TSD 6 plies away counts
//    as ~half a TSD 1 ply away. Stops speculative chains from outweighing
//    immediate, real attacks.
class AtkEvaluator : public Evaluator {
public:
  float board_eval(const SearchState &state) const override {
    const BoardBitset &board = state.board;
    float shape = board_eval_default(board, w_);

    // Donation: iterate up to kMaxSlots detected T-slots. For each, place
    // a T on a sim copy and add reward weighted by line clears. The sim
    // board accumulates placements across iterations, so:
    //   - Chained pending TSDs (one drops the board, exposes another)
    //     are detected naturally via the lowered post-clear board.
    //   - Parallel pending TSDs (multiple slots existing simultaneously)
    //     are also detected because find_t_slot moves to other slots once
    //     the first slot's mouth cells are filled by the placed T.
    // No early break on cleared<2: a slot that doesn't clear contributes
    // 0 to the score (kTSlotWeights[0]=0), but the placed T can unblock
    // detection of nearby parallel slots.
    constexpr int kMaxSlots = 4;
    BoardBitset sim = board;
    float donation_score = 0.0f;
    for (int i = 0; i < kMaxSlots; ++i) {
      Placement t_slot;
      if (!find_t_slot(sim, t_slot))
        break;
      sim.place(t_slot.type, t_slot.rotation, t_slot.x, t_slot.y);
      int cleared = sim.clear_lines();
      if (cleared < 0 || cleared > 3)
        break;
      donation_score += kTSlotWeights[cleared];
    }

    // T-accessibility multiplier (was a hard iteration cap before — that
    // prevented multiple pending TSDs from ever being counted unless T
    // happened to be in both hold AND bag, which is the user's "doesn't
    // like to stack up" symptom).
    //   0.3 base — pending T-slots have value across bag boundaries
    //              (T arrives within ~7 pieces regardless).
    //   +0.5 if T in hold (immediate access)
    //   +0.5 if T in current bag (≤7 pieces away)
    // Range: 0.3 .. 1.3. With multi-slot iteration, a 3-pending-TSD board
    // and T in hand scores donation_score=24 × 1.3 ≈ 31, vs a 1-TSD board
    // at 8 × 1.3 ≈ 10 — multi-pending now materially beats single-immediate.
    bool t_in_hold = state.hold.has_value() && *state.hold == PieceType::T;
    bool t_in_bag = (state.bag_remaining >> static_cast<int>(PieceType::T)) & 1;
    float multiplier =
        0.3f + (t_in_hold ? 0.5f : 0.0f) + (t_in_bag ? 0.5f : 0.0f);
    return shape + donation_score * multiplier;
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
    // T-spin single: minimal tactical reward. B2B maintenance is paid via the
    // in-B2B transient below, not here — we don't want the AI chasing TSS
    // setups when TSDs (or just holding T) are better long-term plays.
    else if (move.spin == SpinKind::TSpin && lines_cleared == 1)
      score += 2.0f;
    // T-spin mini double: genuinely valuable — clears 2 lines, sends 1 attack,
    // sustains B2B. Often worth chasing when a TSD isn't available.
    else if (move.spin == SpinKind::Mini && lines_cleared == 2)
      score += 6.0f;
    // T-spin mini single: 0 attack, only value is B2B maintenance, which the
    // in-B2B transient below already pays for. No separate reward.
    else if (move.spin == SpinKind::Mini && lines_cleared == 1)
      score += 0.0f;
    // Quads are acceptable but not the goal.
    else if (lines_cleared == 4)
      score += 15.0f;
    // Non-spin skim clears break B2B. Penalty is strong so attack-from-combo
    // can't bribe the AI into dropping a chain; scales with chain length.
    else if (lines_cleared > 0 && move.spin == SpinKind::None &&
             lines_cleared < 4) {
      score += (parent.attack.b2b > 0)
                   ? -80.0f - 2.0f * static_cast<float>(parent.attack.b2b)
                   : -2.0f;
    }
    // Locking T outside a spin is a real waste .
    // Without this the bot was happy to dump T into a flat surface when no
    // slot was visible, instead of holding for a future setup.
    if (move.type == PieceType::T &&
        (lines_cleared == 0 ||
         move.spin != SpinKind::TSpin && move.spin != SpinKind::Mini))
      score -= 25.0f;
    // Locking I without a quad is wasteful too: I is the only piece that
    // can clear 4 at once, and TSDquad mode requires it. Smaller magnitude
    // than T-waste because I can also be useful for skimming garbage.
    if (move.type == PieceType::I && lines_cleared < 4)
      score -= 15.0f;
    // Reward attack and B2B maintenance. In-B2B bonus grows with chain length
    // (capped) so the AI prefers extending a chain over staying neutral.
    score += static_cast<float>(attack) * 2.0f;
    if (parent.attack.b2b > 0)
      score +=
          3.0f + std::min(static_cast<float>(parent.attack.b2b), 5.0f) * 0.5f;
    return score;
  }

  // Depth-discount the tactical score (which is cumulative — see
  // accumulate_tactical()). Fusion-style sqrt(depth) divisor, but with a
  // looser clamp than fusion's 2.45 (which kicks in at depth 6 and over-
  // penalizes 2-bag TSD chains). Clamping at 3.5 keeps the discount active
  // through depth ~12, so a 4-TSD chain executing across the search horizon
  // accumulates ~+160 / 3.5 ≈ +46 vs an immediate single TSD at +40 /
  // sqrt(1) = +40 — chain wins, just barely. Pending-TSD shape signal in
  // board_eval (donation lookahead) carries the rest.
  //   depth 1 → 1.00 (no discount)
  //   depth 4 → 2.00
  //   depth 9 → 3.00
  //   depth 12+ → 3.50 (clamped)
  // Tune the clamp upward (4.0+) for even longer planning, downward (back
  // to 2.45) for more grounded immediate-attack play.
  float composite(float board_score, float tactical_score,
                  int depth) const override {
    int d = std::max(depth, 1);
    float depth_factor = std::min(std::sqrt(static_cast<float>(d)), 3.5f);
    return board_score + tactical_score / depth_factor;
  }

  bool accumulate_tactical() const override { return true; }

  bool is_loud(const SearchState &state) const override {
    // Extend search when the board has a TSD-ready overhang — the next
    // T-piece could score big, so don't cut the search short.
    return count_tsd_slots(state.board) > 0;
  }

private:
  // Per-line-clear weight applied to a successful donation T-placement.
  // Index = lines cleared by the simulated T.
  //   [0] 0.0  — slot exists but T placement clears nothing (no value)
  //   [1] 2.0  — TSS (single attack, B2B-only value)
  //   [2] 8.0  — TSD (the goal — sized to be ~20% of the live TSD bonus)
  //   [3] 12.0 — TST (rarely hit from donation, but worth distinguishing)
  static constexpr float kTSlotWeights[4] = {0.0f, 2.0f, 8.0f, 12.0f};

  // Detect a T-spin-double-ready slot on the board surface. Two patterns
  // are checked at every column triple (x, x+1, x+2), x ∈ [0, 7]:
  //
  // RIGHT-FACING overhang (cap on the right wall):
  //   col x:   top occ at y=h-1
  //   col x+1: empty at y=h-1, h, h+1   (the slot)
  //   col x+2: occ at h-1, EMPTY at h, occ at h+1   (overhang cap)
  //   T@South placement at (x, h-1) — three top cells fill row h, tail
  //   fills (x+1, h-1).
  //
  // LEFT-FACING is the mirror: cap on column x's wall, slot at x+1, base
  // wall on the right at x+2. T@South at (x, h-1) where h = column-x+2's
  // height plays the same shape mirrored.
  static bool find_t_slot(const BoardBitset &board, Placement &out) {
    int h[10];
    for (int x = 0; x < 10; ++x)
      h[x] = board.column_height(x);

    for (int x = 0; x <= 7; ++x) {
      // Pattern A: right-facing overhang. Wall column = x, slot = x+1,
      // overhang cap on column x+2 at row h[x]+1, with column x+2 having
      // a hole at row h[x] (the cell directly under the cap).
      if (h[x] >= 1 && h[x] > h[x + 1] && h[x + 2] >= h[x] + 2 &&
          !board.occupied(x + 2, h[x])) {
        out = Placement{PieceType::T, Rotation::South, static_cast<int8_t>(x),
                        static_cast<int8_t>(h[x] - 1), SpinKind::None};
        return true;
      }

      // Pattern B: left-facing overhang. Wall column = x+2, slot = x+1,
      // overhang cap on column x at row h[x+2]+1 with hole at row h[x+2].
      if (h[x + 2] >= 1 && h[x + 2] > h[x + 1] && h[x] >= h[x + 2] + 2 &&
          !board.occupied(x, h[x + 2])) {
        out = Placement{PieceType::T, Rotation::South, static_cast<int8_t>(x),
                        static_cast<int8_t>(h[x + 2] - 1), SpinKind::None};
        return true;
      }
    }
    return false;
  }

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

  // TSD weights. `tsd_overhang` is now ZERO — the donation lookahead in
  // board_eval() replaces the static shape bonus with a dynamic, T-aware
  // reward. Keep the rest as before: hole penalty stays large because
  // donation handles the "intentional cavity" case explicitly.
  BoardWeights w_{
      .holes = -7.0f,
      .cell_coveredness = -0.8f,
      .height = -0.3f,
      .height_upper_half = -1.5f,
      .height_upper_quarter = -5.0f,
      .bumpiness = -0.3f,
      .bumpiness_sq = -0.3f,
      .row_transitions = -0.4f,
      .well_depth = 0.4f,
      .tsd_overhang = 0.0f, // replaced by donation lookahead
  };
};
