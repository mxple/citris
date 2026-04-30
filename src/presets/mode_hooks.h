#pragma once

#include "command.h"
#include "engine/game_state.h"
#include "engine_event.h"
#include "notification.h"
#include "render/view_model.h"
#include "tbp/bot.h"
#include <memory>
#include <optional>
#include <string>

class Board;
class GameMode;
enum class EvalType : int;

class ModeHooks {
public:
  virtual ~ModeHooks() = default;

  virtual std::string title() const { return "Freeplay"; }

  // Versus hooks
  virtual std::unique_ptr<GameMode>
  opponent_mode(const class Settings &) const;
  virtual std::unique_ptr<tbp::TbpBot> make_opponent_bot() const {
    return nullptr;
  }
  virtual std::unique_ptr<tbp::TbpBot> make_player_bot(int idx) const {
    return idx == 1 ? make_opponent_bot() : nullptr;
  }
  virtual int think_time_ms(int) const { return 0; }

  // Lifecycle
  virtual void on_start(TimePoint now) {
    start_time_ = now;
    end_time_.reset();
  }

  // Event subscribers
  virtual void on_piece_locked(const eng::PieceLocked &, const GameState &,
                               CommandBuffer &) {}
  virtual void on_undo(const GameState &) {}
  virtual void on_tick(TimePoint, const GameState &, CommandBuffer &) {}
  virtual void on_input_registered(GameInput, const GameState &) {}

  // View model population
  virtual void fill_hud(HudData &, const GameState &, TimePoint) {}
  virtual void fill_plan_overlay(ViewModel &, const GameState &) {}

  // ImGui
  virtual void draw_imgui() {}
  virtual bool has_sidebar() const { return false; }
  virtual void draw_sidebar() {}

  virtual bool consume_restart_request() { return false; }
  virtual std::optional<EvalType> default_eval_type() const {
    return std::nullopt;
  }

protected:
  TimePoint start_time_;
  std::optional<TimePoint> end_time_;
};
