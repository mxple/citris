#pragma once

// IController that drives a Game from any TbpBot (internal or external).
//
// On first tick: bot.start() is called with a snapshot of the current state.
// On each subsequent tick: poll_suggestion(); on a non-empty suggestion,
// push a cmd::Place to the game's command buffer. If the bot's suggested
// piece type differs from current_piece.type, a cmd::MovePiece{Input::Hold}
// is emitted first — Citris's Game::handle_place() silently drops the
// placement otherwise (game.cc:219).
//
// On PieceLocked: bot.play() with the locked placement. The bot advances
// its own state. Then for each newly-visible queue piece (detected via
// state.queue_draws + state.queue.size()), bot.new_piece() is sent.
//
// The controller ignores input events (bot-only) and has no deadline.

#include "controller/controller.h"
#include "engine/piece.h"
#include "tbp/bot.h"

#include <cstdint>
#include <memory>
#include <optional>

class TbpController : public IController {
public:
  explicit TbpController(std::unique_ptr<tbp::TbpBot> bot);
  ~TbpController() override;

  // IController
  void handle_event(const InputEvent &, TimePoint, const GameState &,
                    CommandBuffer &) override {}
  void tick(TimePoint now, const GameState &state,
            CommandBuffer &cmds) override;
  std::optional<TimePoint> next_deadline() const override {
    return std::nullopt;
  }
  void reset_input_state() override;
  void notify(const EngineEvent &ev, TimePoint now) override;

  // For diagnostics / inspection.
  const tbp::TbpBot &bot() const { return *bot_; }

private:
  void send_start(const GameState &state);
  // Diff state.queue vs the bot's known queue tail, emit new_piece for each
  // newly-visible piece, and advance bot_queue_tail_idx_.
  void catch_up_queue(const GameState &state);

  std::unique_ptr<tbp::TbpBot> bot_;
  bool started_ = false;
  // Absolute piece index (using state.queue_draws as the base) of the
  // furthest piece we've told the bot about. Sentinel -1 means "nothing
  // told yet".
  int64_t bot_queue_tail_idx_ = -1;
  // Cached piece type of the suggestion we emitted a cmd::Place for, awaiting
  // the PieceLocked event. Used only for sanity / diagnostics today.
  std::optional<PieceType> pending_placed_type_;
};
