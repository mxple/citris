// Two-phase PC solver. Port of MinusKelvin's pcf.

#include "pc_solver.h"
#include "pc_combo.h"
#include "movegen.h"
#include <algorithm>
#include <atomic>
#include <thread>

namespace {

// --- Board conversion helpers ---

PcBoard to_pc_board(const BoardBitset &bb, int height) {
  uint64_t bits = 0;
  for (int y = 0; y < height; ++y)
    bits |= static_cast<uint64_t>(bb.rows[y] & 0x3FF) << (y * 10);
  return {bits};
}

BoardBitset to_board_bitset(PcBoard pb) {
  BoardBitset bb{};
  for (int y = 0; y < 6; ++y) {
    uint16_t row = static_cast<uint16_t>((pb.bits >> (y * 10)) & 0x3FF);
    bb.rows[y] = row;
    while (row) {
      int x = std::countr_zero(static_cast<unsigned>(row));
      bb.cols[x] |= 1ULL << y;
      row &= row - 1;
    }
  }
  return bb;
}

// Map piece cells from combined-board coordinates through line clears.
uint64_t map_cells_through_clears(uint64_t cells, PcBoard combined,
                                  int height) {
  uint64_t result = 0;
  int dst = 0;
  for (int y = 0; y < height; ++y) {
    if (combined.line_filled(y))
      continue;
    uint64_t row_bits = (cells >> (y * 10)) & 0x3FF;
    result |= row_bits << (dst * 10);
    ++dst;
  }
  return result;
}

// Convert a Placement's cells to a 60-bit mask.
uint64_t placement_to_bits(const Placement &m) {
  uint64_t bits = 0;
  for (auto &c : m.cells())
    if (c.x >= 0 && c.x < 10 && c.y >= 0 && c.y < 6)
      bits |= 1ULL << (c.x + c.y * 10);
  return bits;
}

// --- Queue tracker (port of pcf's PieceSequence) ---
// Stored reversed: current piece at index (count-1).
// With hold, can access either current or one deeper.

struct PcQueue {
  uint8_t seq[16]{};
  int count = 0;

  bool is_next(int piece_idx, bool hold_allowed) const {
    if (count == 0)
      return false;
    if (seq[count - 1] == piece_idx)
      return true;
    return hold_allowed && count >= 2 && seq[count - 2] == piece_idx;
  }

  bool uses_hold(int piece_idx) const {
    return count >= 2 && seq[count - 1] != piece_idx;
  }

  PcQueue after_remove(int piece_idx) const {
    PcQueue q = *this;
    if (q.seq[q.count - 1] != static_cast<uint8_t>(piece_idx))
      q.seq[q.count - 2] = q.seq[q.count - 1];
    --q.count;
    return q;
  }
};

// --- Phase 2: solve ordering ---

// Check SRS+ reachability: can the piece reach its target cells on the
// actual board (combined with line clears applied)?
bool check_reachable(PcBoard combined, const PcPlacement &pp, int height) {
  uint64_t piece_on_combined = pp.state.board << pp.x;
  uint64_t target = map_cells_through_clears(piece_on_combined, combined, height);
  if (target == 0)
    return false;

  PcBoard actual = combined.lines_cleared();
  BoardBitset bb = to_board_bitset(actual);

  MoveBuffer moves;
  generate_moves(bb, moves, static_cast<PieceType>(pp.state.piece_idx), false);

  for (const auto &m : moves)
    if (placement_to_bits(m) == target)
      return true;
  return false;
}

struct SolveState {
  PcBoard initial_board;
  int height;
  bool use_hold;
  bool found = false;
  std::vector<PcPlacement> best_ordering;
};

void solve_ordering(SolveState &ss, std::vector<PcPlacement> &remaining,
                    PcBoard combined, PcQueue queue,
                    std::vector<PcPlacement> &ordered) {
  if (ss.found)
    return;

  if (remaining.empty()) {
    ss.found = true;
    ss.best_ordering = ordered;
    return;
  }

  for (int i = 0; i < static_cast<int>(remaining.size()); ++i) {
    PcPlacement pp = remaining[i];
    int pidx = pp.state.piece_idx;

    if (!queue.is_next(pidx, ss.use_hold))
      continue;
    if (!pp.supported_after_clears(combined))
      continue;
    if (!check_reachable(combined, pp, ss.height))
      continue;

    PcBoard new_combined = combined.combine(pp.board());
    PcQueue new_queue = queue.after_remove(pidx);

    // swap-remove for O(1)
    remaining[i] = remaining.back();
    remaining.pop_back();
    ordered.push_back(pp);

    solve_ordering(ss, remaining, new_combined, new_queue, ordered);

    ordered.pop_back();
    remaining.push_back(pp);
    std::swap(remaining[i], remaining.back());

    if (ss.found)
      return;
  }
}

// --- Solution conversion ---

// Replay the ordered PcPlacements on the actual game board, matching each
// against generate_moves output to produce Placement objects.
std::vector<Placement> convert_solution(PcBoard initial_board,
                                        const std::vector<PcPlacement> &ordered,
                                        int height, bool &hold_used) {
  std::vector<Placement> result;
  result.reserve(ordered.size());
  PcBoard combined = initial_board;

  for (const auto &pp : ordered) {
    PcBoard actual = combined.lines_cleared();
    BoardBitset bb = to_board_bitset(actual);

    uint64_t piece_on_combined = pp.state.board << pp.x;
    uint64_t target =
        map_cells_through_clears(piece_on_combined, combined, height);

    PieceType pt = static_cast<PieceType>(pp.state.piece_idx);
    MoveBuffer moves;
    generate_moves(bb, moves, pt, false);

    bool found_match = false;
    for (const auto &m : moves) {
      if (placement_to_bits(m) == target) {
        result.push_back(m);
        found_match = true;
        break;
      }
    }

    if (!found_match)
      return {}; // shouldn't happen

    combined = combined.combine(pp.board());
  }

  return result;
}

} // namespace

PcResult find_perfect_clear(const BoardBitset &board,
                            const std::vector<PieceType> &queue,
                            std::optional<PieceType> hold,
                            const PcConfig &config) {
  if (queue.empty())
    return {};

  pc_data_init();

  int max_height = std::min(config.height_cap, 6);

  // Find lowest height that covers all occupied cells.
  PcBoard full_board = to_pc_board(board, max_height);
  int lowest_height = 0;
  for (int y = 0; y < max_height; ++y)
    if ((full_board.bits >> (y * 10)) & 0x3FF)
      lowest_height = y + 1;

  // Empty cells must be divisible by 4 (each piece fills 4).
  int unfilled = 10 * lowest_height - full_board.popcount();
  if (unfilled % 2 != 0)
    return {};
  if (unfilled % 4 != 0)
    lowest_height += 1; // need an extra line
  if (lowest_height == 0)
    lowest_height = 2;

  // Try each height from lowest valid to max.
  for (int height = lowest_height; height <= max_height; height += 2) {
    PcBoard pc_board = to_pc_board(board, height);
    int empty_cells = height * 10 - pc_board.popcount();
    int pieces_needed = empty_cells / 4;

    if (pieces_needed > config.max_pieces)
      continue;

    // Determine how many queue elements we need.
    int queue_take;
    bool have_hold = config.use_hold && hold.has_value();
    if (have_hold)
      queue_take = pieces_needed; // + hold = pieces_needed + 1 total
    else if (config.use_hold)
      queue_take = pieces_needed + 1; // one extra for hold flexibility
    else
      queue_take = pieces_needed;

    if (static_cast<int>(queue.size()) < queue_take)
      break;

    // Build the PcQueue (reversed stack for Phase 2).
    // If hold exists, insert it at position 1 (after current piece).
    std::vector<uint8_t> seq_order;
    if (have_hold) {
      seq_order.push_back(static_cast<uint8_t>(queue[0]));
      seq_order.push_back(static_cast<uint8_t>(*hold));
      for (int i = 1; i < queue_take; ++i)
        seq_order.push_back(static_cast<uint8_t>(queue[i]));
    } else {
      for (int i = 0; i < queue_take; ++i)
        seq_order.push_back(static_cast<uint8_t>(queue[i]));
    }

    PcQueue pq{};
    pq.count = static_cast<int>(seq_order.size());
    for (int i = 0; i < pq.count; ++i)
      pq.seq[i] = seq_order[pq.count - 1 - i]; // reverse

    // Build PieceSet for Phase 1 (multiset of all available pieces).
    PcPieceSet piece_set{};
    for (auto p : seq_order)
      ++piece_set.counts[p];

    // Phase 1 + Phase 2: find combinations, try ordering each.
    SolveState ss{pc_board, height, config.use_hold};

    find_combinations(piece_set, pc_board, height, [&](const auto &combo) {
      if (ss.found)
        return false;

      std::vector<PcPlacement> remaining = combo;
      std::vector<PcPlacement> ordered;
      ordered.reserve(combo.size());

      solve_ordering(ss, remaining, pc_board, pq, ordered);

      return !ss.found; // false = stop
    });

    if (ss.found) {
      bool hold_was_used = false;
      // Check if hold was used by replaying the queue.
      {
        PcQueue replay = pq;
        for (const auto &pp : ss.best_ordering) {
          int pidx = pp.state.piece_idx;
          if (replay.uses_hold(pidx))
            hold_was_used = true;
          replay = replay.after_remove(pidx);
        }
      }

      auto solution =
          convert_solution(pc_board, ss.best_ordering, height, hold_was_used);
      if (solution.empty())
        continue; // conversion failed (shouldn't happen)

      PcResult result;
      result.found = true;
      result.solution = std::move(solution);
      result.hold_used = hold_was_used;
      return result;
    }
  }

  return {};
}

// ---------------------------------------------------------------------------
// PcTask — async wrapper
// ---------------------------------------------------------------------------

struct PcTask::Impl {
  BoardBitset board;
  std::vector<PieceType> queue;
  std::optional<PieceType> hold;
  PcConfig cfg;
  std::thread thread;
  std::atomic<bool> done{false};
  PcResult result;
};

PcTask::PcTask(BeamInput input, PcConfig cfg)
    : impl_(std::make_unique<Impl>()) {
  auto &impl = *impl_;
  impl.board = input.board;
  impl.queue = std::move(input.queue);
  impl.hold = input.hold;
  impl.cfg = cfg;

  impl.thread = std::thread([this] {
    impl_->result = find_perfect_clear(impl_->board, impl_->queue,
                                       impl_->hold, impl_->cfg);
    impl_->done.store(true, std::memory_order_release);
  });
}

PcTask::~PcTask() { cancel(); }

bool PcTask::ready() const {
  return impl_->done.load(std::memory_order_acquire);
}

PcResult PcTask::get() {
  if (impl_->thread.joinable())
    impl_->thread.join();
  return impl_->result;
}

void PcTask::cancel() {
  // No interrupt support — just wait for the search to finish.
  if (impl_->thread.joinable())
    impl_->thread.join();
}

std::unique_ptr<PcTask> start_pc_search(BeamInput input, PcConfig cfg) {
  return std::unique_ptr<PcTask>(new PcTask(std::move(input), cfg));
}
