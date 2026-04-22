#pragma once

#include "command.h"
#include <optional>
#include <random>

class Game;

// Lightweight winner tracking for a 1v1 match. Single-player code paths
// never look at this — it's owned by GameManager only when a second game
// exists.
struct MatchState {
  std::optional<int> winner; // 0 = player 1, 1 = player 2
  bool p1_alive = true;
  bool p2_alive = true;

  bool over() const { return !p1_alive || !p2_alive; }
};

// Drain accumulated attack from each game and post a cmd::AddGarbage to
// the *opposite* game's command buffer. The garbage gap column is drawn
// from a shared per-match RNG so SPRT replays remain deterministic.
//
// Garbage is queued non-immediate, so it materializes after the receiver's
// configured garbage_delay() expires on a later tick.
void route_garbage_between(Game &p1, Game &p2,
                            CommandBuffer &p1_cmds,
                            CommandBuffer &p2_cmds,
                            std::mt19937 &gap_rng);

// Snapshot both games' game_over flags into the match state. Idempotent —
// safe to call every tick. The first game to top out loses; the other is
// the winner. If both end on the same tick, p2 is recorded as winner
// (deterministic tie-break).
void update_match_state(MatchState &m, const Game &p1, const Game *p2);
