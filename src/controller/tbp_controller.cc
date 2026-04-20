#include "controller/tbp_controller.h"

#include "ai/placement.h"
#include "engine_event.h"
#include "tbp/conversions.h"
#include "tbp/types.h"

#include <variant>

TbpController::TbpController(std::unique_ptr<tbp::TbpBot> bot)
    : bot_(std::move(bot)) {
  // Accept rules optimistically. InternalTbpBot always returns Ready; for
  // external bots the caller is expected to verify this before wiring in.
  (void)bot_->rules(tbp::Rules{});
}

TbpController::~TbpController() {
  if (bot_) bot_->quit();
}

void TbpController::reset_input_state() {
  // Mark for re-start on next tick. The bot retains its search thread and
  // will cancel + relaunch when start() is called with the new position.
  started_ = false;
  bot_queue_tail_idx_ = -1;
  pending_placed_type_.reset();
}

void TbpController::tick(TimePoint now, const GameState &state,
                         CommandBuffer &cmds) {
  (void)now;
  if (state.game_over) return;

  if (!started_) {
    send_start(state);
    started_ = true;
    return; // give the search a tick to begin
  }

  catch_up_queue(state);

  if (pending_placed_type_) return; // waiting for the Game to lock our piece

  auto sug = bot_->poll_suggestion();
  if (!sug) return;
  if (sug->moves.empty()) return; // bot forfeits — just don't act

  const auto &move = sug->moves.front();
  auto placed_type = tbp::piece_type_from_str(move.location.type);
  if (!placed_type) return;

  // Hold inference: if the bot's chosen piece type differs from the current
  // piece in play, emit Input::Hold first. The Game will swap (or pop from
  // queue into hold if empty), and the next piece will match the placement.
  if (*placed_type != state.current_piece.type) {
    if (!state.hold_available) {
      // Can't hold right now — drop the suggestion and let the bot re-search
      // on the next tick.
      return;
    }
    cmds.push(cmd::MovePiece{Input::Hold});
  }

  Placement p = tbp::location_to_placement(move.location,
                                           tbp::spin_from_str(move.spin));
  cmds.push(cmd::Place{p});
  pending_placed_type_ = *placed_type;
}

void TbpController::notify(const EngineEvent &ev, TimePoint now) {
  (void)now;
  if (auto *pl = std::get_if<eng::PieceLocked>(&ev)) {
    // Tell the bot the move was played.
    Placement placed{pl->type, pl->rotation, pl->x, pl->y, pl->spin};
    tbp::Move m;
    m.location = tbp::placement_to_location(placed);
    m.spin = tbp::spin_to_str(pl->spin);
    bot_->play(tbp::Play{m});
    pending_placed_type_.reset();
    // The new_piece catch-up happens on the next tick (we don't have state
    // here). This is fine — the bot just started searching again and our
    // poll on the next tick will either get a result or nullopt.
  } else if (std::holds_alternative<eng::GameOver>(ev)) {
    bot_->stop();
  }
}

void TbpController::send_start(const GameState &state) {
  tbp::Start s;
  // TBP queue[0] = current piece in play; Citris tracks current separately.
  s.queue.reserve(state.queue.size() + 1);
  s.queue.push_back(tbp::piece_type_to_str(state.current_piece.type));
  for (auto p : state.queue)
    s.queue.push_back(tbp::piece_type_to_str(p));
  if (state.hold_piece)
    s.hold = std::string(tbp::piece_type_to_str(*state.hold_piece));
  s.combo = state.attack_state.combo;
  s.back_to_back = state.attack_state.b2b > 0;
  s.board = tbp::board_to_tbp(state.board);
  bot_->start(s);

  // Furthest absolute index covered by start(): current_piece_abs_idx is
  // state.queue_draws - 1 (the piece that was popped); the last queue item's
  // abs idx is state.queue_draws + state.queue.size() - 1.
  bot_queue_tail_idx_ = static_cast<int64_t>(state.queue_draws) +
                         static_cast<int64_t>(state.queue.size()) - 1;
}

void TbpController::catch_up_queue(const GameState &state) {
  int64_t tail_now = static_cast<int64_t>(state.queue_draws) +
                     static_cast<int64_t>(state.queue.size()) - 1;
  if (tail_now <= bot_queue_tail_idx_) return;
  for (int64_t idx = bot_queue_tail_idx_ + 1; idx <= tail_now; ++idx) {
    int pos = static_cast<int>(idx - static_cast<int64_t>(state.queue_draws));
    if (pos < 0 || pos >= static_cast<int>(state.queue.size())) continue;
    bot_->new_piece(tbp::NewPiece{
        std::string(tbp::piece_type_to_str(state.queue[pos]))});
  }
  bot_queue_tail_idx_ = tail_now;
}
