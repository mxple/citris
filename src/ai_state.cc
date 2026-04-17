#include "ai_state.h"
#include "ai/board_bitset.h"
#include "ai/plan_overlay.h"
#include "controller/ai_controller.h"
#include "render/view_model.h"

#include <imgui.h>

namespace {

int mode_to_index(const AIMode &m) {
  return std::visit(
      [](const auto &v) -> int {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, AtkMode>)  return 0;
        if constexpr (std::is_same_v<T, WellMode>) return 1;
        if constexpr (std::is_same_v<T, DownMode>) return 2;
        if constexpr (std::is_same_v<T, PcMode>)   return 3;
        return 0;
      },
      m);
}

AIMode index_to_mode(int idx) {
  switch (idx) {
  case 0: return AtkMode{};
  case 1: return WellMode{};
  case 2: return DownMode{};
  case 3: return PcMode{};
  default: return AtkMode{};
  }
}

} // namespace

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
  int next_visible =
      std::min(static_cast<int>(state.queue.size()), queue_lookahead);
  for (int i = 0; i < next_visible; ++i)
    input.queue.push_back(state.queue[i]);
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

void AIState::on_garbage(int lines) {
  if (!active)
    return;
  // Offset remaining plan steps rather than invalidating
  for (int i = plan_.current_step; i < static_cast<int>(plan_.steps.size()); ++i)
    plan_.steps[i].placement.y += lines;
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

  const auto &queue = last_input_.queue;
  int qi = 0;
  auto hold = last_input_.hold;
  BoardBitset sim = last_input_.board;

  // Truncate PV at the known-queue boundary. Deeper PV steps come from
  // draw_from_bag speculation and commit to a specific bag ordering that
  // rarely matches the real game's RNG. Dispatching them makes the engine
  // silently drop the mismatched Place command and stalls autoplay.
  // Followup: replace the flat Plan with a tree branching on piece reveals
  // so speculative work is preserved (see wiki/ai.md §"Plan Dispatch").
  for (const auto &p : result.pv) {
    if (qi >= (int)queue.size())
      break;

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
      if (qi + 1 >= (int)queue.size())
        break;
      step.uses_hold = true;
      hold = current;
      qi += 2;
    }

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
    if (qi >= (int)queue.size())
      break;

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
      if (qi + 1 >= (int)queue.size())
        break;
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

void AIState::fill_plan_overlay(ViewModel &vm, const GameState &state) const {
  if (!plan_computed() || current_plan().complete())
    return;
  auto remaining = std::span(plan_.steps).subspan(plan_.current_step);
  BoardBitset sim = BoardBitset::from_board(state.board);
  vm.plan_overlay = build_plan_overlay(sim, remaining, max_visible);
}

void AIState::draw_sidebar(AIController &ai_ctrl) {
  if (!show_debug_window)
    return;
  if (!ImGui::CollapsingHeader("AI Debug", ImGuiTreeNodeFlags_DefaultOpen))
    return;

  if (ImGui::BeginTable("##ctrl", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Active");
    ImGui::TableNextColumn();
    if (ImGui::Checkbox("##active", &active))
      if (active) needs_search = true;

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Autoplay");
    ImGui::TableNextColumn();
    ImGui::Checkbox("##autoplay", &autoplay);

    if (autoplay) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("Speed");
      ImGui::TableNextColumn();
      int speed = 500 - ai_ctrl.interval_ms();
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (ImGui::SliderInt("##speed", &speed, 0, 500))
        ai_ctrl.set_interval_ms(500 - speed);

      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("Real movement");
      ImGui::TableNextColumn();
      bool real = ai_ctrl.input_mode() == AIInputMode::RealInputs;
      if (ImGui::Checkbox("##realmove", &real))
        ai_ctrl.set_input_mode(real ? AIInputMode::RealInputs
                                    : AIInputMode::DirectPlacement);
    }

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Preview");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderInt("##preview", &max_visible, 0, 15);

    ImGui::EndTable();
  }

  ImGui::SeparatorText("Mode");
  {
    static const char *mode_labels[] = {"ATK", "Well", "Downstack", "PC"};
    int cur = mode_to_index(mode);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##mode", &cur, mode_labels, 4)) {
      mode = index_to_mode(cur);
      if (active) needs_search = true;
    }
  }

  ImGui::SeparatorText("Search");
  if (ImGui::BeginTable("##search", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Depth");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderInt("##depth", &overrides.depth, 1, 20))
      if (active) needs_search = true;

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Beam");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderInt("##beam", &overrides.width, 1, 2000))
      if (active) needs_search = true;

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Lookahead");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderInt("##lookahead", &queue_lookahead, 1, 15))
      if (active) needs_search = true;

    ImGui::EndTable();
  }

  ImGui::Separator();
  {
    bool busy = searching();
    ImVec4 color = busy ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f)
                        : ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    ImGui::TextColored(color, "●");
    ImGui::SameLine();
  }

  if (searching()) {
    const char *what = std::holds_alternative<PcMode>(mode) &&
                               plan_source_ == PlanSource::None
                           ? "Searching (PC)..."
                           : "Searching...";
    ImGui::TextUnformatted(what);
  } else if (plan_computed()) {
    const char *tag = plan_source_ == PlanSource::PerfectClear ? " [PC]" : "";
    ImGui::Text("Plan: %d steps%s", (int)plan_.steps.size(), tag);
  } else {
    ImGui::TextUnformatted("No plan");
  }
}
