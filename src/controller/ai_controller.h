#pragma once

#include "controller.h"
#include <chrono>

class AIController : public IController {
public:
  void set_path(std::vector<GameInput> path, TimePoint now,
                int interval_ms) {
    path_ = std::move(path);
    path_idx_ = 0;
    next_input_time_ = now;
    interval_ = std::chrono::duration_cast<Duration>(
        std::chrono::milliseconds(interval_ms));
  }

  bool idle() const { return path_idx_ >= static_cast<int>(path_.size()); }

  void update(const InputEvent &, TimePoint, const GameState &,
              CommandBuffer &) override {}

  void check_timers(TimePoint now, const GameState &,
                    CommandBuffer &cmds) override {
    if (idle() || now < next_input_time_)
      return;
    cmds.push(cmd::MovePiece{path_[path_idx_++]});
    next_input_time_ = now + interval_;
  }

  std::optional<TimePoint> next_deadline() const override {
    if (!idle())
      return next_input_time_;
    return std::nullopt;
  }

  void reset_input_state() override {
    path_.clear();
    path_idx_ = 0;
  }

  void notify(const EngineEvent &ev, TimePoint) override {
    if (std::holds_alternative<eng::PieceLocked>(ev) ||
        std::holds_alternative<eng::UndoPerformed>(ev))
      reset_input_state();
  }

private:
  std::vector<GameInput> path_;
  int path_idx_ = 0;
  TimePoint next_input_time_{};
  Duration interval_{std::chrono::duration_cast<Duration>(
      std::chrono::milliseconds(250))};
};
