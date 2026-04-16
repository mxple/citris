#include "game.h"
#include "presets/game_mode.h"
#include "srs.h"

Game::Game(const GameMode &mode, Board board, unsigned seed)
    : mode_(mode), board_(std::move(board)) {
  bag_ = mode_.create_bag(seed);
  now_ = SdlClock::now();
  spawn_piece();
}

void Game::apply(const CommandBuffer &cmds) {
  for (const auto &cmd : cmds) {
    if (game_over_)
      break;
    std::visit(
        [&](auto &&c) {
          using T = std::decay_t<decltype(c)>;
          if constexpr (std::is_same_v<T, cmd::MovePiece>) {
            handle_move(c);
          }
          if constexpr (std::is_same_v<T, cmd::SetARRDirection>) {
            arr_direction_ = c.direction;
          }
          if constexpr (std::is_same_v<T, cmd::SetSoftDropActive>) {
            soft_drop_active_ = c.active;
          }
          if constexpr (std::is_same_v<T, cmd::AddGarbage>) {
            handle_garbage_received(c.lines, c.gap_col, c.immediate);
          }
          if constexpr (std::is_same_v<T, cmd::SetGameOver>) {
            handle_set_game_over(c.won);
          }
          if constexpr (std::is_same_v<T, cmd::Undo>) {
            handle_undo();
          }
          if constexpr (std::is_same_v<T, cmd::Passthrough>) {
            pending_events_.push_back(c.notification);
          }
        },
        cmd);
  }
}

void Game::tick(TimePoint now) {
  if (game_over_)
    return;
  now_ = now;

  if (gravity_deadline_ && now_ >= *gravity_deadline_) {
    TimePoint expired = *gravity_deadline_;
    gravity_deadline_.reset();
    handle_gravity(expired);
  }
  if (lock_delay_deadline_ && now_ >= *lock_delay_deadline_) {
    lock_delay_deadline_.reset();
    handle_lock_delay_expired();
  }
  if (garbage_delay_deadline_ && now_ >= *garbage_delay_deadline_) {
    garbage_delay_deadline_.reset();
    handle_garbage_delay_expired();
  }
}

GameState Game::state() const {
  return GameState{
      .board = board_,
      .current_piece = current_piece_,
      .ghost_piece = compute_ghost(),
      .hold_piece = hold_piece_,
      .hold_available = hold_available_,
      .preview = bag_->preview(6),
      .attack_state = attack_state_,
      .game_over = game_over_,
      .won = won_,
      .piece_gen = piece_gen_,
      .bag_draws = bag_->draws(),
      .lines_cleared = lines_cleared_,
      .total_attack = total_attack_,
      .last_clear = last_clear_,
  };
}

int Game::drain_attack() {
  int a = pending_attack_;
  pending_attack_ = 0;
  return a;
}

std::optional<TimePoint> Game::next_deadline() const {
  std::optional<TimePoint> earliest;
  auto consider = [&](const std::optional<TimePoint> &tp) {
    if (tp && (!earliest || *tp < *earliest))
      earliest = tp;
  };
  consider(gravity_deadline_);
  consider(lock_delay_deadline_);
  consider(garbage_delay_deadline_);
  return earliest;
}

void Game::handle_move(const cmd::MovePiece &e) {
  switch (e.input) {
  case Input::Left:
  case Input::Right: {
    int dx = (e.input == Input::Left) ? -1 : 1;
    current_piece_.x += dx;
    if (board_.collides(current_piece_)) {
      current_piece_.x -= dx;
    } else {
      dirty_ = true;
      last_move_was_rotation_ = false;
      apply_sonic_drop();
      post_move_timers();
    }
    break;
  }
  case Input::RotateCW:
  case Input::RotateCCW:
  case Input::Rotate180: {
    auto rotation_func = (e.input == Input::RotateCW)    ? rotate_cw
                         : (e.input == Input::RotateCCW) ? rotate_ccw
                                                         : rotate_180;
    auto result = try_rotate(board_, current_piece_,
                             rotation_func(current_piece_.rotation));
    if (result) {
      dirty_ = true;
      current_piece_ = *result;
      last_move_was_rotation_ = true;
      settle();
      post_move_timers();
    }
    break;
  }
  case Input::SoftDrop: {
    if (drop1()) {
      dirty_ = true;
      last_move_was_rotation_ = false;
      settle();
      if (!is_grounded()) {
        gravity_deadline_.reset();
        arm_gravity();
      } else {
        post_move_timers();
      }
    }
    break;
  }
  case Input::HardDrop: {
    current_piece_ = compute_ghost();
    lock_piece();
    dirty_ = true;
    break;
  }
  case Input::LLeft:
  case Input::RRight: {
    int dx = (e.input == Input::LLeft) ? -1 : 1;
    int start_x = current_piece_.x;
    while (true) {
      current_piece_.x += dx;
      if (board_.collides(current_piece_)) {
        current_piece_.x -= dx;
        break;
      }
    }
    if (current_piece_.x != start_x) {
      dirty_ = true;
      last_move_was_rotation_ = false;
      apply_sonic_drop();
      post_move_timers();
    }
    break;
  }
  case Input::SonicDrop: {
    Piece ghost = compute_ghost();
    if (ghost.y != current_piece_.y) {
      current_piece_.y = ghost.y;
      dirty_ = true;
      last_move_was_rotation_ = false;
      post_move_timers();
    }
    break;
  }
  case Input::Hold: {
    if (!mode_.hold_allowed() || !hold_available_)
      break;
    dirty_ = true;
    if (!mode_.infinite_hold())
      hold_available_ = false;
    gravity_deadline_.reset();
    lock_delay_deadline_.reset();
    auto old_type = current_piece_.type;
    if (hold_piece_.has_value()) {
      current_piece_ = Piece(*hold_piece_);
      hold_piece_ = old_type;
      piece_gen_++;
      lock_resets_remaining_ = mode_.max_lock_resets();
      if (top_out())
        return;
      settle();
      arm_gravity();
    } else {
      hold_piece_ = old_type;
      spawn_piece();
    }
    pending_events_.push_back(eng::HoldUsed{current_piece_.type, old_type});
    break;
  }
  }
}

void Game::handle_gravity(TimePoint expired_at) {
  if (drop1()) {
    dirty_ = true;
    last_move_was_rotation_ = false;
    settle();
    if (is_grounded()) {
      arm_lock_delay();
    } else {
      arm_gravity(expired_at);
    }
  } else {
    arm_lock_delay();
  }
}

void Game::handle_lock_delay_expired() {
  lock_piece();
  pending_events_.push_back(eng::LockDelayExpired{mode_.hard_drop_delay()});
  dirty_ = true;
}

void Game::handle_garbage_received(int lines, int gap_col, bool immediate) {
  if (immediate) {
    board_.add_garbage(lines, gap_col);
    pending_events_.push_back(eng::GarbageMaterialized{lines});
    dirty_ = true;
  } else {
    pending_garbage_.push_back({lines, gap_col});
    garbage_delay_deadline_ = now_ + mode_.garbage_delay();
  }
}

void Game::handle_garbage_delay_expired() {
  int total_lines = 0;
  for (auto &g : pending_garbage_) {
    board_.add_garbage(g.lines, g.gap_col);
    total_lines += g.lines;
  }
  pending_garbage_.clear();
  if (total_lines > 0)
    pending_events_.push_back(eng::GarbageMaterialized{total_lines});
  dirty_ = true;
}

void Game::handle_set_game_over(bool won) {
  if (game_over_)
    return;
  game_over_ = true;
  won_ = won;
  pending_events_.push_back(eng::GameOver{won});
  dirty_ = true;
}

void Game::handle_undo() {
  if (undo_stack_.size() <= 1)
    return;
  undo_stack_.pop_back();
  restore_snapshot(undo_stack_.back());
  pending_events_.push_back(eng::UndoPerformed{});
}

void Game::spawn_piece() {
  if (bag_->preview(1).empty())
    return; // finite queue exhausted — mode handles via on_piece_locked

  current_piece_ = Piece(bag_->next());
  piece_gen_++;
  lock_resets_remaining_ = mode_.max_lock_resets();
  last_move_was_rotation_ = false;

  if (top_out())
    return;

  pending_events_.push_back(eng::PieceSpawned{current_piece_.type});

  settle();
  arm_gravity();
  push_snapshot();
}

void Game::lock_piece() {
  auto spin = SpinKind::None;
  if (last_move_was_rotation_)
    spin = board_.detect_spin(current_piece_);

  auto locked_type = current_piece_.type;
  auto locked_rot = current_piece_.rotation;
  auto locked_x = static_cast<int8_t>(current_piece_.x);
  auto locked_y = static_cast<int8_t>(current_piece_.y);
  board_.place(current_piece_);

  int cleared = board_.clear_lines();
  lines_cleared_ += cleared;
  int prev_combo = attack_state_.combo;
  int attack = compute_attack_and_update_state(attack_state_, cleared, spin);
  pending_attack_ += attack;
  total_attack_ += attack;

  bool pc = cleared > 0 && board_.is_empty();
  if (pc) {
    attack += 10;
    pending_attack_ += 10;
    total_attack_ += 10;
  }
  if (cleared > 0 || spin != SpinKind::None) {
    last_clear_ = {cleared, spin, pc, piece_gen_};
  }

  pending_events_.push_back(
      eng::PieceLocked{locked_type, locked_rot, locked_x, locked_y,
                       cleared, spin, pc, attack, prev_combo,
                       attack_state_.combo, attack_state_.b2b});

  hold_available_ = true;
  gravity_deadline_.reset();
  lock_delay_deadline_.reset();
  spawn_piece();
}

bool Game::top_out() {
  if (!board_.collides(current_piece_))
    return false;
  game_over_ = true;
  won_ = false;
  pending_events_.push_back(eng::GameOver{false});
  return true;
}

void Game::push_snapshot() {
  if (undo_stack_.size() >= kMaxUndoDepth)
    undo_stack_.pop_front();
  undo_stack_.push_back(GameSnapshot{
      board_,
      current_piece_,
      hold_piece_,
      hold_available_,
      attack_state_,
      lock_resets_remaining_,
      pending_garbage_,
      last_clear_,
      piece_gen_,
      pending_attack_,
      lines_cleared_,
      total_attack_,
      game_over_,
      won_,
      last_move_was_rotation_,
      bag_->snapshot(),
  });
}

void Game::restore_snapshot(const GameSnapshot &snap) {
  board_ = snap.board;
  current_piece_ = snap.current_piece;
  hold_piece_ = snap.hold_piece;
  hold_available_ = snap.hold_available;
  attack_state_ = snap.attack_state;
  lock_resets_remaining_ = snap.lock_resets_remaining;
  pending_garbage_ = snap.pending_garbage;
  last_clear_ = snap.last_clear;
  piece_gen_ = snap.piece_gen;
  pending_attack_ = snap.pending_attack;
  lines_cleared_ = snap.lines_cleared;
  total_attack_ = snap.total_attack;
  game_over_ = snap.game_over;
  won_ = snap.won;
  last_move_was_rotation_ = snap.last_move_was_rotation;
  bag_->restore(snap.bag_snapshot);

  gravity_deadline_.reset();
  lock_delay_deadline_.reset();
  garbage_delay_deadline_.reset();

  arr_direction_.reset();
  soft_drop_active_ = false;

  settle();
  arm_gravity();
  dirty_ = true;
}

Piece Game::compute_ghost() const {
  Piece copy = current_piece_;
  while (!board_.collides(copy))
    copy.y -= 1;
  copy.y += 1;
  return copy;
}

bool Game::drop1() {
  current_piece_.y -= 1;
  if (!board_.collides(current_piece_))
    return true;
  current_piece_.y += 1;
  return false;
}

bool Game::is_grounded() const {
  Piece test = current_piece_;
  test.y -= 1;
  return board_.collides(test);
}

void Game::post_move_timers() {
  if (is_grounded()) {
    if (lock_resets_remaining_ > 0) {
      --lock_resets_remaining_;
      arm_lock_delay();
    } else {
      lock_piece();
    }
    gravity_deadline_.reset();
  } else {
    lock_delay_deadline_.reset();
    if (!gravity_deadline_)
      arm_gravity();
  }
}

void Game::arm_gravity(std::optional<TimePoint> chain_from) {
  if (mode_.gravity_interval() < std::chrono::milliseconds{0})
    return;
  if (mode_.gravity_interval() == std::chrono::milliseconds{0}) {
    apply_20g();
    return;
  }
  gravity_deadline_ = chain_from.value_or(now_) + mode_.gravity_interval();
}

void Game::arm_lock_delay() {
  if (mode_.lock_delay() < std::chrono::milliseconds{0})
    return;
  lock_delay_deadline_ = now_ + mode_.lock_delay();
}

void Game::apply_20g() {
  current_piece_ = compute_ghost();
  dirty_ = true;
  arm_lock_delay();
}

void Game::settle() {
  while (apply_sonic_drop() | apply_arr0())
    ;
}

bool Game::apply_sonic_drop() {
  if (!soft_drop_active_)
    return false;
  bool moved = false;
  while (true) {
    current_piece_.y -= 1;
    if (board_.collides(current_piece_)) {
      current_piece_.y += 1;
      break;
    }
    moved = true;
  }
  if (moved) {
    dirty_ = true;
    return true;
  }
  return false;
}

bool Game::apply_arr0() {
  if (!arr_direction_)
    return false;
  int dx = (*arr_direction_ == Input::Left) ? -1 : 1;
  bool moved = false;
  while (true) {
    current_piece_.x += dx;
    if (board_.collides(current_piece_)) {
      current_piece_.x -= dx;
      break;
    }
    moved = true;
  }
  if (moved) {
    dirty_ = true;
    return true;
  }
  return false;
}
