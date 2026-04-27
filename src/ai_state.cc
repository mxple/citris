#include "ai_state.h"
#include "ai/board_bitset.h"
#include "ai/plan.h"
#include "controller/ai_controller.h"
#include "engine/attack.h"
#include "render/view_model.h"

#include <algorithm>
#include <imgui.h>

namespace {

static constexpr char kPieceChar[] = "IOTSZJL";
static constexpr char kRotChar[] = "NESW";
static const char *kSpinLabel[] = {"----", "mini", "full", "all "};

int mode_to_index(const AIMode &m) {
  return std::visit(
      [](const auto &v) -> int {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, AtkMode>)
          return 0;
        if constexpr (std::is_same_v<T, WellMode>)
          return 1;
        if constexpr (std::is_same_v<T, DownMode>)
          return 2;
        if constexpr (std::is_same_v<T, PcMode>)
          return 3;
        return 0;
      },
      m);
}

AIMode index_to_mode(int idx) {
  switch (idx) {
  case 0:
    return AtkMode{};
  case 1:
    return WellMode{};
  case 2:
    return DownMode{};
  case 3:
    return PcMode{};
  default:
    return AtkMode{};
  }
}

} // namespace

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

  if (plan_commit_limit > 0 && plan_.played_count >= plan_commit_limit) {
    // Hit the commit window. Discard the speculative tail and let the
    // next search recompute from fresh information. Plan stays visible
    // until the new search lands.
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

void AIState::build_plan_from_beam(const BeamResult &result) {
  plan_ = Plan{};

  if (result.pv.empty())
    return;

  const auto &queue = last_input_.queue;
  int qi = 0;
  auto hold = last_input_.hold;
  BoardBitset sim = last_input_.board;

  // Truncate PV at the known-piece boundary. Available pieces = queue
  // (current + previews) PLUS the hold slot, so when the queue is drained
  // we still have one final ply to play whatever is in hold (the search
  // explores this — see ai.cc:351-353). Deeper PV steps come from
  // draw_from_bag speculation and commit to a specific bag ordering that
  // rarely matches the real game's RNG. Dispatching them makes the engine
  // silently drop the mismatched Place command and stalls autoplay.
  // The walk-against-queue logic below is only used for truncation; the
  // resulting (uses_hold, ordering) is *not* persisted on the Step — both
  // are re-derived per advance by check_feasibility against the real state.
  for (const auto &p : result.pv) {
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

  plan_source_ = PlanSource::BeamSearch;
}

void AIState::build_plan_from_pc(const PcResult &result) {
  plan_ = Plan{};

  if (result.solution.empty())
    return;

  const auto &queue = last_input_.queue;
  int qi = 0;
  auto hold = last_input_.hold;
  BoardBitset sim = last_input_.board;

  for (const auto &p : result.solution) {
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

void AIState::draw_ai_controls(AIController &ai_ctrl) {
  if (!ImGui::CollapsingHeader("AI", ImGuiTreeNodeFlags_DefaultOpen))
    return;

  draw_control_table(ai_ctrl);

  ImGui::SeparatorText("Mode");
  draw_mode_selector();

  ImGui::SeparatorText("Search");
  draw_search_params();

  ImGui::Separator();
  draw_search_status();

  draw_pv_evaluation();

  ImGui::SeparatorText("Player Eval");
  draw_player_eval();
}

void AIState::draw_control_table(AIController &ai_ctrl) {
  if (!ImGui::BeginTable("##ctrl", 2, ImGuiTableFlags_SizingFixedSame))
    return;

  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  ImGui::TextUnformatted("Active");
  ImGui::TableNextColumn();
  if (ImGui::Checkbox("##active", &active))
    if (active) needs_search = true;

  if (active) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Autoplay");
    ImGui::TableNextColumn();
    ImGui::Checkbox("##autoplay", &autoplay);

    if (autoplay) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("Real movement");
      ImGui::TableNextColumn();
      bool real = ai_ctrl.input_mode() == AIInputMode::RealInputs;
      if (ImGui::Checkbox("##realmove", &real))
        ai_ctrl.set_input_mode(real ? AIInputMode::RealInputs
                                    : AIInputMode::DirectPlacement);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("Speed");
      ImGui::TableNextColumn();
      int speed = 1000 - ai_ctrl.interval_ms();
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (ImGui::SliderInt("##speed", &speed, 0, 1000))
        ai_ctrl.set_interval_ms(1000 - speed);

      // Commit window: 0 means "play the entire PV" (legacy). Any positive
      // value forces a re-search after that many plan steps, discarding
      // the speculative tail of the previous PV.
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("Commit");
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::SliderInt("##commit", &plan_commit_limit, 0, 10);
    }

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Preview");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderInt("##preview", &max_visible, 0, 15);
  }
  ImGui::EndTable();
}

void AIState::draw_mode_selector() {
  static const char *mode_labels[] = {"ATK", "Well", "Downstack", "PC"};
  int cur = mode_to_index(mode);
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (ImGui::Combo("##mode", &cur, mode_labels, 4)) {
    mode = index_to_mode(cur);
    if (active)
      needs_search = true;
  }
}

void AIState::draw_search_params() {
  if (!ImGui::BeginTable("##search", 2, ImGuiTableFlags_SizingStretchProp))
    return;

  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  ImGui::TextUnformatted("Depth");
  ImGui::TableNextColumn();
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (ImGui::SliderInt("##depth", &overrides.depth, 1, 20))
    if (active)
      needs_search = true;

  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  ImGui::TextUnformatted("Beam");
  ImGui::TableNextColumn();
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (ImGui::SliderInt("##beam", &overrides.width, 1, 2000))
    if (active)
      needs_search = true;

  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  ImGui::TextUnformatted("Preview");
  ImGui::TableNextColumn();
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (ImGui::SliderInt("##preview", &queue_lookahead, 1, 15))
    if (active)
      needs_search = true;

  ImGui::EndTable();
}

void AIState::draw_search_status() {
  bool busy = searching();
  ImVec4 color =
      busy ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f) : ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
  ImGui::TextColored(color, "●");
  ImGui::SameLine();

  if (busy) {
    ImGui::TextUnformatted("Searching...");
  } else if (plan_computed()) {
    ImGui::Text("Plan: %d steps%s", (int)plan_.size(),
                plan_.feasible ? "" : " (infeasible)");
  } else {
    ImGui::TextUnformatted("No plan");
  }
}

// Replays the PV through the current mode's evaluator and prints a
// per-step breakdown. Only shown for beam results — PC plans don't
// produce a beam-comparable score. Reconstructed SearchState uses
// defaults for fields BeamInput doesn't preserve (bag_remaining,
// used_counts); the replayed values can drift from the reported score
// deep into the PV but the per-step breakdown is what you want for
// debugging why the AI chose this line.
void AIState::draw_pv_evaluation() {
  if (!plan_computed() || plan_source_ != PlanSource::BeamSearch)
    return;

  ImGui::SeparatorText("PV Evaluation");

  auto eval = make_evaluator();

  SearchState state{};
  state.board = last_input_.board;
  state.hold = last_input_.hold;
  state.hold_available = last_input_.hold_available;
  state.bag_remaining = 0x7F;
  state.queue_draws = last_input_.queue_draws;
  state.attack = last_input_.attack; // real b2b/combo at search root

  float initial_board = eval->board_eval(state);
  float cum_tact = 0.0f;
  float last_board = initial_board;

  ImGui::Text("Init board: %+.1f", initial_board);

  if (ImGui::BeginTable("##pv_eval", 5,
                        ImGuiTableFlags_Borders |
                            ImGuiTableFlags_SizingFixedFit |
                            ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("Move");
    ImGui::TableSetupColumn("Spin:L");
    ImGui::TableSetupColumn("Tact");
    ImGui::TableSetupColumn("\xce\xa3Tact"); // "ΣTact"
    ImGui::TableSetupColumn("Board");
    ImGui::TableHeadersRow();


    for (int i = 0; i < static_cast<int>(plan_.remaining.size()); ++i) {
      const auto &step = plan_.remaining[i];

      // Advance attack state on a copy so we can feed the attack value
      // into tactical_eval without mutating `state` until after the call.
      AttackState after_attack = state.attack;
      int attack_sent = compute_attack_and_update_state(
          after_attack, step.lines_cleared, step.placement.spin);

      float tact = eval->tactical_eval(step.placement, step.lines_cleared,
                                       attack_sent, state);
      cum_tact += tact;

      // Advance the reconstructed state to post-move
      state.board = step.board_after;
      state.attack = after_attack;
      state.depth = i + 1;
      state.lines_cleared += step.lines_cleared;
      state.total_attack += attack_sent;
      float board = eval->board_eval(state);
      last_board = board;

      ImGui::TableNextRow();
      if (i == 0)
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                               IM_COL32(40, 80, 120, 120));

      ImGui::TableNextColumn();
      ImGui::Text("%c %c,%d,%d",
                  kPieceChar[static_cast<int>(step.placement.type)],
                  kRotChar[static_cast<int>(step.placement.rotation)],
                  step.placement.x, step.placement.y);
      ImGui::TableNextColumn();
      ImGui::Text("%s:%d", kSpinLabel[static_cast<int>(step.placement.spin)],
                  step.lines_cleared);
      ImGui::TableNextColumn();
      ImGui::Text("%+.1f", tact);
      ImGui::TableNextColumn();
      ImGui::Text("%+.1f", cum_tact);
      ImGui::TableNextColumn();
      ImGui::Text("%+.1f", board);
    }
    ImGui::EndTable();
  }

  // Final composite = what the beam search actually compared against.
  float composite = eval->composite(last_board, cum_tact,
                                    static_cast<int>(plan_.size()));
  ImGui::Text("Composite: %+.1f  (reported: %+.1f)", composite, last_score_);
}

// Player Eval: score the current board + last locked move through the
// selected evaluator. Works whether the AI is active or not, so the user
// can test their own play against whatever eval they've tuned. Always
// visible once at least one piece has locked — `parent_board_` and
// `last_move_` persist across search invalidations.
void AIState::draw_player_eval() {
  auto eval = make_evaluator();

  // --- Board eval of current board ---
  SearchState cur_state{};
  cur_state.board = last_board_;
  cur_state.hold = last_input_.hold;
  cur_state.hold_available = last_input_.hold_available;
  cur_state.bag_remaining = 0x7F;
  cur_state.attack.b2b = tracked_b2b_;
  float cur_board_score = eval->board_eval(cur_state);
  ImGui::Text("Current board: %+.2f  (b2b=%d)", cur_board_score,
              tracked_b2b_);

  // --- Tactical eval of last move ---
  if (!has_last_move_) {
    ImGui::TextDisabled("Last move: (none yet)");
    return;
  }

  Placement p{};
  p.type = last_move_.type;
  p.rotation = last_move_.rotation;
  p.x = last_move_.x;
  p.y = last_move_.y;
  p.spin = last_move_.spin;

  // Reconstruct parent state: the board + attack state BEFORE the move
  // landed. b2b and combo come from tracked values; new_b2b is stored
  // in tracked_b2b_, parent_b2b_ snapshots the prior value.
  SearchState parent{};
  parent.board = parent_board_;
  parent.hold = last_input_.hold;
  parent.hold_available = last_input_.hold_available;
  parent.bag_remaining = 0x7F;
  parent.attack.combo = last_move_.prev_combo;
  parent.attack.b2b = parent_b2b_;

  float tact = eval->tactical_eval(p, last_move_.lines_cleared,
                                   last_move_.attack, parent);

  ImGui::Text("Last: %c @ (%d,%d,%c)  %s/%dL  atk=%d",
              kPieceChar[static_cast<int>(p.type)], p.x, p.y,
              kRotChar[static_cast<int>(p.rotation)],
              kSpinLabel[static_cast<int>(p.spin)],
              last_move_.lines_cleared, last_move_.attack);
  ImGui::Text("  tactical: %+.2f", tact);

  SearchState parent_for_board = parent;
  float parent_board_score = eval->board_eval(parent_for_board);
  float after_board_score = cur_board_score;
  ImGui::Text("  board: %+.2f \xe2\x86\x92 %+.2f  (\xce\x94=%+.2f)",
              parent_board_score, after_board_score,
              after_board_score - parent_board_score);
}
