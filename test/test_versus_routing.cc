// Phase 1 verify: garbage routes p1 -> p2 via route_garbage_between, and the
// receiver materializes the rows on its next non-clearing piece lock (capped
// at Game::kMaxGarbagePerLock). No SDL, no GameManager.

#include "ai/placement.h"
#include "engine/board.h"
#include "engine/game.h"
#include "engine/piece.h"
#include "engine/piece_queue.h"
#include "engine/piece_source.h"
#include "match.h"
#include "presets/game_mode.h"

#include <cstdio>
#include <cstdlib>
#include <random>

namespace {

// Minimal mode for routing test. Disables gravity / lock delay so the
// placement-via-cmd::Place path is the only mover. Pre-fills board for a
// guaranteed tetris when requested. Fixed I-heavy queue so the test is
// deterministic.
class TestRoutingMode : public GameMode {
public:
  std::chrono::milliseconds gravity_interval() const override {
    return std::chrono::milliseconds{-1};
  }
  std::chrono::milliseconds lock_delay() const override {
    return std::chrono::milliseconds{-1};
  }
  std::chrono::milliseconds garbage_delay() const override {
    // Zero delay so every non-clearing lock materializes immediately; the
    // delay behavior is exercised in test_garbage_delay.
    return std::chrono::milliseconds{0};
  }
  int max_lock_resets() const override { return 0; }

  void setup_board(Board &b) override {
    if (!fill_for_tetris) return;
    for (int row = 0; row < 4; ++row)
      for (int col = 0; col < 9; ++col)
        b.set_cell(col, row, CellColor::Garbage);
  }

  PieceQueue create_queue(unsigned) const override {
    auto src = std::make_unique<EmptySource>();
    std::vector<PieceType> prefix = {PieceType::I, PieceType::I, PieceType::I,
                                     PieceType::I, PieceType::I, PieceType::I};
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
    if (filled == Board::kWidth - 1) ++rows;
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

  game1.drain_events();
  game2.drain_events();

  // Step 1: tetris-into-PC on game1 → 14 attack drained.
  CommandBuffer cmds1, cmds2;
  cmds1.push(cmd::Place{
      Placement{PieceType::I, Rotation::East, 7, 0, SpinKind::None}});
  auto t0 = SdlClock::now();
  game1.apply(cmds1);
  game1.tick(t0);
  cmds1.clear();
  game1.drain_events();

  // Step 2: route attack. game2 hasn't moved yet.
  route_garbage_between(game1, game2, cmds1, cmds2, gap_rng);

  EXPECT(cmds1.empty(), "no return-path garbage expected");
  int routed_lines = 0;
  int gap_col = -1;
  for (auto &c : cmds2) {
    if (auto *g = std::get_if<cmd::AddGarbage>(&c)) {
      routed_lines += g->lines;
      gap_col = g->gap_col;
    }
  }
  EXPECT(routed_lines == 14,
         "tetris+PC should yield 14 attack (4 tetris + 10 PC); got %d",
         routed_lines);
  EXPECT(gap_col >= 0 && gap_col < 10, "gap_col out of range: %d", gap_col);

  // Step 3: queue the garbage on game2. Nothing materializes yet — the
  // buffer only drains on a non-clearing piece lock.
  game2.apply(cmds2);
  cmds2.clear();
  game2.drain_events();
  EXPECT(game2.pending_garbage_lines() == 14,
         "expected 14 lines buffered, got %d", game2.pending_garbage_lines());
  EXPECT(count_garbage_rows(game2.state().board) == 0,
         "no garbage should be on the board yet");

  // Step 4: place I horizontally in a column that avoids the gap so no line
  // clears. I/North occupies cells (x..x+3, y+2). Pick x that doesn't include
  // gap_col.
  int safe_x = (gap_col <= 3) ? 5 : 0;
  // Pass a high y so compute_ghost drops to the lowest valid position. Passing
  // y=0 would start below any existing stack and compute_ghost only goes
  // *down*, so it can't recover from a colliding start.
  cmds2.push(cmd::Place{
      Placement{PieceType::I, Rotation::North, static_cast<int8_t>(safe_x),
                25, SpinKind::None}});
  game2.apply(cmds2);
  game2.tick(t0);
  cmds2.clear();

  int materialized_first = 0;
  for (auto &ev : game2.drain_events())
    if (auto *gm = std::get_if<eng::GarbageMaterialized>(&ev))
      materialized_first += gm->lines;
  EXPECT(materialized_first == 8,
         "first non-clear lock should materialize 8 (cap); got %d",
         materialized_first);
  EXPECT(game2.pending_garbage_lines() == 6,
         "6 lines should remain buffered; got %d",
         game2.pending_garbage_lines());
  EXPECT(count_garbage_rows(game2.state().board) == 8,
         "board should have exactly 8 garbage rows; got %d",
         count_garbage_rows(game2.state().board));

  // Step 5: another non-clearing lock → remaining 6 lines materialize on
  // the *same* gap column (partial-batch preservation).
  int safe_x2 = safe_x; // same column; it lands one row above the previous I.
  cmds2.push(cmd::Place{
      Placement{PieceType::I, Rotation::North, static_cast<int8_t>(safe_x2),
                25, SpinKind::None}});
  game2.apply(cmds2);
  game2.tick(t0);
  cmds2.clear();

  int materialized_second = 0;
  for (auto &ev : game2.drain_events())
    if (auto *gm = std::get_if<eng::GarbageMaterialized>(&ev))
      materialized_second += gm->lines;
  EXPECT(materialized_second == 6,
         "second non-clear lock should drain remaining 6; got %d",
         materialized_second);
  EXPECT(game2.pending_garbage_lines() == 0, "buffer should be empty");
  EXPECT(count_garbage_rows(game2.state().board) == 14,
         "board should have 14 garbage rows total; got %d",
         count_garbage_rows(game2.state().board));

  // Verify all 14 garbage rows share the same gap column (single-batch
  // routing from a single tetris-PC must preserve the well).
  int same_gap = 0;
  for (int row = 0; row < 14; ++row)
    if (game2.state().board.cell_color(gap_col, row) == CellColor::Empty)
      ++same_gap;
  EXPECT(same_gap == 14,
         "all 14 garbage rows should share gap_col=%d; got %d", gap_col,
         same_gap);

  std::printf("PASS test_versus_routing: 14 attack -> 8 materialize -> 6 "
              "materialize (gap_col=%d)\n",
              gap_col);

  // Sanity: p1 attack was drained, match still in progress.
  EXPECT(game1.drain_attack() == 0, "p1 attack should have been drained");
  MatchState match;
  update_match_state(match, game1, &game2);
  EXPECT(match.p1_alive && match.p2_alive && !match.winner,
         "match still in progress");
  return 0;
}
