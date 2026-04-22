#include "controller/tbp_controller.h"

#include "ai/board_bitset.h"
#include "ai/placement.h"
#include "ai/plan.h"
#include "ai/plan_overlay.h"
#include "engine_event.h"
#include "game_state.h"
#include "log.h"
#include "tbp/conversions.h"
#include "tbp/external_bot.h"
#include "tbp/internal_bot.h"
#include "tbp/types.h"

#include <variant>

TbpController::TbpController(std::unique_ptr<tbp::TbpBot> bot,
                             int think_time_ms)
    : bot_(std::move(bot)), think_time_ms_(think_time_ms) {
  // Accept rules optimistically. InternalTbpBot always returns Ready; for
  // external bots the caller is expected to verify this before wiring in.
  (void)bot_->rules(tbp::Rules{});
}

TbpController::~TbpController() {
  if (bot_)
    bot_->quit();
}

void TbpController::reset() {
  // Mark the controller for a fresh Start on the next tick and drop everything
  // tied to the old session.
  phase_ = Phase::Cold;
  needs_suggest_ = true;
  pending_sug_.reset();

  bot_->stop();
}

void TbpController::tick(TimePoint now, const GameState &state,
                         CommandBuffer &cmds) {
  if (state.game_over)
    return;

  if (phase_ == Phase::Cold) {
    resync(state);
    return;
  }

  // Always poll — keeps the bot's plan cache (last_pv / last_plan_moves)
  // fresh regardless of whether we're allowed to place this tick.
  if (auto fresh = bot_->poll_suggestion()) {
    needs_suggest_ = true;
    pending_sug_ = std::move(*fresh);
  }

  if (phase_ == Phase::Placing) [[unlikely]] {
    // LOG_WARN("Game didn't lock our last played piece?");
    return;
  }

  // Rate cap: stall until the configured think time has elapsed since the
  // last placement was emitted.
  if (think_time_ms_ > 0) {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_placement_time_);
    if (elapsed.count() < think_time_ms_)
      return;
  }

  if (!pending_sug_ || pending_sug_->moves.empty()) {
    return;
  }

  // Place suggested piece
  auto sug = std::move(pending_sug_);
  pending_sug_.reset();

  const auto &move = sug->moves.front();
  PieceType placed_type = move.location.type;

  // Hold inference
  if (placed_type != state.current_piece.type) {
    if (!state.hold_available) [[unlikely]] {
      LOG_WARN("{}:{} Hold not available?", __FILE__, __LINE__);
      reset();
      return;
    }
    cmds.push(cmd::MovePiece{Input::Hold});
  }

  Placement p = tbp::location_to_placement(move.location, move.spin);
  cmds.push(cmd::Place{p});
  phase_ = Phase::Placing;
  last_placement_time_ = now;
}

void TbpController::notify(const EngineEvent &ev, TimePoint now, const GameState &state) {
  (void)now;
  if (auto *pl = std::get_if<eng::PieceLocked>(&ev)) {
    phase_ = Phase::Ready;
    Placement placed{pl->type, pl->rotation, pl->x, pl->y, pl->spin};
    tbp::Move m;
    m.location = tbp::placement_to_location(placed);
    m.spin = pl->spin;
    bot_->play(tbp::Play{m});
  } else if (auto *qr = std::get_if<eng::QueueRefill>(&ev)) {
    // A new piece entered the preview tail. Forward to the bot.
    bot_->new_piece(tbp::NewPiece{qr->piece});
  } else if (std::holds_alternative<eng::GarbageMaterialized>(ev)) {
    // Restart since board state is desynced
    reset();
  } else if (std::holds_alternative<eng::UndoPerformed>(ev)) {
    // Queue, board, and hold all rewound — the bot's cached state is stale.
    reset();
  } else if (std::holds_alternative<eng::IllegalPlacement>(ev)) {
    // restart misbehaving bot
    LOG_INFO("Bot [{}] is misbehaving, resyncing", bot_->info().name);
    reset();
  } else if (std::holds_alternative<eng::GameOver>(ev)) {
    bot_->stop();
  }
}

void TbpController::post_hook(TimePoint now, const GameState &state) {
  (void)now;
  if (state.game_over)
    return;

  if (phase_ == Phase::Cold) {
    resync(state);
    return;
  }

  // request in post_hook giving bot ~1 frame before polling its suggestion
  if (needs_suggest_) {
    needs_suggest_ = false;
    bot_->request_suggestion();
  }
}

void TbpController::fill_plan_overlay(ViewModel &vm, const GameState &state) {
  if (!show_plan_)
    return;

  // Only the placement field of Plan::Step is consumed by
  // build_plan_overlay; the rest stays default.
  auto plan = bot_->last_plan();
  if (plan.empty())
    return;
  std::vector<Plan::Step> steps;
  steps.reserve(plan.size());
  for (const auto &p : plan) {
    Plan::Step step;
    step.placement = p;
    steps.push_back(std::move(step));
  }

  // Defensive: only render if the first planned step actually plays the
  // current piece (or held piece — bot may intend to hold-then-play). This
  // filters out a brief window after PieceLocked where the cache might
  // still describe the piece we just placed (esp. for external bots that
  // rely on the next poll to refresh).
  PieceType first_type = steps.front().placement.type;
  bool type_ok = first_type == state.current_piece.type ||
                 (state.hold_piece && *state.hold_piece == first_type);
  if (!type_ok)
    return;

  BoardBitset sim = BoardBitset::from_board(state.board);
  vm.plan_overlay =
      build_plan_overlay(std::move(sim), steps, /*max_visible=*/7);
}

void TbpController::resync(const GameState &state) {
  // Stop any in-flight search so the bot doesn't hand us a suggestion based
  // on the pre-resync state.
  bot_->stop();

  tbp::Start s;
  // TBP queue[0] = current piece in play; Citris tracks current separately.
  s.queue.reserve(state.queue.size() + 1);
  s.queue.push_back(state.current_piece.type);
  for (auto p : state.queue)
    s.queue.push_back(p);
  s.hold = state.hold_piece;
  s.combo = state.attack_state.combo;
  s.back_to_back = state.attack_state.b2b > 0;
  s.board = tbp::board_to_tbp(state.board);
  bot_->start(s);

  // Flush any stale suggestion captured from the pre-resync search.
  pending_sug_.reset();
  needs_suggest_ = true;
  phase_ = Phase::Ready;
}
