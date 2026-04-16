#include "ai_state.h"
#include "ai/board_bitset.h"

bool AIState::plan_computed() const {
  return plan_source_ != PlanSource::None && !plan_.steps.empty();
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
  for (auto &p : state.preview)
    input.queue.push_back(p);
  input.hold = state.hold_piece;
  input.hold_available = state.hold_available;
  input.bag_draws = state.bag_draws;

  last_input_ = input; // copy saved before any move

  if (std::holds_alternative<PcMode>(mode)) {
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

void AIState::on_piece_locked(const eng::PieceLocked &ev) {
  if (!active)
    return;

  if (plan_source_ != PlanSource::None && !plan_.complete() &&
      plan_.current()) {
    const auto &p = plan_.current()->placement;
    bool match = ev.type == p.type && ev.rotation == p.rotation &&
                 ev.x == p.x && ev.y == p.y;
    if (match) {
      plan_.advance();
      if (plan_.complete()) {
        plan_source_ = PlanSource::None;
        needs_search = true;
      }
      // else: plan still valid — no re-search
    } else {
      plan_ = Plan{};
      plan_source_ = PlanSource::None;
      needs_search = true;
    }
  } else {
    needs_search = true;
  }
}

void AIState::on_garbage() {
  if (!active)
    return;
  plan_ = Plan{};
  plan_source_ = PlanSource::None;
  needs_search = true;
}

void AIState::on_undo() {
  if (!active)
    return;
  beam_task_.reset();
  pc_task_.reset();
  plan_ = Plan{};
  plan_source_ = PlanSource::None;
  needs_search = true;
}

void AIState::deactivate() {
  beam_task_.reset();
  pc_task_.reset();
  plan_ = Plan{};
  plan_source_ = PlanSource::None;
  needs_search = false;
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

void AIState::build_plan_from_beam(const BeamResult &result) {
  plan_.steps.clear();
  plan_.current_step = 0;

  if (result.pv.empty())
    return;

  BoardBitset sim = last_input_.board;
  for (size_t i = 0; i < result.pv.size(); ++i) {
    const auto &p = result.pv[i];
    Plan::Step step;
    step.placement = p;
    step.uses_hold = (i == 0) ? result.hold_used : false;
    sim.place(p.type, p.rotation, p.x, p.y);
    step.lines_cleared = sim.clear_lines();
    step.board_after = sim;
    plan_.steps.push_back(std::move(step));
  }

  plan_source_ = PlanSource::BeamSearch;
}

void AIState::build_plan_from_pc(const PcResult &result) {
  plan_.steps.clear();
  plan_.current_step = 0;

  if (result.solution.empty())
    return;

  const auto &queue = last_input_.queue;
  int qi = 0;
  auto hold = last_input_.hold;
  BoardBitset sim = last_input_.board;

  for (const auto &p : result.solution) {
    Plan::Step step;
    step.placement = p;

    PieceType current = queue[qi];
    if (current == p.type) {
      step.uses_hold = false;
      ++qi;
    } else if (hold.has_value() && *hold == p.type) {
      step.uses_hold = true;
      hold = current;
      ++qi;
    } else {
      // Hold was empty — hold current, play next
      step.uses_hold = true;
      hold = current;
      qi += 2;
    }

    sim.place(p.type, p.rotation, p.x, p.y);
    step.lines_cleared = sim.clear_lines();
    step.board_after = sim;
    plan_.steps.push_back(std::move(step));
  }

  plan_source_ = PlanSource::PerfectClear;
}
