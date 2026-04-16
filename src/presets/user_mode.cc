#include "user_mode.h"
#include "engine/board.h"
#include <algorithm>
#include <cstdio>

UserMode::UserMode(UserModeConfig config) : config_(std::move(config)) {
  if (needs_bank())
    bank_ = std::make_unique<PuzzleBank>(
        [this](std::mt19937 &rng) { return generate(rng); });
}

// --- Helpers ---

int UserMode::total_pieces() const {
  if (config_.count > 0)
    return config_.count;
  if (config_.continuation == UserModeConfig::Continuation::None)
    return static_cast<int>(config_.initial_queue.size());
  return 0; // infinite
}

// --- GameMode overrides ---

std::unique_ptr<BagRandomizer> UserMode::create_bag(unsigned seed) const {
  // Finite queue: entire queue was pre-built in generate_queue().
  if (finite_queue() && has_current_)
    return std::make_unique<FixedQueueRandomizer>(current_.queue, seed);

  // Infinite with initial prefix.
  if (!config_.initial_queue.empty()) {
    auto prefix = config_.initial_queue;
    if (config_.shuffle_initial)
      std::shuffle(prefix.begin(), prefix.end(),
                   std::mt19937{std::random_device{}()});
    return std::make_unique<PrefixedBagRandomizer>(std::move(prefix), seed);
  }

  // Infinite, no prefix: standard 7-bag.
  return std::make_unique<SevenBagRandomizer>(seed);
}

void UserMode::on_start(TimePoint now) {
  GameMode::on_start(now);
  pieces_placed_ = 0;
  pcs_ = tsds_ = tsts_ = quads_ = 0;
  max_combo_ = max_b2b_ = 0;
}

void UserMode::setup_board(Board &board) {
  if (last_was_win_ || !has_current_) {
    if (bank_) {
      if (auto s = bank_->pop()) {
        current_ = std::move(*s);
      } else {
        // Pool empty — synchronous fallback.
        for (int i = 0; i < 100; ++i)
          if (auto s = generate(fallback_rng_)) {
            current_ = std::move(*s);
            break;
          }
      }
    } else {
      // No bank needed — build setup directly from config.
      current_.board = generate_board(fallback_rng_);
      current_.queue = generate_queue(fallback_rng_);
    }
    has_current_ = true;
  }
  // On loss: replay same setup (current_ unchanged).
  board = current_.board;
  pieces_placed_ = 0;
  pcs_ = tsds_ = tsts_ = quads_ = 0;
  max_combo_ = max_b2b_ = 0;
}

void UserMode::on_piece_locked(const eng::PieceLocked &ev,
                               const GameState &state, CommandBuffer &cmds) {
  pieces_placed_++;

  // Accumulate goal counters.
  if (ev.perfect_clear)
    pcs_++;
  if (ev.spin == SpinKind::TSpin && ev.lines_cleared == 2)
    tsds_++;
  if (ev.spin == SpinKind::TSpin && ev.lines_cleared == 3)
    tsts_++;
  if (ev.lines_cleared >= 4)
    quads_++;
  max_combo_ = std::max(max_combo_, ev.new_combo);
  max_b2b_ = std::max(max_b2b_, ev.new_b2b);

  // Check win.
  bool all_placed =
      finite_queue() && pieces_placed_ >= total_pieces();

  if (config_.use_all_queue) {
    // Evaluate only after all pieces are placed.
    if (all_placed) {
      if (goals_met(state)) {
        last_was_win_ = true;
        solves_++;
      } else {
        last_was_win_ = false;
      }
      attempts_++;
      cmds.push(cmd::SetGameOver{last_was_win_});
    }
  } else {
    // Evaluate after each piece lock.
    if (goals_met(state)) {
      last_was_win_ = true;
      solves_++;
      attempts_++;
      cmds.push(cmd::SetGameOver{true});
    } else if (all_placed) {
      // Ran out of pieces without meeting goals.
      last_was_win_ = false;
      attempts_++;
      cmds.push(cmd::SetGameOver{false});
    }
  }
}

void UserMode::on_undo(const GameState &) {
  if (pieces_placed_ > 0)
    pieces_placed_--;
  // Note: accumulated counters (tsds_, pcs_, etc.) are not decremented.
  // This is imprecise but acceptable for training — undo is rare and
  // the counters only affect win evaluation, not scoring.
}

void UserMode::fill_hud(HudData &hud, const GameState &state, TimePoint now) {
  // Center: remaining pieces (finite) or blank.
  if (finite_queue()) {
    int remaining = total_pieces() - pieces_placed_;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", remaining);
    hud.center_text = buf;
  }

  if (state.game_over && !end_time_)
    end_time_ = now;

  if (state.game_over) {
    hud.game_over_label = state.won ? "CLEAR!" : "FAILED";
    hud.game_over_label_color =
        state.won ? Color(100, 255, 100) : Color(255, 100, 100);

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%d / %d", solves_, attempts_);
    hud.game_over_detail = buf;
    hud.game_over_detail_color = Color(200, 200, 200);
  }
}

// --- Goal evaluation ---

bool UserMode::goals_met(const GameState &state) const {
  if (config_.pc > 0 && pcs_ < config_.pc)
    return false;
  if (config_.tsd > 0 && tsds_ < config_.tsd)
    return false;
  if (config_.tst > 0 && tsts_ < config_.tst)
    return false;
  if (config_.quad > 0 && quads_ < config_.quad)
    return false;
  if (config_.combo > 0 && max_combo_ < config_.combo)
    return false;
  if (config_.b2b > 0 && max_b2b_ < config_.b2b)
    return false;
  if (config_.lines > 0 && state.lines_cleared < config_.lines)
    return false;

  // Board quality conditions.
  if (config_.max_holes >= 0) {
    int holes = 0;
    for (int col = 0; col < Board::kWidth; ++col) {
      bool found_filled = false;
      for (int row = Board::kTotalHeight - 1; row >= 0; --row) {
        if (state.board.filled(col, row))
          found_filled = true;
        else if (found_filled)
          holes++;
      }
    }
    if (holes > config_.max_holes)
      return false;
  }

  if (config_.max_overhangs >= 0) {
    // An overhang: a column where an empty cell has a filled cell above it
    // with no sky access (blocked from above by filled cells).
    int overhangs = 0;
    for (int col = 0; col < Board::kWidth; ++col) {
      bool has_overhang = false;
      bool found_filled = false;
      for (int row = Board::kTotalHeight - 1; row >= 0; --row) {
        if (state.board.filled(col, row))
          found_filled = true;
        else if (found_filled) {
          has_overhang = true;
          break;
        }
      }
      if (has_overhang)
        overhangs++;
    }
    if (overhangs > config_.max_overhangs)
      return false;
  }

  // If no conditions were specified at all, don't auto-win.
  bool has_any_goal = config_.pc > 0 || config_.tsd > 0 || config_.tst > 0 ||
                      config_.quad > 0 || config_.combo > 0 ||
                      config_.b2b > 0 || config_.lines > 0 ||
                      config_.max_holes >= 0 || config_.max_overhangs >= 0;
  return has_any_goal;
}

// --- Setup generation ---

std::optional<GeneratedSetup> UserMode::generate(std::mt19937 &rng) {
  Board board = generate_board(rng);
  std::vector<PieceType> queue = generate_queue(rng);
  if (config_.needs_validation() && !validate_setup(board, queue))
    return std::nullopt;
  return GeneratedSetup{std::move(board), std::move(queue)};
}

Board UserMode::generate_board(std::mt19937 &rng) {
  using B = UserModeConfig::BoardType;
  Board board;

  switch (config_.board_type) {
  case B::Empty:
    break;

  case B::Predetermined:
    for (int row = 0;
         row < static_cast<int>(config_.board_rows.size()) &&
         row < Board::kTotalHeight;
         ++row) {
      uint16_t mask = config_.board_rows[row];
      for (int col = 0; col < Board::kWidth; ++col)
        if (mask & (1 << col))
          board.set_cell(col, row, CellColor::Garbage);
    }
    break;

  case B::Generated:
    // TODO: random board generation with constraints
    break;
  }

  return board;
}

std::vector<PieceType> UserMode::generate_queue(std::mt19937 &rng) {
  std::vector<PieceType> queue;

  // Initial pieces.
  queue = config_.initial_queue;
  if (config_.shuffle_initial && queue.size() > 1)
    std::shuffle(queue.begin(), queue.end(), rng);

  // For infinite queues, we don't pre-build — handled by create_bag().
  if (!finite_queue())
    return queue;

  // Fill remaining with continuation.
  int remaining = total_pieces() - static_cast<int>(queue.size());
  if (remaining <= 0)
    return queue;

  switch (config_.continuation) {
  case UserModeConfig::Continuation::SevenBag: {
    SevenBagRandomizer bag(rng());
    for (int i = 0; i < remaining; ++i)
      queue.push_back(bag.next());
    break;
  }
  case UserModeConfig::Continuation::Random: {
    std::uniform_int_distribution<int> dist(0, kPieceTypeN - 1);
    for (int i = 0; i < remaining; ++i)
      queue.push_back(static_cast<PieceType>(dist(rng)));
    break;
  }
  case UserModeConfig::Continuation::None:
    break;
  }

  return queue;
}

bool UserMode::validate_setup(const Board &board,
                              const std::vector<PieceType> &queue) {
  // Validation is derived from goal conditions.
  // Only called when needs_validation() is true (generated board + goal
  // requires specific clears).

  if (config_.pc > 0) {
    // TODO: call find_perfect_clear(board, queue, ...)
    // return false if not solvable
  }

  if (config_.tsd > 0 || config_.tst > 0) {
    // TODO: beam search with TsdEvaluator, check PV for required clears
    // return false if not achievable
  }

  return true;
}
