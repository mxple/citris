// Phase 1 verify: garbage routes p1 -> p2 via route_garbage_between, and the
// receiver materializes the rows after garbage_delay. No SDL, no GameManager.

#include "ai/placement.h"
#include "engine/board.h"
#include "engine/game.h"
#include "engine/piece.h"
#include "engine/piece_queue.h"
#include "engine/piece_source.h"
#include "match.h"
#include "presets/game_mode.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>

using namespace std::chrono_literals;

namespace {

// Minimal mode for routing test. Disables gravity / lock delay so the
// placement-via-cmd::Place path is the only mover. Short garbage delay keeps
// the test fast. Optional pre-fill of the board for a guaranteed tetris.
class TestRoutingMode : public GameMode {
public:
  std::chrono::milliseconds gravity_interval() const override {
    return std::chrono::milliseconds{-1};
  }
  std::chrono::milliseconds lock_delay() const override {
    return std::chrono::milliseconds{-1};
  }
  std::chrono::milliseconds garbage_delay() const override {
    return std::chrono::milliseconds{50};
  }
  int max_lock_resets() const override { return 0; }

  void setup_board(Board &b) override {
    if (!fill_for_tetris)
      return;
    // Fill columns 0..8 of bottom 4 rows; column 9 is the gap that the I
    // piece will fill to clear 4 lines.
    for (int row = 0; row < 4; ++row)
      for (int col = 0; col < 9; ++col)
        b.set_cell(col, row, CellColor::Garbage);
  }

  PieceQueue create_queue(unsigned) const override {
    auto src = std::make_unique<EmptySource>();
    std::vector<PieceType> prefix = {PieceType::I, PieceType::I, PieceType::I,
                                     PieceType::I};
    return PieceQueue(std::move(src), std::move(prefix));
  }

  bool fill_for_tetris = false;
};

#define EXPECT(cond, ...)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "FAIL [%s:%d]: " #cond "\n",                        \
                   __FILE__, __LINE__);                                        \
      std::fprintf(stderr, "  " __VA_ARGS__);                                  \
      std::fprintf(stderr, "\n");                                              \
      return 1;                                                                \
    }                                                                          \
  } while (0)

int count_garbage_rows(const Board &b) {
  int rows = 0;
  for (int row = 0; row < Board::kTotalHeight; ++row) {
    int filled = 0;
    for (int col = 0; col < Board::kWidth; ++col)
      if (b.cell_color(col, row) == CellColor::Garbage)
        ++filled;
    if (filled == Board::kWidth - 1)
      ++rows;
  }
  return rows;
}

} // namespace

int main() {
  TestRoutingMode mode_p1;
  mode_p1.fill_for_tetris = true;
  TestRoutingMode mode_p2;

  Board b1, b2;
  mode_p1.setup_board(b1);
  mode_p2.setup_board(b2);

  Game game1(mode_p1, std::move(b1), 1);
  Game game2(mode_p2, std::move(b2), 2);
  std::mt19937 gap_rng(0xC17150);

  // Drain the spawn-time events both games emit during construction so they
  // don't pollute later assertions.
  game1.drain_events();
  game2.drain_events();

  // Place the I piece vertical in column 9 — Game::handle_place snaps to
  // ground, so any starting y works. East rotation has cells at col_off=2,
  // so x=7 puts the piece in column 9.
  CommandBuffer cmds1, cmds2;
  cmds1.push(cmd::Place{
      Placement{PieceType::I, Rotation::East, 7, 0, SpinKind::None}});

  auto t0 = SdlClock::now();
  game1.apply(cmds1);
  game1.tick(t0);
  cmds1.clear();
  game2.apply(cmds2);
  game2.tick(t0);
  cmds2.clear();

  // Discard PieceLocked / PieceSpawned events from the tetris.
  game1.drain_events();
  game2.drain_events();

  // Route garbage. game1 should have 4 attack pending; game2 has 0.
  route_garbage_between(game1, game2, cmds1, cmds2, gap_rng);

  EXPECT(cmds1.empty(), "expected no garbage routed back to p1");
  int routed_lines = 0;
  int gap_col = -1;
  for (auto &c : cmds2) {
    if (auto *g = std::get_if<cmd::AddGarbage>(&c)) {
      routed_lines += g->lines;
      gap_col = g->gap_col;
    }
  }
  // A 4-line clear that empties the board is a perfect clear (+10 bonus),
  // so the actual figure here is 14. We assert the routing *invariant*
  // (attack drained == garbage materialized) rather than the attack value
  // so this test stays robust against attack-table changes.
  EXPECT(routed_lines > 0, "expected positive attack routed, got %d",
         routed_lines);
  EXPECT(gap_col >= 0 && gap_col < 10, "gap_col out of range: %d", gap_col);

  // Apply the routed AddGarbage to game2 (queues it under garbage_delay).
  game2.apply(cmds2);
  cmds2.clear();
  game2.drain_events(); // discard apply-time events if any

  // Tick past the garbage_delay (50ms in our test mode).
  auto t1 = t0 + 100ms;
  game2.tick(t1);

  int materialized = 0;
  for (auto &ev : game2.drain_events()) {
    if (auto *gm = std::get_if<eng::GarbageMaterialized>(&ev))
      materialized += gm->lines;
  }
  EXPECT(materialized == routed_lines,
         "materialized (%d) != routed (%d) — routing lost data",
         materialized, routed_lines);

  // Verify the receiver's board now has `routed_lines` garbage rows (9 cells
  // filled, 1 gap each).
  int garbage_rows = count_garbage_rows(game2.state().board);
  EXPECT(garbage_rows == routed_lines,
         "board has %d garbage rows, expected %d", garbage_rows, routed_lines);

  // Verify game1's pending_attack was zeroed (next drain returns 0).
  EXPECT(game1.drain_attack() == 0, "p1 attack should have been drained");

  // MatchState: nobody topped out, both alive, no winner.
  MatchState match;
  update_match_state(match, game1, &game2);
  EXPECT(match.p1_alive && match.p2_alive && !match.winner,
         "expected match still in progress");

  std::printf("PASS test_versus_routing: %d attack -> %d garbage rows on p2 "
              "(gap_col=%d)\n",
              routed_lines, garbage_rows, gap_col);
  return 0;
}
