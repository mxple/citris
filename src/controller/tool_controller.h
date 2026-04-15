#pragma once

#include "controller.h"
#include "ai_state.h"
#include "ai/plan_overlay.h"
#include "settings.h"
#include "presets/game_mode.h"

#include <imgui.h>

class ToolController : public IController {
public:
  ToolController(const Settings &settings, const GameMode &mode, AIState &ai)
      : undo_key_(settings.undo), undo_allowed_(mode.undo_allowed()),
        ai_(ai) {}

  void update(const InputEvent &ev, TimePoint, const GameState &,
              CommandBuffer &cmds) override {
    if (auto *kd = std::get_if<KeyDown>(&ev)) {
      if (kd->key == undo_key_ && undo_allowed_)
        cmds.push(cmd::Undo{});

      if (kd->key == SDLK_F3) {
        ai_.active = !ai_.active;
        if (ai_.active) {
          ai_.rebuild_ai();
          ai_.needs_search = true;
        } else {
          ai_.deactivate();
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
    if (!ai_.active)
      return;

    ImGui::SetNextWindowSize(ImVec2(220, 0), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("AI Debug", &ai_.active)) {
      static const char *labels[] = {"TSD", "Sprint", "Cheese", "Default"};
      int current = static_cast<int>(ai_.eval_type);
      if (ImGui::Combo("Evaluator", &current, labels, 4)) {
        ai_.eval_type = static_cast<AIState::EvalType>(current);
        ai_.rebuild_ai();
        ai_.needs_search = true;
      }

      ImGui::Checkbox("Autoplay", &ai_.autoplay);
      if (ai_.autoplay)
        ImGui::SliderInt("Input ms", &ai_.input_interval_ms, 0, 500);

      if (ImGui::SliderInt("Show pieces", &ai_.max_visible, 1, 14))
        ;

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

      // Eval debug: score breakdown for current board
      auto eval = ai_.make_evaluator();
      BoardBitset bb = BoardBitset::from_board(last_state_.board);
      float board_score = eval->board_eval(bb);

      ImGui::Separator();
      ImGui::Text("Board eval: %.1f", board_score);

      // Show search result details
      if (ai_.plan_computed && !ai_.plan.steps.empty()) {
        ImGui::Text("Best score: %.1f", ai_.last_result.score);
        ImGui::Text("Search depth: %d", ai_.last_depth);

        // Top root moves
        if (!ai_.last_result.root_scores.empty()) {
          ImGui::Separator();
          ImGui::TextUnformatted("Top moves:");
          static const char *piece_names[] = {"I", "O", "T", "S",
                                              "Z", "J", "L"};
          int shown = std::min((int)ai_.last_result.root_scores.size(), 5);
          for (int i = 0; i < shown; ++i) {
            auto &[pl, sc] = ai_.last_result.root_scores[i];
            ImGui::Text(" %d. %s (%d,%d) %.1f", i + 1,
                        piece_names[static_cast<int>(pl.type)], pl.x, pl.y,
                        sc);
          }
        }
      }
    }
    ImGui::End();
  }

private:
  KeyCode undo_key_;
  bool undo_allowed_;
  AIState &ai_;
  GameState last_state_;
};
