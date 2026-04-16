#pragma once

#include "ai/ai_mode.h"
#include "ai/plan_overlay.h"
#include "ai_state.h"
#include "controller.h"
#include "presets/game_mode.h"
#include "settings.h"

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
            ai_.mode = eval_type_to_mode(*def);
          ai_.active = true;
          ai_.needs_search = true;
        }
      }
    }
  }

  void check_timers(TimePoint, const GameState &state,
                    CommandBuffer &) override {
    last_state_ = state;
  }

  std::optional<TimePoint> next_deadline() const override {
    return std::nullopt;
  }

  void reset_input_state() override {}

  void notify(const EngineEvent &, TimePoint) override {}

  void fill_plan_overlay(ViewModel &vm, const GameState &state) override {
    if (!ai_.plan_computed() || ai_.current_plan().complete())
      return;
    const auto &plan = ai_.current_plan();
    auto remaining = std::span(plan.steps).subspan(plan.current_step);
    BoardBitset sim = BoardBitset::from_board(state.board);
    vm.plan_overlay = build_plan_overlay(sim, remaining, ai_.max_visible);
  }

  void draw_imgui() override {
    if (!show_window_)
      return;

    ImGui::SetNextWindowSize(ImVec2(240, 0), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("AI Debug", &show_window_)) {
      // Active / autoplay controls
      if (ImGui::BeginTable("##ctrl", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Active");
        ImGui::TableNextColumn();
        if (ImGui::Checkbox("##active", &ai_.active))
          if (ai_.active) ai_.needs_search = true;

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
        ImGui::SliderInt("##preview", &ai_.max_visible, 0, 15);

        ImGui::EndTable();
      }

      // Mode selector
      ImGui::SeparatorText("Mode");
      {
        static const char *mode_labels[] = {"ATK", "Well", "Downstack", "PC"};
        int cur = mode_to_index(ai_.mode);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##mode", &cur, mode_labels, 4)) {
          ai_.mode = index_to_mode(cur);
          if (ai_.active) ai_.needs_search = true;
        }
      }

      // Beam search settings (shared across all modes)
      ImGui::SeparatorText("Search");
      if (ImGui::BeginTable("##search", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Depth");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::SliderInt("##depth", &ai_.overrides.depth, 1, 20))
          if (ai_.active) ai_.needs_search = true;

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Beam");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::SliderInt("##beam", &ai_.overrides.width, 1, 2000))
          if (ai_.active) ai_.needs_search = true;

        ImGui::EndTable();
      }

      // Status
      ImGui::Separator();
      {
        bool busy = ai_.searching();
        ImVec4 color = busy ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f)
                            : ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
        ImGui::TextColored(color, "●");
        ImGui::SameLine();
      }

      PlanSource src = ai_.plan_source();
      if (ai_.searching()) {
        const char *what = std::holds_alternative<PcMode>(ai_.mode) &&
                                   src == PlanSource::None
                               ? "Searching (PC)..."
                               : "Searching...";
        ImGui::TextUnformatted(what);
      } else if (ai_.plan_computed()) {
        const char *tag = src == PlanSource::PerfectClear ? " [PC]" : "";
        ImGui::Text("Plan: %d steps%s",
                    (int)ai_.current_plan().steps.size(), tag);
      } else {
        ImGui::TextUnformatted("No plan");
      }

      auto eval = ai_.make_evaluator();
      BoardBitset bb = BoardBitset::from_board(last_state_.board);
      ImGui::Text("Board eval: %.1f", eval->board_eval(bb));

      if (ai_.plan_computed()) {
        ImGui::Text("Best score: %.1f", ai_.last_score());
        ImGui::Text("Search depth: %d", ai_.last_depth());

        if (!ai_.root_scores().empty()) {
          ImGui::Separator();
          ImGui::TextUnformatted("Top moves:");
          static const char *piece_names[] = {"I", "O", "T", "S",
                                              "Z", "J", "L"};
          static const char *rot_names[] = {"N", "E", "S", "W"};
          int shown = std::min((int)ai_.root_scores().size(), 5);
          for (int i = 0; i < shown; ++i) {
            auto &[pl, sc] = ai_.root_scores()[i];
            ImGui::Text(" %d. %s-%s (%d,%d) %.1f", i + 1,
                        piece_names[static_cast<int>(pl.type)],
                        rot_names[static_cast<int>(pl.rotation)], pl.x, pl.y,
                        sc);
          }
        }
      }
    }
    ImGui::End();
  }

private:
  static int mode_to_index(const AIMode &m) {
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

  static AIMode index_to_mode(int idx) {
    switch (idx) {
    case 0: return AtkMode{};
    case 1: return WellMode{};
    case 2: return DownMode{};
    case 3: return PcMode{};
    default: return AtkMode{};
    }
  }

  static AIMode eval_type_to_mode(EvalType t) {
    switch (t) {
    case EvalType::Tsd:     return AtkMode{};
    case EvalType::Sprint:  return WellMode{};
    case EvalType::Cheese:  return DownMode{};
    case EvalType::Default: return AtkMode{};
    }
    return AtkMode{};
  }

  KeyCode undo_key_;
  KeyCode debug_key_;
  bool undo_allowed_;
  bool show_window_ = false;
  const GameMode &mode_;
  AIState &ai_;
  GameState last_state_;
};
