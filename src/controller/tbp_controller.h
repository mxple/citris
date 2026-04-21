#pragma once

// IController that drives a Game from any TbpBot (internal or external).

#include "controller/controller.h"
#include "engine/piece.h"
#include "engine_event.h"
#include "tbp/bot.h"

#include <cstdint>
#include <memory>
#include <optional>

class TbpController : public IController {
public:
  // think_time_ms: minimum milliseconds between piece placements. 0 = no
  // rate cap (place as soon as the bot suggests). Applied by skipping
  // placement emission on ticks that arrive too soon after the last one.
  // Useful to keep AI-vs-AI GUI matches watchable.
  explicit TbpController(std::unique_ptr<tbp::TbpBot> bot,
                         int think_time_ms = 0);
  ~TbpController() override;

  // IController
  void handle_event(const InputEvent &, TimePoint, const GameState &,
                    CommandBuffer &) override {}
  void tick(TimePoint now, const GameState &state,
            CommandBuffer &cmds) override;
  std::optional<TimePoint> next_deadline() const override {
    return std::nullopt;
  }
  void reset() override;
  void notify(const EngineEvent &ev, TimePoint now, const GameState &state) override;
  void post_hook(TimePoint now, const GameState &state) override;
  void fill_plan_overlay(ViewModel &vm, const GameState &state) override;

  // Toggle plan-overlay rendering. Only meaningful when the wrapped bot
  // exposes its principal variation (InternalTbpBot today).
  void set_show_plan(bool s) { show_plan_ = s; }

  // For diagnostics / inspection.
  const tbp::TbpBot &bot() const { return *bot_; }

private:
  // Resync the bot against the current engine state: stops any in-flight
  // search and sends Start with the visible queue. Used on first tick, on
  // GarbageMaterialized (board desync), on IllegalPlacement (misbehaving
  // bot), and on UndoPerformed. After resync, the bot's queue baseline is
  // state.queue; subsequent QueueRefill events extend it via new_piece.
  void resync(const GameState &state);

  // Placement lifecycle. Transitions:
  //   Cold    → Ready    : first tick() — via resync()
  //   Ready   → Placing  : tick() pushes cmd::Place
  //   Placing → Ready    : notify(PieceLocked)
  //   *       → Ready    : notify(Garbage|Undo|Illegal) — via resync()
  //   *       → Cold     : reset()
  // Orthogonal to the search lifecycle tracked by pending_sug_ /
  // needs_suggest_ (when the bot is searching vs. has a suggestion ready).
  enum class Phase { Cold, Ready, Placing };
  Phase phase_ = Phase::Cold;

  std::unique_ptr<tbp::TbpBot> bot_;
  int think_time_ms_ = 0;
  TimePoint last_placement_time_{};
  bool show_plan_ = false;
  // The most recent unplayed suggestion. Filled on each poll_suggestion()
  // hit; consumed when the think_time gate releases and we push a
  // cmd::Place. Decoupling poll from place keeps the plan cache fresh for
  // rendering even during the think-time wait window.
  std::optional<tbp::Suggestion> pending_sug_;
  // Set when we need to issue a fresh Suggest to the bot — on start and at
  // each turn boundary. Consumed in post_hook so the bot searches on the
  // full post-turn state rather than a stale one.
  bool needs_suggest_ = true;
};
