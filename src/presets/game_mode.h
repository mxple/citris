#pragma once

#include "command.h"
#include "engine_event.h"
#include "engine/game_state.h"
#include "engine/piece_queue.h"
#include "engine/piece_source.h"
#include "render/view_model.h"
#include "tbp/bot.h"
#include <chrono>
#include <memory>
#include <string>

class Board;
enum class EvalType : int;

class GameMode {
public:
  virtual ~GameMode() = default;

  virtual std::string title() const { return "Freeplay"; }

  // Versus hook: if this mode represents a 1v1 match, return a fresh
  // GameMode instance for the opponent. Default returns nullptr
  // (single-player). GameManager's constructor takes this as mode2.
  virtual std::unique_ptr<GameMode>
  opponent_mode(const class Settings &) const {
    return nullptr;
  }

  // Versus hook: build the TbpBot that drives the opponent. Default falls
  // back to Citris's in-process AI in GameManager if this returns nullptr.
  virtual std::unique_ptr<tbp::TbpBot> make_opponent_bot() const {
    return nullptr;
  }

  // Versus hook, per-player: build the TbpBot for player `idx` (0 = p1,
  // 1 = p2). nullptr means the player is Human and GameManager wires a
  // PlayerController. Default defers to make_opponent_bot() for idx=1 so
  // single-opponent modes keep working.
  virtual std::unique_ptr<tbp::TbpBot> make_player_bot(int idx) const {
    return idx == 1 ? make_opponent_bot() : nullptr;
  }

  // Optional think-time rate cap for a given player's TbpController. 0 =
  // no cap. Only meaningful when make_player_bot(idx) returns non-null.
  virtual int think_time_ms(int /*idx*/) const { return 0; }

  // Tuning
  virtual std::chrono::milliseconds gravity_interval() const {
    return std::chrono::milliseconds{10000};
  }
  virtual std::chrono::milliseconds lock_delay() const {
    return std::chrono::milliseconds{5000};
  }
  virtual std::chrono::milliseconds garbage_delay() const {
    return std::chrono::milliseconds{250};
  }
  virtual int max_lock_resets() const { return 15; }
  virtual bool infinite_hold() const { return false; }
  virtual bool hold_allowed() const { return true; }
  virtual bool undo_allowed() const { return true; }
  virtual std::optional<EvalType> default_eval_type() const { return std::nullopt; }
  virtual PieceQueue create_queue(unsigned seed) const {
    return PieceQueue(std::make_unique<SevenBagSource>(seed));
  }
  // Size of the preview window Game exposes via GameState::queue. Engines
  // and controllers consume the full window; the renderer clamps to 5 for
  // display. 15 is enough for 6-line PC search.
  virtual int queue_visible() const { return 5; }
  virtual bool auto_restart() const { return false; }

  // Lifecycle
  virtual void on_start(TimePoint now) { start_time_ = now; end_time_.reset(); }
  virtual void setup_board(Board &) {}

  // Event subscribers
  virtual void on_piece_locked(const eng::PieceLocked &, const GameState &,
                               CommandBuffer &) {}
  virtual void on_undo(const GameState &) {}
  virtual void on_tick(TimePoint, const GameState &, CommandBuffer &) {}

  // View model population
  virtual void fill_hud(HudData &, const GameState &, TimePoint) {}
  virtual void fill_plan_overlay(ViewModel &, const GameState &) {}

  // Per-frame ImGui window(s) owned by the mode (e.g. debug controls).
  virtual void draw_imgui() {}
  // Return true if this mode has sidebar content to display.
  virtual bool has_sidebar() const { return false; }
  // Per-frame content drawn inside the sidebar panel (use CollapsingHeader).
  virtual void draw_sidebar() {}

  // Drain-and-clear a restart request from the mode (e.g. set by a debug
  // button). Polled by the manager next to auto_restart().
  virtual bool consume_restart_request() { return false; }

protected:
  TimePoint start_time_;
  std::optional<TimePoint> end_time_;
};
