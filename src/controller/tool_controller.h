#pragma once

#include "controller.h"
#include "presets/game_mode.h"
#include "settings.h"

class ToolController : public IController {
public:
  ToolController(const Settings &settings, const GameMode &mode)
      : undo_key_(settings.undo), undo_allowed_(mode.undo_allowed()) {}

  void update(const InputEvent &ev, TimePoint, const GameState &,
              CommandBuffer &cmds) override {
    if (auto *kd = std::get_if<KeyDown>(&ev)) {
      if (kd->key == undo_key_ && undo_allowed_)
        cmds.push(cmd::Undo{});
    }
  }

  void check_timers(TimePoint, CommandBuffer &) override {}
  std::optional<TimePoint> next_deadline() const override {
    return std::nullopt;
  }
  void reset_input_state() override {}

private:
  sf::Keyboard::Key undo_key_;
  bool undo_allowed_;
};
