#include "game.h"
#include "srs.h"

Game::Game(const Settings &settings, Stats &stats, TimerManager &timers,
           unsigned seed)
    : settings_(settings), stats_(stats), timers_(timers) {
  bag_ = std::make_unique<SevenBagRandomizer>(seed);
  now_ = std::chrono::steady_clock::now();
  spawn_piece();
}

bool Game::process(const Event &ev, TimePoint now) {
  std::visit(
      [&](auto &&e) {
        using T = std::decay_t<decltype(e)>;
        if (game_over_)
          return true;
        now_ = now;
        if constexpr (std::is_same_v<T, MoveInput>) {
          handle_move(e);
        } else if constexpr (std::is_same_v<T, Gravity>) {
          handle_gravity();
        } else if constexpr (std::is_same_v<T, LockDelayExpired>) {
          handle_lock_delay_expired();
        } else if constexpr (std::is_same_v<T, GarbageReceived>) {
          handle_garbage_received(e);
        } else if constexpr (std::is_same_v<T, GarbageDelayExpired>) {
          handle_garbage_delay_expired();
        } else if constexpr (std::is_same_v<T, ARRActive>) {
          arr_direction_ = e.direction;
        } else if constexpr (std::is_same_v<T, ARRInactive>) {
          arr_direction_.reset();
        } else if constexpr (std::is_same_v<T, SoftDropActive>) {
          soft_drop_active_ = true;
        } else if constexpr (std::is_same_v<T, SoftDropInactive>) {
          soft_drop_active_ = false;
        }
        return true;
      },
      ev);
  return false;
}

GameState Game::state() const {
  auto pv = bag_->preview(6);
  std::array<PieceType, 6> preview_arr{};
  std::copy_n(pv.begin(), std::min(pv.size(), preview_arr.size()),
              preview_arr.begin());

  return GameState{
      .board = board_,
      .current_piece = current_piece_,
      .ghost_piece = compute_ghost(),
      .hold_piece = hold_piece_,
      .hold_available = hold_available_,
      .preview = preview_arr,
      .attack_state = attack_state_,
      .game_over = game_over_,
      .piece_gen = piece_gen_,
      .last_clear = last_clear_,
  };
}

int Game::drain_attack() {
  int a = pending_attack_;
  pending_attack_ = 0;
  return a;
}

void Game::handle_move(const MoveInput &e) {
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
        timers_.cancel(TimerKind::Gravity);
        arm_gravity();
      } else {
        post_move_timers();
      }
    }
    break;
  }
  case Input::HardDrop: {
    if (now_ < hard_drop_blocked_until_)
      break;
    current_piece_ = compute_ghost();
    lock_piece();
    dirty_ = true;
    break;
  }
  case Input::Hold: {
    if (!hold_available_)
      break;
    dirty_ = true;
    if (!settings_.infinite_hold)
      hold_available_ = false;
    timers_.cancel(TimerKind::Gravity);
    timers_.cancel(TimerKind::LockDelay);
    if (hold_piece_.has_value()) {
      auto temp = current_piece_.type;
      current_piece_ = Piece(hold_piece_.value());
      hold_piece_ = temp;
      piece_gen_++;
      lock_resets_remaining_ = settings_.max_lock_resets;
      if (board_.collides(current_piece_)) {
        game_over_ = true;
        return;
      }
      settle();
      arm_gravity();
    } else {
      hold_piece_ = current_piece_.type;
      spawn_piece();
    }
    break;
  }
  }
}

void Game::handle_gravity() {
  if (drop1()) {
    dirty_ = true;
    last_move_was_rotation_ = false;
    settle();
    if (is_grounded()) {
      arm_lock_delay();
    } else {
      arm_gravity();
    }
  } else {
    arm_lock_delay();
  }
}

void Game::handle_lock_delay_expired() {
  lock_piece();
  hard_drop_blocked_until_ = now_ + settings_.hard_drop_delay;
  dirty_ = true;
}

void Game::handle_garbage_received(const GarbageReceived &e) {
  pending_garbage_.push_back({e.lines, e.gap_col});
  timers_.schedule(TimerKind::GarbageDelay, now_ + settings_.garbage_delay,
                   GarbageDelayExpired{});
}

void Game::handle_garbage_delay_expired() {
  for (auto &g : pending_garbage_) {
    board_.add_garbage(g.lines, g.gap_col);
  }
  pending_garbage_.clear();
  dirty_ = true;
}

void Game::spawn_piece() {
  current_piece_ = Piece(bag_->next());
  piece_gen_++;
  lock_resets_remaining_ = settings_.max_lock_resets;
  last_move_was_rotation_ = false;

  if (board_.collides(current_piece_)) {
    game_over_ = true;
    return;
  }

  settle();
  arm_gravity();
  push_snapshot();
}

void Game::lock_piece() {
  // Detect spin BEFORE placing. AllSpin immobility check must not see the
  // piece's own cells on the board.
  auto spin = SpinKind::None;
  if (last_move_was_rotation_)
    spin = board_.detect_spin(current_piece_);

  board_.place(current_piece_);

  int cleared = board_.clear_lines();
  int attack = compute_attack_and_update_state(attack_state_, cleared, spin);
  pending_attack_ += attack;

  bool pc = cleared > 0 && board_.is_empty();
  if (cleared > 0 || spin != SpinKind::None) {
    last_clear_ = {cleared, spin, pc, piece_gen_};
  }

  stats_.add_piece();
  stats_.add_lines(cleared);
  stats_.add_attack(attack);
  stats_.set_combo(attack_state_.combo);
  stats_.set_b2b(attack_state_.b2b);
  if (pc)
    stats_.add_perfect_clear();

  hold_available_ = true;
  timers_.cancel(TimerKind::Gravity);
  timers_.cancel(TimerKind::LockDelay);
  spawn_piece();
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
    timers_.cancel(TimerKind::Gravity);
  } else {
    timers_.cancel(TimerKind::LockDelay);
    if (!timers_.active(TimerKind::Gravity))
      arm_gravity();
  }
}

void Game::arm_gravity() {
  if (settings_.gravity_interval < std::chrono::milliseconds{0})
    return;
  if (settings_.gravity_interval == std::chrono::milliseconds{0}) {
    apply_20g();
    return;
  }
  timers_.schedule(TimerKind::Gravity, now_ + settings_.gravity_interval,
                   Gravity{});
}

void Game::apply_20g() {
  current_piece_ = compute_ghost();
  dirty_ = true;
  arm_lock_delay();
}

void Game::arm_lock_delay() {
  if (settings_.lock_delay < std::chrono::milliseconds{0})
    return;
  timers_.schedule(TimerKind::LockDelay, now_ + settings_.lock_delay,
                   LockDelayExpired{});
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

void Game::settle() {
  while (apply_sonic_drop() | apply_arr0()) {}
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
      game_over_,
      last_move_was_rotation_,
      bag_->snapshot(),
      stats_.snapshot(),
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
  game_over_ = snap.game_over;
  last_move_was_rotation_ = snap.last_move_was_rotation;
  bag_->restore(snap.bag_snapshot);
  stats_.restore_for_undo(snap.stats_snapshot);

  timers_.cancel(TimerKind::Gravity);
  timers_.cancel(TimerKind::LockDelay);
  timers_.cancel(TimerKind::GarbageDelay);

  arr_direction_.reset();
  soft_drop_active_ = false;
  hard_drop_blocked_until_ = {};

  settle();
  arm_gravity();
  dirty_ = true;
}

bool Game::undo() {
  if (undo_stack_.size() <= 1)
    return false;
  undo_stack_.pop_back();
  restore_snapshot(undo_stack_.back());
  return true;
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
