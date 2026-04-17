#pragma once

#include "ai/ai_mode.h"
#include "ai/beam_search.h"
#include "ai/pc/pc_solver.h"
#include "ai/plan.h"
#include "engine/game_state.h"
#include "engine_event.h"
#include <memory>

enum class PlanSource { None, BeamSearch, PerfectClear };

class AIState {
public:
  // --- Config: mutable by UI ---
  AIMode mode = AtkMode{};
  BeamOverrides overrides;
  bool active = false;
  bool autoplay = false;
  bool needs_search = false;
  int max_visible = 7;       // plan steps shown in overlay
  int queue_lookahead = 5;   // preview pieces fed to the beam (excludes current)

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
  void on_piece_locked(const eng::PieceLocked &ev);
  void on_garbage(int lines);
  void on_undo();
  void deactivate();

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

  void build_plan_from_beam(const BeamResult &result);
  void build_plan_from_pc(const PcResult &result);
  void start_beam_fallback();
};
