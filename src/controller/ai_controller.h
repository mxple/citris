#pragma once

#include "ai/pathfinder.h"
#include "ai/placement.h"
#include "controller.h"
#include <chrono>

enum class AIInputMode {
  RealInputs,       // pathfind internally, replay GameInput path
  DirectPlacement,   // cmd::Place — skip input simulation entirely
};

class AIController : public IController {
public:
  void set_input_mode(AIInputMode mode) { input_mode_ = mode; }
  AIInputMode input_mode() const { return input_mode_; }

  int interval_ms() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(interval_)
        .count();
  }
  void set_interval_ms(int ms) {
    interval_ = std::chrono::duration_cast<Duration>(
        std::chrono::milliseconds(ms));
  }

  void set_placement(Placement placement, bool uses_hold) {
    pending_placement_ = PlacementAction{placement, uses_hold};
    path_.clear();
    path_idx_ = 0;
  }

  bool idle() const {
    if (pending_placement_)
      return false;
    return path_idx_ >= static_cast<int>(path_.size());
  }

  void handle_event(const InputEvent &, TimePoint, const GameState &,
                    CommandBuffer &) override {}

  void tick(TimePoint now, const GameState &state,
            CommandBuffer &cmds) override {
    while (now >= next_input_time_) {
      next_input_time_ += interval_;
      if (next_input_time_ < now)
        next_input_time_ = now + interval_;

      if (pending_placement_)
        resolve_placement(state, cmds);

      if (!emit_next(cmds))
        break;
    }
  }

  std::optional<TimePoint> next_deadline() const override {
    if (!idle())
      return next_input_time_;
    return std::nullopt;
  }

  void reset_input_state() override {
    path_.clear();
    path_idx_ = 0;
    pending_placement_.reset();
  }

  void notify(const EngineEvent &ev, TimePoint) override {
    if (std::holds_alternative<eng::PieceLocked>(ev) ||
        std::holds_alternative<eng::UndoPerformed>(ev))
      reset_input_state();
    if (auto *gm = std::get_if<eng::GarbageMaterialized>(&ev)) {
      if (pending_placement_)
        pending_placement_->placement.y += gm->lines;
    }
  }

private:
  void resolve_placement(const GameState &state, CommandBuffer &cmds) {
    auto action = *pending_placement_;
    pending_placement_.reset();

    if (input_mode_ == AIInputMode::DirectPlacement) {
      if (action.uses_hold)
        cmds.push(cmd::MovePiece{GameInput::Hold});
      cmds.push(cmd::Place{action.placement});
      return;
    }

    // RealInputs: pathfind and queue for step-by-step replay
    if (action.uses_hold) {
      path_.push_back(GameInput::Hold);
      auto spawn = spawn_position(action.placement.type);
      auto moves = find_path(state.board, action.placement, spawn.x, spawn.y,
                             Rotation::North);
      path_.insert(path_.end(), moves.begin(), moves.end());
    } else {
      auto &piece = state.current_piece;
      path_ = find_path(state.board, action.placement, piece.x, piece.y,
                        piece.rotation);
    }
    path_idx_ = 0;
  }

  // Returns true if an input was emitted (caller should keep looping).
  bool emit_next(CommandBuffer &cmds) {
    if (path_idx_ >= static_cast<int>(path_.size()))
      return false;
    cmds.push(cmd::MovePiece{path_[path_idx_++]});
    return true;
  }

  struct PlacementAction {
    Placement placement;
    bool uses_hold = false;
  };

  AIInputMode input_mode_ = AIInputMode::DirectPlacement;
  std::optional<PlacementAction> pending_placement_;
  std::vector<GameInput> path_;
  int path_idx_ = 0;
  TimePoint next_input_time_{};
  Duration interval_{std::chrono::duration_cast<Duration>(
      std::chrono::milliseconds(250))};
};
