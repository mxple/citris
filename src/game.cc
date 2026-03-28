#include "game.h"
#include "srs.h"
#include <algorithm>
#include <iostream>
#include <memory>
#include <ostream>

Game::Game(const Settings &settings, unsigned seed) : settings_(settings) {
  bag_ = std::make_unique<SevenBagRandomizer>(seed);
  now_ = std::chrono::steady_clock::now();
  spawn_piece();
}

void Game::post_event(GameEvent event) {
  event_queue_.push_back(std::move(event));
}

void Game::update(TimePoint now) {
  if (game_over_)
    return;

  now_ = now;

  bool made_progress = true;
  while (made_progress) {
    made_progress = false;

    if (!event_queue_.empty()) {
      made_progress = true;
      auto pending = std::move(event_queue_);
      event_queue_.clear();
      for (auto &ev : pending) {
        process_event(ev);
      }
    }

    for (int i = 0; i < static_cast<int>(TimerKind::N); i++) {
      if (timers_[i] && *timers_[i] <= now_) {
        made_progress = true;
        timers_[i].reset();
        handle_timer(TimerEvent{static_cast<TimerKind>(i)});
      }
    }
  }
}

// TODO attack limits
int Game::drain_attack() {
  int a = pending_attack_;
  pending_attack_ = 0;
  return a;
}

GameState Game::state() const {
  auto pv = bag_->preview(6);
  std::array<PieceType, 6> preview_arr{};
  std::copy_n(pv.begin(), std::min(pv.size(), preview_arr.size()),
              preview_arr.begin());

  return GameState{
      .board = board_,
      .current_piece = current_piece_,
      .ghost_piece = this->compute_ghost(),
      .hold_piece = hold_piece_,
      .hold_available = hold_available_,
      .preview = preview_arr,
      .attack_state = attack_state_,
      .game_over = game_over_,
  };
}

std::optional<TimePoint> Game::next_wakeup() const {
  std::optional<TimePoint> earliest;
  for (auto &t : timers_) {
    if (t && (!earliest || *t < *earliest))
      earliest = t;
  }
  return earliest;
}

void Game::process_event(const GameEvent &event) {
  if (game_over_)
    return;
  std::visit(
      [this](auto &ev) {
        using T = std::decay_t<decltype(ev)>;
        if constexpr (std::is_same_v<T, InputEvent>)
          handle_input(ev);
        else if constexpr (std::is_same_v<T, TimerEvent>)
          handle_timer(ev);
        else if constexpr (std::is_same_v<T, GarbageEvent>)
          handle_garbage(ev);
      },
      event);
  dirty_ = true;
}

void Game::handle_input(const InputEvent &e) {
  switch (e.input) {
  case Input::Left:
  case Input::Right: {
    int dx = (e.input == Input::Left) ? -1 : 1;
    current_piece_.x += dx;
    if (board_.collides(current_piece_)) {
      current_piece_.x -= dx;
    } else {
      last_move_was_rotation_ = false;
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
      current_piece_ = *result;
      last_move_was_rotation_ = true;
      post_move_timers();
    }
    break;
  }
  case Input::SoftDrop: {
    if (drop1()) {
      last_move_was_rotation_ = false;
      if (!is_grounded()) {
        cancel_timer(TimerKind::Gravity);
        schedule_timer(TimerKind::Gravity, settings_.gravity_interval);
      } else {
        post_move_timers();
      }
    }
    break;
  }
  case Input::HardDrop: {
    current_piece_ = compute_ghost();
    lock_piece();
    break;
  }
  case Input::Hold: {
    if (!hold_available_)
      break;
    hold_available_ = false;
    cancel_timer(TimerKind::Gravity);
    cancel_timer(TimerKind::LockDelay);
    if (hold_piece_.has_value()) {
      auto temp = current_piece_.type;
      current_piece_ = Piece(hold_piece_.value());
      hold_piece_ = temp;
      lock_resets_remaining_ = settings_.max_lock_resets;
      if (board_.collides(current_piece_)) {
        game_over_ = true;
        return;
      }
      schedule_timer(TimerKind::Gravity, settings_.gravity_interval);
    } else {
      hold_piece_ = current_piece_.type;
      spawn_piece();
    }
    break;
  }
  }

}

void Game::handle_timer(const TimerEvent &e) {
  switch (e.kind) {
  case TimerKind::Gravity: {
    if (drop1()) {
      last_move_was_rotation_ = false;
      if (is_grounded()) {
        schedule_timer(TimerKind::LockDelay, settings_.lock_delay);
      } else {
        schedule_timer(TimerKind::Gravity, settings_.gravity_interval);
      }
    } else {
      schedule_timer(TimerKind::LockDelay, settings_.lock_delay);
    }
    break;
  }
  case TimerKind::LockDelay: {
    lock_piece();
    break;
  }
  case TimerKind::GarbageDelay: {
    for (auto &g : pending_garbage_) {
      board_.add_garbage(g.lines, g.gap_col);
    }
    pending_garbage_.clear();
    break;
  }
  case TimerKind::N:
    break;
  }
}

void Game::handle_garbage(const GarbageEvent &e) {
  pending_garbage_.push_back({e.lines, e.gap_col});
  schedule_timer(TimerKind::GarbageDelay, settings_.garbage_delay);
}

void Game::spawn_piece() {
  current_piece_ = Piece(bag_->next());
  lock_resets_remaining_ = settings_.max_lock_resets;
  last_move_was_rotation_ = false;

  if (board_.collides(current_piece_)) {
    game_over_ = true;
    return;
  }

  schedule_timer(TimerKind::Gravity, settings_.gravity_interval);
}

void Game::lock_piece() {
  board_.place(current_piece_);

  auto spin = SpinKind::None;
  if (last_move_was_rotation_) {
    spin = board_.detect_spin(current_piece_);
  }

  int cleared = board_.clear_lines();
  pending_attack_ +=
      compute_attack_and_update_state(attack_state_, cleared, spin);

  hold_available_ = true;
  cancel_timer(TimerKind::Gravity);
  cancel_timer(TimerKind::LockDelay);
  spawn_piece();
}

Piece Game::compute_ghost() const {
  Piece copy = current_piece_;
  while (!board_.collides(copy)) {
    copy.y -= 1;
  }
  copy.y += 1;
  return copy;
}

bool Game::drop1() {
  current_piece_.y -= 1;
  if (!board_.collides(current_piece_)) {
    dirty_ = true;
    return true;
  }
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
      schedule_timer(TimerKind::LockDelay, settings_.lock_delay);
    }
    cancel_timer(TimerKind::Gravity);
  } else {
    cancel_timer(TimerKind::LockDelay);
    if (!timers_[static_cast<int>(TimerKind::Gravity)]) {
      schedule_timer(TimerKind::Gravity, settings_.gravity_interval);
    }
  }
}

void Game::schedule_timer(TimerKind kind, Duration fire_time) {
  timers_[static_cast<size_t>(kind)] = now_ + fire_time;
}

void Game::cancel_timer(TimerKind kind) {
  timers_[static_cast<size_t>(kind)].reset();
}
