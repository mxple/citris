#pragma once

#include "ai_state.h"
#include "controller.h"
#include "debug_state.h"
#include "presets/game_mode.h"
#include "settings.h"

// Owns the F3 debug-panel state and the auxiliary "tools" inputs (undo).
// On each tick it drains any pending debug actions (queue edits, hold
// clear) into the command buffer — keeping debug-state lifecycle, the F3
// toggle, and the engine-command emission all in one place rather than
// scattered across GameManager.
class ToolController : public IController {
public:
  ToolController(const Settings &settings, const GameMode &mode, AIState &ai)
      : undo_key_(settings.undo), debug_key_(settings.debug_menu),
        undo_allowed_(mode.undo_allowed()), ai_(ai) {}

  DebugState &debug() { return debug_; }
  const DebugState &debug() const { return debug_; }

  void handle_event(const InputEvent &ev, TimePoint, const GameState &,
                    CommandBuffer &cmds) override {
    if (auto *kd = std::get_if<KeyDown>(&ev)) {
      if (kd->key == undo_key_ && undo_allowed_)
        cmds.push(cmd::Undo{});
      if (kd->key == debug_key_)
        debug_.open = !debug_.open;
    }
  }

  void tick(TimePoint, const GameState &, CommandBuffer &cmds) override {
    if (debug_.clear_hold) {
      cmds.push(cmd::ClearHold{});
      debug_.clear_hold = false;
    }
    if (debug_.pending_queue_replacement) {
      auto pieces = std::move(*debug_.pending_queue_replacement);
      debug_.pending_queue_replacement.reset();
      if (!pieces.empty()) {
        cmds.push(cmd::ReplaceCurrentPiece{pieces.front()});
        if (pieces.size() > 1) {
          std::vector<PieceType> rest(pieces.begin() + 1, pieces.end());
          cmds.push(cmd::ReplaceQueuePrefix{std::move(rest)});
        }
        if (ai_.active)
          ai_.needs_search = true;
      }
    }
  }

  std::optional<TimePoint> next_deadline() const override { return std::nullopt; }
  void reset() override {}

private:
  KeyCode undo_key_;
  KeyCode debug_key_;
  bool undo_allowed_;
  AIState &ai_;
  DebugState debug_;
};
