#include "plan.h"
#include "movegen.h"

#include <algorithm>
#include <array>
#include <cstdint>

bool Plan::advance(const Placement &locked) {
  Placement canon = locked.canonical();
  auto it = std::find_if(remaining.begin(), remaining.end(),
                         [&](const Step &s) {
                           return s.placement.canonical().same_landing(canon);
                         });
  if (it == remaining.end())
    return false;
  remaining.erase(it);
  ++played_count;
  return true;
}

Plan Plan::from_placements(std::span<const Placement> placements) {
  Plan p;
  p.remaining.reserve(placements.size());
  for (const auto &pl : placements) {
    Step s{};
    s.placement = pl;
    p.remaining.push_back(s);
  }
  return p;
}

std::vector<OverlayCell>
build_plan_overlay(BoardBitset board, std::span<const Plan::Step> steps,
                   int max_visible) {
  // row_map[working_y] = display y in the original board frame
  std::array<int, BoardBitset::kHeight> row_map;
  for (int i = 0; i < BoardBitset::kHeight; ++i)
    row_map[i] = i;
  int active_rows = BoardBitset::kHeight;

  std::vector<OverlayCell> out;
  int count = std::min(max_visible, static_cast<int>(steps.size()));
  out.reserve(static_cast<std::size_t>(count) * 4);

  for (int i = 0; i < count; ++i) {
    const auto &step = steps[i];
    auto raw_cells = step.placement.cells();

    for (int c = 0; c < 4; ++c) {
      int wy = raw_cells[c].y;
      int display_y = (wy >= 0 && wy < active_rows) ? row_map[wy] : wy;
      out.push_back({{raw_cells[c].x, display_y}, step.placement.type});
    }

    // Simulate the placement and compact row_map for any rows it filled.
    board.place(step.placement.type, step.placement.rotation, step.placement.x,
                step.placement.y);

    int write = 0;
    for (int r = 0; r < active_rows; ++r) {
      if (!board.row_full(r)) {
        row_map[write] = row_map[r];
        ++write;
      }
    }
    active_rows = write;

    board.clear_lines();
  }

  return out;
}

namespace {

struct SolveCtx {
  std::span<const PieceType> queue;
  const std::vector<Plan::Step> *steps;

  // Filled in reverse during the successful recursion (innermost first).
  // Reversed once at the top level so out_order[0] = first piece to play.
  std::vector<int> out_order;
  std::vector<BoardBitset> out_boards;
  std::vector<int> out_lines;
};

bool solve(SolveCtx &ctx, BoardBitset board, uint64_t mask, PieceType current,
           std::optional<PieceType> hold, int queue_idx) {
  if (mask == 0)
    return true;

  // Multiset prune: every remaining step needs a piece of matching type;
  // bail if any required type can't be supplied from {current, hold, queue}.
  std::array<int, 7> need{};
  std::array<int, 7> have{};
  for (int i = 0; i < (int)ctx.steps->size(); ++i)
    if (mask & (1ull << i))
      need[(int)(*ctx.steps)[i].placement.type]++;
  have[(int)current]++;
  if (hold.has_value())
    have[(int)*hold]++;
  for (int i = queue_idx; i < (int)ctx.queue.size(); ++i)
    have[(int)ctx.queue[i]]++;
  for (int t = 0; t < 7; ++t)
    if (need[t] > have[t])
      return false;

  auto try_play = [&](PieceType play_type, std::optional<PieceType> after_hold,
                      PieceType after_current, int after_queue_idx) -> bool {
    MoveBuffer buf;
    bool generated = false;

    for (int i = 0; i < (int)ctx.steps->size(); ++i) {
      if (!(mask & (1ull << i)))
        continue;
      const auto &step = (*ctx.steps)[i];
      if (step.placement.type != play_type)
        continue;

      if (!generated) {
        generate_moves(board, buf, play_type);
        generated = true;
      }

      Placement target = step.placement.canonical();
      bool reachable = false;
      for (const auto &m : buf) {
        if (m.canonical().same_landing(target)) {
          reachable = true;
          break;
        }
      }
      if (!reachable)
        continue;

      BoardBitset next_board = board;
      next_board.place(step.placement.type, step.placement.rotation,
                       step.placement.x, step.placement.y);
      int lines = next_board.clear_lines();

      if (solve(ctx, next_board, mask & ~(1ull << i), after_current, after_hold,
                after_queue_idx)) {
        ctx.out_order.push_back(i);
        ctx.out_boards.push_back(next_board);
        ctx.out_lines.push_back(lines);
        return true;
      }
    }
    return false;
  };

  // Option A: play `current` directly. After playing, next current is drawn
  // from the queue (or, if the queue is exhausted, becomes a sentinel — only
  // matters if more steps remain, in which case the multiset prune in the
  // next recursion catches it).
  PieceType nextA = (queue_idx < (int)ctx.queue.size()) ? ctx.queue[queue_idx]
                                                        : current;
  int nqA = (queue_idx < (int)ctx.queue.size()) ? queue_idx + 1 : queue_idx;
  if (try_play(current, hold, nextA, nqA))
    return true;

  // Option B: play via hold-swap. If hold is non-empty, it swaps with current;
  // current becomes hold's old contents, hold becomes current's old type.
  // If hold is empty, holding draws queue head as new current and stashes the
  // old current — costs one queue draw.
  if (hold.has_value()) {
    PieceType swapped = *hold;
    std::optional<PieceType> new_hold = current;
    PieceType nextB = (queue_idx < (int)ctx.queue.size())
                          ? ctx.queue[queue_idx]
                          : swapped;
    int nqB = (queue_idx < (int)ctx.queue.size()) ? queue_idx + 1 : queue_idx;
    if (try_play(swapped, new_hold, nextB, nqB))
      return true;
  } else if (queue_idx < (int)ctx.queue.size()) {
    PieceType swapped = ctx.queue[queue_idx];
    std::optional<PieceType> new_hold = current;
    PieceType nextB = (queue_idx + 1 < (int)ctx.queue.size())
                          ? ctx.queue[queue_idx + 1]
                          : swapped;
    int nqB = (queue_idx + 1 < (int)ctx.queue.size()) ? queue_idx + 2
                                                      : queue_idx + 1;
    if (try_play(swapped, new_hold, nextB, nqB))
      return true;
  }

  return false;
}

} // namespace

bool check_feasibility(std::vector<Plan::Step> &steps, BoardBitset start_board,
                       PieceType current, std::optional<PieceType> hold,
                       std::span<const PieceType> queue) {
  if (steps.empty())
    return true;
  if (steps.size() > 64)
    return false;

  SolveCtx ctx;
  ctx.queue = queue;
  ctx.steps = &steps;
  ctx.out_order.reserve(steps.size());
  ctx.out_boards.reserve(steps.size());
  ctx.out_lines.reserve(steps.size());

  uint64_t full_mask =
      (steps.size() == 64) ? ~uint64_t(0) : ((uint64_t(1) << steps.size()) - 1);

  if (!solve(ctx, start_board, full_mask, current, hold, 0))
    return false;

  std::reverse(ctx.out_order.begin(), ctx.out_order.end());
  std::reverse(ctx.out_boards.begin(), ctx.out_boards.end());
  std::reverse(ctx.out_lines.begin(), ctx.out_lines.end());

  std::vector<Plan::Step> reordered;
  reordered.reserve(steps.size());
  for (std::size_t k = 0; k < ctx.out_order.size(); ++k) {
    Plan::Step s = steps[ctx.out_order[k]];
    s.board_after = ctx.out_boards[k];
    s.lines_cleared = ctx.out_lines[k];
    reordered.push_back(std::move(s));
  }
  steps = std::move(reordered);
  return true;
}
