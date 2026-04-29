#pragma once

#include "ai/ai_mode.h"
#include "ai/beam_search.h"
#include "ai/pc/pc_solver.h"
#include "ai/plan.h"
#include "engine/game_state.h"
#include "engine_event.h"
#include <memory>
#include <span>

class AIController;
struct ViewModel;

enum class PlanSource { None, BeamSearch, PerfectClear };

class AIState {
public:
  // --- Config: mutable by UI ---
  AIMode mode = AIMode::Atk;
  BeamOverrides overrides;
  bool active = false;
  bool autoplay = false;
  bool needs_search = false;
  int max_visible = 7;       // plan steps shown in overlay
  int queue_lookahead = 5;   // preview pieces fed to the beam (excludes current)
  // Autoplay commit window. After this many plan steps have been played,
  // invalidate the remaining plan and request a re-search. Trims the
  // speculative tail of long PVs (where bag-extension speculation
  // produces over-optimistic moves). 0 = play the whole plan (legacy
  // behavior). Typical good values: 2-4 — keeps the high-confidence head
  // of each search and discards the lower-confidence tail.
  int plan_commit_limit = 0;

  // --- Read-only queries ---
  bool plan_computed() const;
  bool searching() const;
  PlanSource plan_source() const { return plan_source_; }
  const Plan &current_plan() const { return plan_; }
  float last_score() const { return last_score_; }
  int last_depth() const { return last_depth_; }
  const std::vector<std::pair<Placement, float>> &root_scores() const {
    return last_root_scores_;
  }
  std::unique_ptr<Evaluator> make_evaluator() const;

  // --- Lifecycle ---
  void start_search(const GameState &state);
  bool poll_search(); // returns true if a search just completed
  void on_piece_locked(const eng::PieceLocked &ev, const GameState &state);
  void on_garbage(int lines, const GameState &state);
  void on_undo();
  void clear_search_state(); // tasks + plan + needs_search; preserves config
  void deactivate();         // clear_search_state() + flips active/autoplay off

  // --- Rendering ---
  void fill_plan_overlay(ViewModel &vm, const GameState &state) const;

  // --- Debug accessors (read-only, used by ui/ai_debug_panel) ---
  const BeamInput &last_input() const { return last_input_; }
  const BoardBitset &last_board() const { return last_board_; }
  const BoardBitset &parent_board() const { return parent_board_; }
  const eng::PieceLocked &last_move() const { return last_move_; }
  bool has_last_move() const { return has_last_move_; }
  int tracked_b2b() const { return tracked_b2b_; }
  int parent_b2b() const { return parent_b2b_; }

private:
  std::unique_ptr<BeamTask> beam_task_;
  std::unique_ptr<PcTask> pc_task_;

  Plan plan_;
  PlanSource plan_source_ = PlanSource::None;
  float last_score_ = 0.0f;
  int last_depth_ = 0;
  std::vector<std::pair<Placement, float>> last_root_scores_;

  // Saved at start_search time — used for plan building and PC fallback
  BeamInput last_input_;

  // --- Debug: last move + current board snapshots for the Player Eval
  // panel. `last_board_` is refreshed every frame in fill_plan_overlay (so
  // it's "current" at draw time). `parent_board_` captures that snapshot
  // at the moment a piece locks — i.e., the board BEFORE the locked move
  // was placed — which is what tactical_eval wants as its parent state.
  mutable BoardBitset last_board_{};
  BoardBitset parent_board_{};
  eng::PieceLocked last_move_{};
  bool has_last_move_ = false;
  int tracked_b2b_ = 0;      // rolling; updated each PieceLocked to ev.new_b2b
  int parent_b2b_ = 0;       // snapshot of tracked_b2b_ BEFORE the last move

  void build_plan(std::span<const Placement> pv);
  void build_plan_from_beam(const BeamResult &result);
  void build_plan_from_pc(const PcResult &result);
  void start_beam_fallback();

};
