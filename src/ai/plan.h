#pragma once

#include "board_bitset.h"
#include "engine/piece.h"
#include "placement.h"
#include "vec2.h"
#include <optional>
#include <span>
#include <vector>

// A plan is a *set* of placements the AI wants to play, with the original PV
// order preserved as a dispatch hint. A locked placement advances the plan if
// it matches any remaining step (canonical same_landing) AND the rest of the
// plan is still feasible from the resulting state. Feasibility is verified by
// `check_feasibility` below, which performs an iterated movegen search over
// hold/reorder permutations.
struct Plan {
  struct Step {
    Placement placement;
    int lines_cleared = 0;
    BoardBitset board_after; // refreshed by check_feasibility on each advance
  };

  std::vector<Step> remaining;
  bool feasible = true;
  int played_count = 0;

  bool empty() const { return remaining.empty(); }
  std::size_t size() const { return remaining.size(); }
  const Step *front() const {
    return remaining.empty() ? nullptr : &remaining.front();
  }

  // Try to consume a remaining step whose canonical landing matches `locked`.
  // Canonicalizes `locked` internally, scans `remaining` first-match-wins, and
  // on hit erases the step and increments `played_count`. Returns true on
  // match, false otherwise (state unchanged on miss).
  //
  // Pure plan-internal mutation: does not consult the live queue/hold and
  // does not touch `feasible`. Callers that also want a feasibility recheck
  // should call `check_feasibility` separately afterward.
  bool advance(const Placement &locked);

  // Build a fresh plan whose `remaining` mirrors `placements`. Other Step
  // fields (`lines_cleared`, `board_after`) are left default-initialized —
  // suitable for callers that only need the placement set (e.g. trainer
  // overlays). `feasible` and `played_count` start at their defaults.
  static Plan from_placements(std::span<const Placement> placements);
};

// Verify that `steps` can still be played to completion from the given real
// state, exploring hold-swap / reorder permutations. On success: returns true
// and reorders `steps` so steps[0] is the next placement to dispatch (its
// piece type is reachable from `current` either directly or after one Hold),
// and refreshes each Step's `board_after` and `lines_cleared` to the chosen
// sequence. On failure: returns false; `steps` is left in arbitrary state
// (caller invalidates the plan).
bool check_feasibility(std::vector<Plan::Step> &steps, BoardBitset start_board,
                       PieceType current, std::optional<PieceType> hold,
                       std::span<const PieceType> queue);

// One cell of a plan overlay, in display-space board coordinates. Position is
// already adjusted for line clears performed by earlier steps in the plan, so
// the renderer can draw each cell directly without further bookkeeping.
struct OverlayCell {
  Vec2 pos;
  PieceType type;
};

// Walk `steps` against `board`, simulating each placement and clear, and emit
// one OverlayCell per occupied cell of every drawn step. The returned cells
// live in the *original* board frame: y-coordinates are remapped through a
// row-index table so cleared rows shift later steps upward in the display.
// At most `max_visible` steps are rendered.
std::vector<OverlayCell> build_plan_overlay(BoardBitset board,
                                            std::span<const Plan::Step> steps,
                                            int max_visible = 7);
