#include "match.h"

#include "engine/game.h"

void route_garbage_between(Game &p1, Game &p2,
                            CommandBuffer &p1_cmds,
                            CommandBuffer &p2_cmds,
                            std::mt19937 &gap_rng) {
  std::uniform_int_distribution<int> col_dist(0, 9);
  int atk1 = p1.drain_attack();
  if (atk1 > 0)
    p2_cmds.push(cmd::AddGarbage{atk1, col_dist(gap_rng)});
  int atk2 = p2.drain_attack();
  if (atk2 > 0)
    p1_cmds.push(cmd::AddGarbage{atk2, col_dist(gap_rng)});
}

void update_match_state(MatchState &m, const Game &p1, const Game *p2) {
  if (m.p1_alive && p1.state().game_over) {
    m.p1_alive = false;
    if (!m.winner && p2 && m.p2_alive)
      m.winner = 1;
  }
  if (p2 && m.p2_alive && p2->state().game_over) {
    m.p2_alive = false;
    if (!m.winner && m.p1_alive)
      m.winner = 0;
  }
}
