#pragma once

#include "controller.h"
#include "ai_state.h"
#include "ai/plan_overlay.h"
#include "pc/pc_solver.h"
#include "settings.h"
#include "presets/game_mode.h"

#include <chrono>
#include <imgui.h>

class ToolController : public IController {
public:
  ToolController(const Settings &settings, const GameMode &mode, AIState &ai)
      : undo_key_(settings.undo), debug_key_(settings.debug_menu),
        undo_allowed_(mode.undo_allowed()), mode_(mode), ai_(ai) {}

  void update(const InputEvent &ev, TimePoint, const GameState &,
              CommandBuffer &cmds) override {
    if (auto *kd = std::get_if<KeyDown>(&ev)) {
      if (kd->key == undo_key_ && undo_allowed_)
        cmds.push(cmd::Undo{});

      if (kd->key == debug_key_) {
        show_window_ = !show_window_;
        if (show_window_ && !ai_.active) {
          if (auto def = mode_.default_eval_type())
            ai_.eval_type = *def;
          ai_.active = true;
          ai_.rebuild_ai();
          ai_.needs_search = true;
        }
      }
    }
  }

  void check_timers(TimePoint, const GameState &state,
                    CommandBuffer &) override {
    if (ai_.active)
      last_state_ = state;
  }

  std::optional<TimePoint> next_deadline() const override {
    return std::nullopt;
  }

  void reset_input_state() override {}

  void notify(const EngineEvent &, TimePoint) override {}

  void fill_plan_overlay(ViewModel &vm, const GameState &state) override {
    if (!ai_.active || !ai_.plan_computed || ai_.plan.complete())
      return;

    auto remaining =
        std::span(ai_.plan.steps).subspan(ai_.plan.current_step);
    BoardBitset sim = BoardBitset::from_board(state.board);
    vm.plan_overlay = build_plan_overlay(sim, remaining, ai_.max_visible);
  }

  void draw_imgui() override {
    if (!show_window_)
      return;

    ImGui::SetNextWindowSize(ImVec2(220, 0), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("AI Debug", &show_window_)) {
      // Controls
      if (ImGui::BeginTable("##ctrl", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Autoplay");
        ImGui::TableNextColumn();
        ImGui::Checkbox("##autoplay", &ai_.autoplay);

        if (ai_.autoplay) {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextUnformatted("Speed");
          ImGui::TableNextColumn();
          int speed = 500 - ai_.input_interval_ms;
          ImGui::SetNextItemWidth(-FLT_MIN);
          if (ImGui::SliderInt("##speed", &speed, 0, 500))
            ai_.input_interval_ms = 500 - speed;
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Preview");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderInt("##preview", &ai_.max_visible, 0, 8);

        ImGui::EndTable();
      }

      // Search settings
      ImGui::SeparatorText("Search Settings");
      if (ImGui::BeginTable("##search", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Eval");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ai_.custom_evaluator) {
          ImGui::TextUnformatted("Custom");
        } else {
          static const char *labels[] = {"TSD", "Sprint", "Cheese", "Default"};
          int current = static_cast<int>(ai_.eval_type);
          if (ImGui::Combo("##eval", &current, labels, 4)) {
            ai_.eval_type = static_cast<EvalType>(current);
            ai_.rebuild_ai();
            ai_.needs_search = true;
          }
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Depth");
        ImGui::TableNextColumn();
        int depth = ai_.override_max_depth.value_or(12);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::SliderInt("##depth", &depth, 0, 20)) {
          ai_.override_max_depth = depth;
          ai_.needs_search = true;
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Beam");
        ImGui::TableNextColumn();
        int beam = ai_.override_beam_width.value_or(800);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::SliderInt("##beam", &beam, 0, 2000)) {
          ai_.override_beam_width = beam;
          ai_.needs_search = true;
        }

        ImGui::EndTable();
      }

      // Debug info
      ImGui::Separator();
      {
        bool busy = ai_.searching();
        ImVec4 color = busy ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f)
                            : ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
        ImGui::TextColored(color, "●");
        ImGui::SameLine();
      }
      if (ai_.searching())
        ImGui::TextUnformatted("Searching...");
      else if (ai_.plan_computed)
        ImGui::Text("Plan: %d steps", (int)ai_.plan.steps.size());
      else
        ImGui::TextUnformatted("No plan");

      auto eval = ai_.make_evaluator();
      BoardBitset bb = BoardBitset::from_board(last_state_.board);
      float board_score = eval->board_eval(bb);

      ImGui::Separator();
      ImGui::Text("Board eval: %.1f", board_score);

      if (ai_.plan_computed && !ai_.plan.steps.empty()) {
        ImGui::Text("Best score: %.1f", ai_.last_result.score);
        ImGui::Text("Search depth: %d", ai_.last_depth);

        if (!ai_.last_result.root_scores.empty()) {
          ImGui::Separator();
          ImGui::TextUnformatted("Top moves:");
          static const char *piece_names[] = {"I", "O", "T", "S",
                                              "Z", "J", "L"};
          static const char *rot_names[] = {"N", "E", "S", "W"};
          int shown = std::min((int)ai_.last_result.root_scores.size(), 5);
          for (int i = 0; i < shown; ++i) {
            auto &[pl, sc] = ai_.last_result.root_scores[i];
            ImGui::Text(" %d. %s-%s (%d,%d) %.1f", i + 1,
                        piece_names[static_cast<int>(pl.type)],
                        rot_names[static_cast<int>(pl.rotation)],
                        pl.x, pl.y, sc);
          }
        }
      }
    }
    ImGui::End();
  }

private:
  void try_pc_solve() {
    BoardBitset bb = BoardBitset::from_board(last_state_.board);

    std::vector<PieceType> queue;
    queue.push_back(last_state_.current_piece.type);
    for (auto &p : last_state_.preview)
      queue.push_back(p);

    PcConfig cfg;
    cfg.use_hold = true;
    cfg.max_pieces = static_cast<int>(queue.size());

    auto t0 = std::chrono::steady_clock::now();
    auto result = find_perfect_clear(bb, queue, last_state_.hold_piece, cfg);
    auto dt = std::chrono::steady_clock::now() - t0;
    auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(dt).count();

    if (!result.found) {
      pc_status_ = "No PC found (" + std::to_string(ms) + "ms)";
      return;
    }

    // Build plan with hold tracking.
    ai_.plan = Plan{};
    ai_.plan.current_step = 0;

    int qi = 0;
    auto hold = last_state_.hold_piece;
    BoardBitset sim = bb;

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
        // Hold was empty: hold current, place next from queue.
        step.uses_hold = true;
        hold = current;
        qi += 2;
      }

      sim.place(p.type, p.rotation, p.x, p.y);
      step.lines_cleared = sim.clear_lines();
      step.board_after = sim;
      ai_.plan.steps.push_back(std::move(step));
    }

    ai_.plan_computed = true;
    pc_status_ = "PC: " + std::to_string(result.solution.size()) +
                 " pieces (" + std::to_string(ms) + "ms)";
  }

  KeyCode undo_key_;
  KeyCode debug_key_;
  bool undo_allowed_;
  bool show_window_ = false;
  const GameMode &mode_;
  AIState &ai_;
  GameState last_state_;
  std::string pc_status_;
};
