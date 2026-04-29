#include "ui/ai_debug_panel.h"
#include "ai_state.h"
#include "ai/beam_search.h"
#include "ai/plan.h"
#include "controller/ai_controller.h"
#include "engine/attack.h"

#include <imgui.h>

namespace {

static constexpr char kPieceChar[] = "IOTSZJL";
static constexpr char kRotChar[] = "NESW";
static const char *kSpinLabel[] = {"----", "mini", "full", "all "};

void draw_control_table(AIState &ai, AIController &ai_ctrl) {
  if (!ImGui::BeginTable("##ctrl", 2, ImGuiTableFlags_SizingFixedSame))
    return;

  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  ImGui::TextUnformatted("Active");
  ImGui::TableNextColumn();
  if (ImGui::Checkbox("##active", &ai.active))
    if (ai.active) ai.needs_search = true;

  if (ai.active) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Autoplay");
    ImGui::TableNextColumn();
    ImGui::Checkbox("##autoplay", &ai.autoplay);

    if (ai.autoplay) {
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

      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("Commit");
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::SliderInt("##commit", &ai.plan_commit_limit, 0, 10);
    }

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Preview");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderInt("##preview", &ai.max_visible, 0, 15);
  }
  ImGui::EndTable();
}

void draw_mode_selector(AIState &ai) {
  static const char *mode_labels[] = {"ATK", "Well", "Downstack", "PC"};
  int cur = static_cast<int>(ai.mode);
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (ImGui::Combo("##mode", &cur, mode_labels, 4)) {
    ai.mode = static_cast<AIMode>(cur);
    if (ai.active)
      ai.needs_search = true;
  }
}

void draw_search_params(AIState &ai) {
  if (!ImGui::BeginTable("##search", 2, ImGuiTableFlags_SizingStretchProp))
    return;

  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  ImGui::TextUnformatted("Depth");
  ImGui::TableNextColumn();
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (ImGui::SliderInt("##depth", &ai.overrides.depth, 1, 20))
    if (ai.active)
      ai.needs_search = true;

  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  ImGui::TextUnformatted("Beam");
  ImGui::TableNextColumn();
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (ImGui::SliderInt("##beam", &ai.overrides.width, 1, 2000))
    if (ai.active)
      ai.needs_search = true;

  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  ImGui::TextUnformatted("Preview");
  ImGui::TableNextColumn();
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (ImGui::SliderInt("##preview", &ai.queue_lookahead, 1, 15))
    if (ai.active)
      ai.needs_search = true;

  ImGui::EndTable();
}

void draw_search_status(AIState &ai) {
  bool busy = ai.searching();
  ImVec4 color =
      busy ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f) : ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
  ImGui::TextColored(color, "●");
  ImGui::SameLine();

  if (busy) {
    ImGui::TextUnformatted("Searching...");
  } else if (ai.plan_computed()) {
    ImGui::Text("Plan: %d steps%s", (int)ai.current_plan().size(),
                ai.current_plan().feasible ? "" : " (infeasible)");
  } else {
    ImGui::TextUnformatted("No plan");
  }
}

void draw_pv_evaluation(AIState &ai) {
  if (!ai.plan_computed() || ai.plan_source() != PlanSource::BeamSearch)
    return;

  ImGui::SeparatorText("PV Evaluation");

  auto eval = ai.make_evaluator();
  const auto &input = ai.last_input();
  const auto &plan = ai.current_plan();

  SearchState state{};
  state.board = input.board;
  state.hold = input.hold;
  state.hold_available = input.hold_available;
  state.bag_remaining = 0x7F;
  state.queue_draws = input.queue_draws;
  state.attack = input.attack;

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
    ImGui::TableSetupColumn("\xce\xa3Tact");
    ImGui::TableSetupColumn("Board");
    ImGui::TableHeadersRow();

    for (int i = 0; i < static_cast<int>(plan.remaining.size()); ++i) {
      const auto &step = plan.remaining[i];

      AttackState after_attack = state.attack;
      int attack_sent = compute_attack_and_update_state(
          after_attack, step.lines_cleared, step.placement.spin);

      float tact = eval->tactical_eval(step.placement, step.lines_cleared,
                                       attack_sent, state);
      cum_tact += tact;

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

  float composite = eval->composite(last_board, cum_tact,
                                    static_cast<int>(plan.size()));
  ImGui::Text("Composite: %+.1f  (reported: %+.1f)", composite,
              ai.last_score());
}

void draw_player_eval(AIState &ai) {
  auto eval = ai.make_evaluator();
  const auto &input = ai.last_input();

  SearchState cur_state{};
  cur_state.board = ai.last_board();
  cur_state.hold = input.hold;
  cur_state.hold_available = input.hold_available;
  cur_state.bag_remaining = 0x7F;
  cur_state.attack.b2b = ai.tracked_b2b();
  float cur_board_score = eval->board_eval(cur_state);
  ImGui::Text("Current board: %+.2f  (b2b=%d)", cur_board_score,
              ai.tracked_b2b());

  if (!ai.has_last_move()) {
    ImGui::TextDisabled("Last move: (none yet)");
    return;
  }

  const auto &lm = ai.last_move();
  Placement p{};
  p.type = lm.type;
  p.rotation = lm.rotation;
  p.x = lm.x;
  p.y = lm.y;
  p.spin = lm.spin;

  SearchState parent{};
  parent.board = ai.parent_board();
  parent.hold = input.hold;
  parent.hold_available = input.hold_available;
  parent.bag_remaining = 0x7F;
  parent.attack.combo = lm.prev_combo;
  parent.attack.b2b = ai.parent_b2b();

  float tact = eval->tactical_eval(p, lm.lines_cleared, lm.attack, parent);

  ImGui::Text("Last: %c @ (%d,%d,%c)  %s/%dL  atk=%d",
              kPieceChar[static_cast<int>(p.type)], p.x, p.y,
              kRotChar[static_cast<int>(p.rotation)],
              kSpinLabel[static_cast<int>(p.spin)],
              lm.lines_cleared, lm.attack);
  ImGui::Text("  tactical: %+.2f", tact);

  SearchState parent_for_board = parent;
  float parent_board_score = eval->board_eval(parent_for_board);
  float after_board_score = cur_board_score;
  ImGui::Text("  board: %+.2f \xe2\x86\x92 %+.2f  (\xce\x94=%+.2f)",
              parent_board_score, after_board_score,
              after_board_score - parent_board_score);
}

} // namespace

void draw_ai_controls(AIState &ai, AIController &ai_ctrl) {
  if (!ImGui::CollapsingHeader("AI", ImGuiTreeNodeFlags_DefaultOpen))
    return;

  draw_control_table(ai, ai_ctrl);

  ImGui::SeparatorText("Mode");
  draw_mode_selector(ai);

  ImGui::SeparatorText("Search");
  draw_search_params(ai);

  ImGui::Separator();
  draw_search_status(ai);

  draw_pv_evaluation(ai);

  ImGui::SeparatorText("Player Eval");
  draw_player_eval(ai);
}
