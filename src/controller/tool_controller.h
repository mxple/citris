#pragma once

#include "ai_state.h"
#include "controller.h"
#include "presets/game_mode.h"
#include "settings.h"

// Handles undo and debug-panel toggle. AI plan overlay and debug UI live in
// AIDebugController.
class ToolController : public IController {
public:
  ToolController(const Settings &settings, const GameMode &mode, AIState &ai)
      : undo_key_(settings.undo), debug_key_(settings.debug_menu),
        undo_allowed_(mode.undo_allowed()), ai_(ai) {}

  void handle_event(const InputEvent &ev, TimePoint, const GameState &,
                    CommandBuffer &cmds) override {
    if (auto *kd = std::get_if<KeyDown>(&ev)) {
      if (kd->key == undo_key_ && undo_allowed_)
        cmds.push(cmd::Undo{});
      if (kd->key == debug_key_)
        ai_.show_debug_window = !ai_.show_debug_window;
    }
  }

  void tick(TimePoint, const GameState &, CommandBuffer &) override {}
  std::optional<TimePoint> next_deadline() const override { return std::nullopt; }
  void reset_input_state() override {}

private:
  KeyCode undo_key_;
  KeyCode debug_key_;
  bool undo_allowed_;
  AIState &ai_;
};
