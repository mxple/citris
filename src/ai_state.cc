#include "ai_state.h"
#include "ai/board_bitset.h"
#include "ai/plan.h"
#include "render/view_model.h"

#include <algorithm>


bool AIState::plan_computed() const {
  return plan_source_ != PlanSource::None && !plan_.empty();
}

bool AIState::searching() const {
  return (beam_task_ && !beam_task_->ready()) ||
         (pc_task_ && !pc_task_->ready());
}

std::unique_ptr<Evaluator> AIState::make_evaluator() const {
  return ::make_evaluator(mode);
}

void AIState::start_search(const GameState &state) {
  beam_task_.reset();
  pc_task_.reset();
  plan_ = Plan{};
  plan_source_ = PlanSource::None;

  BeamInput input;
  input.board = BoardBitset::from_board(state.board);
  input.queue.push_back(state.current_piece.type);
  int next_visible =
      std::min(static_cast<int>(state.queue.size()), queue_lookahead);
  for (int i = 0; i < next_visible; ++i)
    input.queue.push_back(state.queue[i]);
  input.hold = state.hold_piece;
  input.hold_available = state.hold_available;
  input.queue_draws = state.queue_draws;
  input.attack = state.attack_state; // seed b2b/combo into search root

  last_input_ = input; // copy saved before any move

  if (mode == AIMode::Pc) {
    PcConfig cfg;
    cfg.use_hold = true;
    cfg.max_pieces = static_cast<int>(input.queue.size());
    pc_task_ = start_pc_search(std::move(input), cfg);
  } else {
    BeamConfig cfg;
    cfg.width = overrides.width;
    cfg.depth = overrides.depth;
    cfg.evaluator = make_evaluator();
    beam_task_ = start_beam_search(std::move(input), std::move(cfg));
  }
}

bool AIState::poll_search() {
  if (pc_task_ && pc_task_->ready()) {
    auto result = pc_task_->get();
    pc_task_.reset();
    if (result.found) {
      build_plan_from_pc(result);
    } else {
      // PC found nothing — fall back to beam immediately
      start_beam_fallback();
    }
    return true;
  }

  if (beam_task_ && beam_task_->ready()) {
    auto result = beam_task_->get();
    beam_task_.reset();
    last_score_ = result.score;
    last_depth_ = result.depth;
    last_root_scores_ = result.root_scores;
    build_plan_from_beam(result);
    return true;
  }

  return false;
}

void AIState::on_piece_locked(const eng::PieceLocked &ev,
                              const GameState &state) {
  // Debug hook: snapshot the move and the pre-move board regardless of
  // whether the AI is active — we want to evaluate the player's moves too.
  // `last_board_` holds the most recent snapshot taken in fill_plan_overlay
  // (last frame's board state = board BEFORE the just-locked piece).
  last_move_ = ev;
  parent_board_ = last_board_;
  parent_b2b_ = tracked_b2b_;
  tracked_b2b_ = ev.new_b2b;
  has_last_move_ = true;

  if (!active)
    return;

  if (plan_source_ == PlanSource::None || plan_.empty()) {
    needs_search = true;
    return;
  }

  if (!plan_.advance({ev.type, ev.rotation, ev.x, ev.y})) {
    plan_ = Plan{};
    plan_source_ = PlanSource::None;
    needs_search = true;
    return;
  }

  if (plan_.empty() ||
      (plan_commit_limit > 0 && plan_.played_count >= plan_commit_limit)) {
    plan_source_ = PlanSource::None;
    needs_search = true;
    return;
  }

  // Verify the remainder is still achievable from the post-lock real state,
  // exploring hold-swap / reorder permutations. On success, remaining is
  // re-ordered so front() is the next dispatch target.
  plan_.feasible = check_feasibility(
      plan_.remaining, BoardBitset::from_board(state.board),
      state.current_piece.type, state.hold_piece, state.queue);
  if (!plan_.feasible)
    needs_search = true;
}

void AIState::on_garbage(int lines, const GameState &state) {
  if (!active)
    return;
  // Offset remaining plan steps rather than invalidating outright. The
  // y-shift may make some previously-legal landings now obstructed, so
  // re-run feasibility against the new board.
  for (auto &step : plan_.remaining)
    step.placement.y += lines;
  if (!plan_.empty()) {
    plan_.feasible = check_feasibility(
        plan_.remaining, BoardBitset::from_board(state.board),
        state.current_piece.type, state.hold_piece, state.queue);
  }
  needs_search = true;
}

void AIState::on_undo() {
  // Invalidate debug snapshots on undo — the last_move_ we cached no longer
  // corresponds to the current board state, and tracked_b2b_ may be wrong
  // relative to the new (undone) state.
  has_last_move_ = false;
  tracked_b2b_ = 0;

  if (!active)
    return;
  beam_task_.reset();
  pc_task_.reset();
  plan_ = Plan{};
  plan_source_ = PlanSource::None;
  needs_search = true;
}

void AIState::clear_search_state() {
  beam_task_.reset();
  pc_task_.reset();
  plan_ = Plan{};
  plan_source_ = PlanSource::None;
  needs_search = false;

  // Drop debug snapshots too — this path fires on game restart (see
  // GameManager::restart in game_manager.cc:40). Keeping a stale
  // tracked_b2b_ across restarts would make the Player Eval panel
  // fabricate a B2B streak that no longer exists.
  has_last_move_ = false;
  tracked_b2b_ = 0;
}

void AIState::deactivate() {
  clear_search_state();
  autoplay = false;
  active = false;
}

void AIState::start_beam_fallback() {
  BeamConfig cfg;
  cfg.width = overrides.width;
  cfg.depth = overrides.depth;
  cfg.evaluator = make_evaluator(); // PcMode returns TsdEvaluator
  beam_task_ = ::start_beam_search(last_input_, std::move(cfg));
}

// Walk placements against last_input_, truncating at the known-piece
// boundary. Each step is simulated on a scratch board so board_after and
// lines_cleared are populated for check_feasibility later.
void AIState::build_plan(std::span<const Placement> pv) {
  plan_ = Plan{};
  if (pv.empty())
    return;

  const auto &queue = last_input_.queue;
  int qi = 0;
  auto hold = last_input_.hold;
  BoardBitset sim = last_input_.board;

  for (const auto &p : pv) {
    bool queue_empty = qi >= (int)queue.size();
    if (queue_empty && !hold.has_value())
      break;

    if (queue_empty) {
      if (*hold != p.type)
        break;
      hold = std::nullopt;
    } else {
      PieceType current = queue[qi];
      if (current == p.type) {
        ++qi;
      } else if (hold.has_value() && *hold == p.type) {
        hold = current;
        ++qi;
      } else {
        if (qi + 1 >= (int)queue.size())
          break;
        hold = current;
        qi += 2;
      }
    }

    Plan::Step step;
    step.placement = p;
    sim.place(p.type, p.rotation, p.x, p.y);
    step.lines_cleared = sim.clear_lines();
    step.board_after = sim;
    plan_.remaining.push_back(std::move(step));
  }
}

void AIState::build_plan_from_beam(const BeamResult &result) {
  build_plan(result.pv);
  plan_source_ = PlanSource::BeamSearch;
}

void AIState::build_plan_from_pc(const PcResult &result) {
  build_plan(result.solution);
  plan_source_ = PlanSource::PerfectClear;
}

void AIState::fill_plan_overlay(ViewModel &vm, const GameState &state) const {
  // Refresh the debug snapshot every frame. Done unconditionally so the
  // Player Eval panel stays live even when the AI isn't active or no plan
  // exists. `last_board_` is mutable to allow this from a const method.
  last_board_ = BoardBitset::from_board(state.board);

  if (!plan_computed() || !plan_.feasible)
    return;
  BoardBitset sim = BoardBitset::from_board(state.board);
  vm.plan_overlay = build_plan_overlay(sim, plan_.remaining, max_visible);
}

