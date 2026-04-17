#include "puzzle_gen.h"
#include "ai/board_bitset.h"
#include "ai/movegen.h"
#include "engine/piece.h"
#include <algorithm>
#include <array>

// 1-to-1 port of references/downstack-practice/script7.js — TSD variant.
//
// Every cell in the grid is either 0 (empty) or 1 (garbage). There are NO
// protected cells: reverse construction can uncarve ANY garbage cell,
// including the overhang cap and the wall. The reference's
// `is_few_non_cheese_hole` check (with mdhole_ind = 0) effectively requires
// the overhang cap to be uncarved during reverse construction — otherwise
// the count of non-cheese holes stays above 0 and the puzzle is rejected.
//
// Net effect: the initial board shown to the player usually does NOT display
// the TSD setup; the setup has to be built back up through downstacking.

namespace {

constexpr int kBoardW = Board::kWidth;
constexpr int kBoardH = Board::kTotalHeight;

struct Grid {
  std::array<std::array<uint8_t, kBoardW>, kBoardH> c{};

  bool filled(int x, int y) const { return c[y][x] != 0; }
  void set(int x, int y, uint8_t v) { c[y][x] = v; }

  int max_height() const {
    for (int y = kBoardH - 1; y >= 0; --y)
      for (int x = 0; x < kBoardW; ++x)
        if (filled(x, y))
          return y + 1;
    return 0;
  }

  int column_height(int x) const {
    for (int y = kBoardH - 1; y >= 0; --y)
      if (filled(x, y))
        return y + 1;
    return 0;
  }

  // Reference's `height[col]` semantics (script7.js:662-668): the row index
  // of the topmost filled cell, or 0 if the column is empty.
  int column_top_row(int x) const {
    int h = 0;
    for (int y = 0; y < kBoardH; ++y)
      if (filled(x, y))
        h = y;
    return h;
  }

  Board to_board() const {
    Board b;
    for (int y = 0; y < kBoardH; ++y)
      for (int x = 0; x < kBoardW; ++x)
        if (filled(x, y))
          b.set_cell(x, y, CellColor::Garbage);
    return b;
  }

  BoardBitset to_bitset() const {
    BoardBitset bb;
    for (int y = 0; y < kBoardH; ++y) {
      uint16_t r = 0;
      for (int x = 0; x < kBoardW; ++x)
        if (filled(x, y))
          r |= uint16_t(1) << x;
      bb.rows[y] = r;
      for (int x = 0; x < kBoardW; ++x)
        if (r & (uint16_t(1) << x))
          bb.cols[x] |= uint64_t(1) << y;
    }
    return bb;
  }
};

// add_line(row_idx): 1-to-1 port of script7.js:445-453. Shifts rows
// [row_idx+1, kBoardH-1] up by one, then fills row_idx with all garbage.
void add_line(Grid &g, int row_idx) {
  for (int y = kBoardH - 1; y > row_idx; --y)
    g.c[y] = g.c[y - 1];
  for (int x = 0; x < kBoardW; ++x)
    g.c[row_idx][x] = 1;
}

// 1-to-1 port of generate_final_map (script7.js:864-934), TSD mode.
//
// Builds the initial board state: random mesa with heights 2..4, a carved
// TSD keyhole (wall + overhang + slot + shoulder), and `garbage_below`
// cheese rows with a hole carried through a channel column.
//
// NOTE: the overhang cap is stamped as regular garbage (value 1). Reverse
// construction will usually uncarve it to satisfy is_few_non_cheese_hole,
// so the overhang usually isn't visible on the initial board.
bool generate_final_map(Grid &g, int garbage_below, std::mt19937 &rng) {
  g = Grid{};

  // Random heights 2..4 per column (script7.js:870-872).
  std::array<int, kBoardW> height;
  std::uniform_int_distribution<int> hdist(2, 4);
  for (int i = 0; i < kBoardW; ++i)
    height[i] = hdist(rng);

  // tsd_col in [1, 8]. Slot column — all empty.
  std::uniform_int_distribution<int> tsd_col_dist(1, 8);
  int tsd_col = tsd_col_dist(rng);
  height[tsd_col] = 0;

  // is_left decides which side the shoulder sits on; edges forced.
  bool is_left;
  if (tsd_col == 1)
    is_left = false;
  else if (tsd_col == 8)
    is_left = true;
  else
    is_left = std::uniform_int_distribution<int>(0, 1)(rng) == 1;

  int sign_left = is_left ? 1 : -1;
  height[tsd_col + sign_left] = 1; // shoulder column

  // Fill cols to their heights with G.
  for (int j = 0; j < kBoardH; ++j)
    for (int i = 0; i < kBoardW; ++i)
      if (j < height[i])
        g.set(i, j, 1);

  // Overhang gap: N at (tsd_col - sign_left, row 1). Overrides the fill.
  if (tsd_col - sign_left >= 0 && tsd_col - sign_left < kBoardW)
    g.set(tsd_col - sign_left, 1, 0);

  // Overhang cap + wall stamp (script7.js:889-900).
  if (is_left) {
    g.set(tsd_col - 1, 2, 1); // overhang cap
    g.set(tsd_col - 2, 0, 1); // wall row 0
    g.set(tsd_col - 2, 1, 1); // wall row 1
    g.set(tsd_col - 2, 2, 1); // wall row 2
  } else {
    g.set(tsd_col + 1, 2, 1);
    g.set(tsd_col + 2, 0, 1);
    g.set(tsd_col + 2, 1, 1);
    g.set(tsd_col + 2, 2, 1);
  }

  // --- Cheese rows (script7.js:902-930) ---

  int lines_add = garbage_below;

  // col_add: 50% chance slot column, 50% chance random; reject overhang and
  // wall columns (the setup's structural cells), fall back to slot.
  int col_add;
  bool use_slot = std::uniform_int_distribution<int>(0, 1)(rng) == 1;
  if (use_slot) {
    col_add = tsd_col;
  } else {
    col_add = std::uniform_int_distribution<int>(0, kBoardW - 1)(rng);
  }
  int overhang_col = tsd_col - sign_left;     // the overhang column
  int wall_col = tsd_col - 2 * sign_left;     // the wall column
  if (col_add == overhang_col || col_add == wall_col)
    col_add = tsd_col;

  // Pre-carve col_add at rows 2, 3 before any cheese shifts.
  g.set(col_add, 2, 0);
  g.set(col_add, 3, 0);

  // Insert cheese rows. Each add_line shifts rows up, then we poke a hole at
  // col_add in the newly-inserted row.
  for (int row_idx = 0; row_idx < lines_add; ++row_idx) {
    add_line(g, row_idx);
    g.set(col_add, row_idx, 0);
  }

  return true;
}

// --- Reverse construction ---------------------------------------------

void uncarve(Grid &g, const Placement &p) {
  for (auto c : p.cells())
    g.set(c.x, c.y, 0);
}

void recarve(Grid &g, const Placement &p) {
  for (auto c : p.cells())
    g.set(c.x, c.y, 1);
}

// Find the highest y at which the piece's 4 cells exactly overlap filled
// (garbage) cells. Equivalent to script7.js:509-544 `try_drop`.
bool try_uncarve_drop(const Grid &g, PieceType t, Rotation r, int x,
                      Placement &out) {
  const auto &cells =
      kPieceCells[static_cast<int>(t)][static_cast<int>(r)];
  for (int y = kBoardH - 1; y >= 0; --y) {
    bool ok = true;
    for (auto c : cells) {
      int cx = x + c.x, cy = y + c.y;
      if (cx < 0 || cx >= kBoardW || cy < 0 || cy >= kBoardH ||
          !g.filled(cx, cy)) {
        ok = false;
        break;
      }
    }
    if (ok) {
      out = Placement{t, r, static_cast<int8_t>(x), static_cast<int8_t>(y),
                      SpinKind::None};
      return true;
    }
  }
  return false;
}

bool placement_in_moves(const MoveBuffer &moves, const Placement &p) {
  for (int i = 0; i < moves.count; ++i) {
    const Placement &m = moves.moves[i];
    if (m.type == p.type && m.rotation == p.rotation && m.x == p.x &&
        m.y == p.y)
      return true;
  }
  return false;
}

// Ports `get_unstability == 0` from script7.js:609-635.
// Every filled cell must be grounded (contiguous from y=0) or horizontally
// adjacent to a grounded cell.
bool no_unstable_garbage(const Grid &g) {
  std::array<std::array<bool, kBoardW>, kBoardH> grounded{};
  for (int x = 0; x < kBoardW; ++x) {
    bool still = true;
    for (int y = 0; y < kBoardH; ++y) {
      if (g.filled(x, y) && still)
        grounded[y][x] = true;
      else
        still = false;
    }
  }
  for (int y = 0; y < kBoardH; ++y) {
    for (int x = 0; x < kBoardW; ++x) {
      if (!g.filled(x, y) || grounded[y][x])
        continue;
      bool adj = (x > 0 && grounded[y][x - 1]) ||
                 (x < kBoardW - 1 && grounded[y][x + 1]);
      if (!adj)
        return false;
    }
  }
  return true;
}

// Ports script7.js:637-658 `is_smooth`: once heights have gone UP by >1,
// they may not go DOWN by >1 again.
bool is_monotonic_smooth(const std::array<int, kBoardW> &h) {
  bool uped = false;
  int last = h[0];
  for (int x = 1; x < kBoardW; ++x) {
    int ele = h[x];
    if (ele - last > 1)
      uped = true;
    else if (last - ele > 1) {
      if (uped)
        return false;
    }
    last = ele;
  }
  return true;
}

// script7.js:670-676: monotonic + sorted outlier clip (sorted[8] - sorted[1]).
bool surface_smooth(const Grid &g) {
  std::array<int, kBoardW> h{};
  for (int x = 0; x < kBoardW; ++x)
    h[x] = g.column_height(x);
  if (!is_monotonic_smooth(h))
    return false;
  std::array<int, kBoardW> sorted_h = h;
  std::sort(sorted_h.begin(), sorted_h.end());
  return sorted_h[8] - sorted_h[1] <= 5;
}

// Ports script7.js:660-700 `is_few_non_cheese_hole`. Returns count so the
// caller can compare against a budget.
int count_non_cheese_holes(const Grid &g) {
  std::array<int, kBoardW> top{};
  for (int x = 0; x < kBoardW; ++x)
    top[x] = g.column_top_row(x);

  bool is_cheese_level = true;
  int non_cheese_holes = 0;
  for (int y = 0; y < kBoardH; ++y) {
    int non_garbages = 0;
    int holes_in_row = 0;
    for (int x = 0; x < kBoardW; ++x) {
      if (!g.filled(x, y)) {
        ++non_garbages;
        if (y < top[x])
          ++holes_in_row;
      }
    }
    if (non_garbages != 1)
      is_cheese_level = false;
    if (!is_cheese_level)
      non_cheese_holes += holes_in_row;
  }
  return non_cheese_holes;
}

// Ports `is_even_distributed` (script7.js:738-763).
//   * Reserved pieces (req.reserved) are never placed during downstack;
//     the reference achieves this by subtracting their counts from the
//     per-piece limit. We just check membership directly.
//   * I + J + L combined <= 4.
//   * No adjacent duplicates (only relevant when req.unique_pieces > 1).
bool piece_allowed(PieceType t, const std::vector<Placement> &placed,
                   const PuzzleRequest &req) {
  for (PieceType r : req.reserved)
    if (r == t)
      return false;

  int count = 0;
  for (const Placement &p : placed)
    if (p.type == t)
      ++count;
  if (count >= req.unique_pieces)
    return false;

  int ijl = (t == PieceType::I || t == PieceType::J || t == PieceType::L) ? 1 : 0;
  for (const Placement &p : placed)
    if (p.type == PieceType::I || p.type == PieceType::J ||
        p.type == PieceType::L)
      ++ijl;
  if (ijl > 4)
    return false;

  if (req.unique_pieces > 1 && !placed.empty() && placed.back().type == t)
    return false;

  return true;
}

// 1-to-1 port of `add_random_line` (script7.js:455-477).
// Insertion point is uniform in [0, max_height+1]. With probability 0.05
// insert 3 consecutive lines, 0.07 (given enough room) insert 2 non-
// consecutive, 0.15 insert 2 consecutive, 0.30 insert 1, otherwise 0.
std::vector<int> maybe_add_skim_lines(Grid &g, std::mt19937 &rng) {
  std::vector<int> added;
  std::uniform_real_distribution<float> u(0.f, 1.f);
  float r = u(rng);

  int max_h = 0;
  for (int x = 0; x < kBoardW; ++x)
    max_h = std::max(max_h, g.column_height(x));

  std::uniform_int_distribution<int> rdist(0, max_h + 1);
  int row_index = rdist(rng);

  auto do_insert = [&](int y) {
    if (y >= kBoardH)
      return;
    add_line(g, y);
    added.push_back(y);
  };

  if (r < 0.05f) {
    do_insert(row_index);
    do_insert(row_index + 1);
    do_insert(row_index + 2);
  } else if (r < 0.07f && row_index < max_h + 1) {
    do_insert(row_index);
    do_insert(row_index + 2);
  } else if (r < 0.15f) {
    do_insert(row_index);
    do_insert(row_index + 1);
  } else if (r < 0.30f) {
    do_insert(row_index);
  }
  return added;
}

// Piece must cover every row in must_cover_rows so forward-play clears them
// (script7.js:711).
bool placement_covers_rows(const Placement &p,
                           const std::vector<int> &must_cover_rows) {
  for (int y : must_cover_rows) {
    bool found = false;
    for (auto c : p.cells())
      if (c.y == y) {
        found = true;
        break;
      }
    if (!found)
      return false;
  }
  return true;
}

// Ports `try_a_piece` + `try_all_pieces` (script7.js:703-796).
//   1. Shuffle (type, rotation, x) candidates.
//   2. For each: try_uncarve_drop to find the landing y.
//   3. Must cover any skim rows.
//   4. Uncarve; verify the placement is a legal hard-drop via movegen.
//   5. Check stability, smoothness, cavity count.
bool try_place_piece(Grid &g, const PuzzleRequest &req, int hole_budget,
                     const std::vector<Placement> &placed,
                     const std::vector<int> &must_cover_rows,
                     std::mt19937 &rng, Placement &out_placement) {
  std::array<PieceType, 7> types{PieceType::I, PieceType::O, PieceType::T,
                                 PieceType::S, PieceType::Z, PieceType::J,
                                 PieceType::L};
  std::shuffle(types.begin(), types.end(), rng);

  struct Candidate { Rotation rot; int x; };
  constexpr int kXLo = -2;
  constexpr int kXHi = Board::kWidth;
  constexpr int kCandN = 4 * (kXHi - kXLo);
  std::array<Candidate, kCandN> candidates;

  for (PieceType t : types) {
    if (!piece_allowed(t, placed, req))
      continue;

    int n = 0;
    for (int r = 0; r < 4; ++r)
      for (int x = kXLo; x < kXHi; ++x)
        candidates[n++] = {static_cast<Rotation>(r), x};
    std::shuffle(candidates.begin(), candidates.begin() + n, rng);

    for (int i = 0; i < n; ++i) {
      Placement p;
      if (!try_uncarve_drop(g, t, candidates[i].rot, candidates[i].x, p))
        continue;

      if (!placement_covers_rows(p, must_cover_rows))
        continue;

      uncarve(g, p);

      // Sonic-only (hard-drop landings + rotations tried at each column).
      // Excludes tucks/kicks-after-drop, approximating the reference's
      // `is_exposed || is_spinable` reachability filter (script7.js:714).
      // Accepting tuck-reachable placements was letting reverse construction
      // generate easy boards that don't require real T-spin access.
      MoveBuffer moves;
      generate_moves(g.to_bitset(), moves, t, /*sonic_only=*/true);
      bool ok = placement_in_moves(moves, p);

      if (ok)
        ok = no_unstable_garbage(g);
      if (ok && req.smooth_surface)
        ok = surface_smooth(g);
      if (ok)
        ok = count_non_cheese_holes(g) <= hole_budget;

      if (ok) {
        out_placement = p;
        return true;
      }
      recarve(g, p);
    }
  }
  return false;
}

// Recursive 5-trial backtracker (`generate_a_ds_map`, script7.js:937-963).
bool reverse_construct(Grid &g, const PuzzleRequest &req, int hole_budget,
                       int remaining, std::vector<Placement> &placed,
                       std::mt19937 &rng) {
  if (remaining == 0)
    return true;

  for (int trial = 0; trial < 5; ++trial) {
    Grid snapshot = g;

    std::vector<int> added_rows;
    if (req.allow_skims)
      added_rows = maybe_add_skim_lines(g, rng);

    Placement p;
    if (!try_place_piece(g, req, hole_budget, placed, added_rows, rng, p)) {
      g = snapshot;
      continue;
    }

    placed.push_back(p);
    if (reverse_construct(g, req, hole_budget, remaining - 1, placed, rng))
      return true;

    placed.pop_back();
    g = snapshot;
  }
  return false;
}

std::vector<PieceType> build_queue(const std::vector<Placement> &placed,
                                    const PuzzleRequest &req) {
  std::vector<PieceType> q;
  q.reserve(placed.size() + req.reserved.size());
  for (auto it = placed.rbegin(); it != placed.rend(); ++it)
    q.push_back(it->type);
  for (PieceType t : req.reserved)
    q.push_back(t);
  return q;
}

std::vector<Placement> build_solution(const std::vector<Placement> &placed) {
  std::vector<Placement> sol;
  sol.reserve(placed.size());
  for (auto it = placed.rbegin(); it != placed.rend(); ++it)
    sol.push_back(*it);
  return sol;
}

} // namespace

// --- Public API -----------------------------------------------------------

// Ports `play_a_map` outer loop (script7.js:965-1013). Retry budget with
// skim disable at attempt 50.
//
// Relaxation strategy at attempt 50+ — user-configurable later. Currently:
//   A. (active) Disable allow_skims — reference behavior.
// Alternatives (commented, one-line swap):
//   B. Reseed rng to fresh_rng — restart from a new base map.
//   C. Bump max_non_cheese_holes by 1 — quality degradation.
std::optional<PuzzleResult> generate_puzzle(const PuzzleRequest &req,
                                            std::mt19937 &rng) {
  if (req.num_pieces < 1 || req.num_pieces > 7)
    return std::nullopt;
  if (req.unique_pieces < 1 || req.unique_pieces > 2)
    return std::nullopt;

  PuzzleRequest local = req;
  constexpr int kSkimDisableAttempt = 50;

  Grid g;
  bool map_valid = false;

  for (int attempt = 0; attempt < local.max_attempts; ++attempt) {
    if (attempt == kSkimDisableAttempt)
      local.allow_skims = false;

    // Regenerate the base map every other iteration (reference regenerates
    // on `i % 2 == 1`, script7.js:995-1001). First iteration always builds.
    bool should_regen = !map_valid || (attempt % 2 == 1);
    if (should_regen) {
      if (!generate_final_map(g, local.garbage_below, rng))
        return std::nullopt;
      map_valid = true;
    }

    // Hole budget: ABSOLUTE cap (matches reference's mdhole_ind check, not
    // baseline-relative). For TSD the initial overhang contributes 1
    // non-cheese hole, so with max_non_cheese_holes=0 the first valid piece
    // placement MUST uncarve the overhang cap to bring the count down to 0.
    // This is the mechanism that hides the TSD setup from the initial
    // board — the player has to rebuild it during forward play.
    int hole_budget = local.max_non_cheese_holes;

    Grid work = g;
    std::vector<Placement> placed;
    placed.reserve(local.num_pieces);

    if (!reverse_construct(work, local, hole_budget, local.num_pieces,
                           placed, rng))
      continue;

    if (work.max_height() >= local.max_height)
      continue;

    PuzzleResult result;
    result.board = work.to_board();
    // Returns the queue in PLAY order. Consumers wanting the hold-shuffle
    // disguise (matches script7.js:991) call PieceQueue::shuffle on the
    // resulting queue — see TSpinPracticeMode::create_queue.
    result.queue = build_queue(placed, local);
    result.solution = build_solution(placed);
    return result;
  }
  return std::nullopt;
}
