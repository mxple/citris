#include "human_player.h"
#include "board.h"
#include <iostream>
#include <ostream>

HumanPlayer::HumanPlayer(const Settings &settings) : settings_(settings) {
  key_map_[settings.move_left] = Input::Left;
  key_map_[settings.move_right] = Input::Right;
  key_map_[settings.rotate_cw] = Input::RotateCW;
  key_map_[settings.rotate_ccw] = Input::RotateCCW;
  key_map_[settings.rotate_180] = Input::Rotate180;
  key_map_[settings.hard_drop] = Input::HardDrop;
  key_map_[settings.soft_drop] = Input::SoftDrop;
  key_map_[settings.hold] = Input::Hold;
}

std::optional<Input> HumanPlayer::poll(const GameState &state) {
  if (buffer_.empty())
    return std::nullopt;
  auto input = buffer_.front();
  buffer_.pop_front();
  return input;
}

void HumanPlayer::on_key_pressed(sf::Keyboard::Key key) {
  auto input = key_to_input(key);
  if (!input)
    return;

  auto now = std::chrono::steady_clock::now();

  switch (*input) {
  case Input::Left: {
    if (!das_left_.held) {
      das_left_.held = true;
      das_left_.das_charged = false;
      das_left_.press_time = now;
      buffer_.push_back(Input::Left);
    }
    // Last-pressed wins: Left takes over from Right.
    active_direction_ = Input::Left;
    break;
  }
  case Input::Right: {
    if (!das_right_.held) {
      das_right_.held = true;
      das_right_.das_charged = false;
      das_right_.press_time = now;
      buffer_.push_back(Input::Right);
    }
    active_direction_ = Input::Right;
    break;
  }
  case Input::SoftDrop: {
    if (!soft_drop_held_) {
      soft_drop_held_ = true;
      last_soft_drop_ = now;
      buffer_.push_back(Input::SoftDrop);
    }
    break;
  }
  default:
    // Instant actions: rotate, hard drop, hold.
    buffer_.push_back(*input);
    break;
  }
}

void HumanPlayer::on_key_released(sf::Keyboard::Key key) {
  auto input = key_to_input(key);
  if (!input)
    return;

  switch (*input) {
  case Input::Left:
    das_left_.held = false;
    if (!settings_.das_preserve_charge)
      das_left_.das_charged = false;
    // If Left was active and Right is still held, Right resumes.
    if (active_direction_ == Input::Left) {
      active_direction_ =
          das_right_.held ? std::optional(Input::Right) : std::nullopt;
    }
    break;
  case Input::Right:
    das_right_.held = false;
    if (!settings_.das_preserve_charge)
      das_right_.das_charged = false;
    if (active_direction_ == Input::Right) {
      active_direction_ =
          das_left_.held ? std::optional(Input::Left) : std::nullopt;
    }
    break;
  case Input::SoftDrop:
    soft_drop_held_ = false;
    break;
  default:
    break;
  }
}

void HumanPlayer::tick(TimePoint now) {
  // Only process DAS/ARR for the active direction (last-pressed wins).
  // std::cout << static_cast<int>(active_direction_.value_or(Input::Left)) <<
  // std::endl;
  if (active_direction_ == Input::Left)
    update_das(das_left_, Input::Left, now);
  else if (active_direction_ == Input::Right)
    update_das(das_right_, Input::Right, now);

  update_soft_drop(now);
}

void HumanPlayer::update_das(DASState &das, Input input, TimePoint now) {
  if (!das.held)
    return;

  if (!das.das_charged) {
    if (now >= das.press_time + settings_.das) {
      das.das_charged = true;
      das.last_repeat = das.press_time + settings_.das;
      buffer_.push_back(input);
    }
    return;
  }

  // DAS is charged — emit ARR repeats.
  if (settings_.arr == std::chrono::milliseconds{0}) {
    // ARR=0: emit enough to cross the board every tick.
    // Collision handling in Game stops the piece at the wall.
    for (int i = 0; i < Board::kWidth; ++i)
      buffer_.push_back(input);
  } else {
    while (now >= das.last_repeat + settings_.arr) {
      das.last_repeat += settings_.arr;
      buffer_.push_back(input);
    }
  }
}

void HumanPlayer::update_soft_drop(TimePoint now) {
  if (!soft_drop_held_)
    return;

  if (settings_.soft_drop_interval == std::chrono::milliseconds{0}) {
    // Sonic: emit enough to drop through the full board every tick.
    for (int i = 0; i < Board::kTotalHeight; ++i)
      buffer_.push_back(Input::SoftDrop);
  } else {
    while (now >= last_soft_drop_ + settings_.soft_drop_interval) {
      last_soft_drop_ += settings_.soft_drop_interval;
      buffer_.push_back(Input::SoftDrop);
    }
  }
}

std::optional<TimePoint> HumanPlayer::next_wakeup() const {
  std::optional<TimePoint> earliest;

  auto consider = [&](TimePoint t) {
    if (!earliest || t < *earliest)
      earliest = t;
  };

  auto consider_das = [&](const DASState &das) {
    if (!das.held)
      return;
    if (!das.das_charged) {
      consider(das.press_time + settings_.das);
    } else if (settings_.arr > std::chrono::milliseconds{0}) {
      consider(das.last_repeat + settings_.arr);
    } else {
      // ARR=0: need immediate wakeup every tick while held.
      consider(das.last_repeat); // already in the past → wakes immediately
    }
  };

  if (active_direction_ == Input::Left)
    consider_das(das_left_);
  if (active_direction_ == Input::Right)
    consider_das(das_right_);

  if (soft_drop_held_) {
    if (settings_.soft_drop_interval > std::chrono::milliseconds{0}) {
      consider(last_soft_drop_ + settings_.soft_drop_interval);
    } else {
      consider(last_soft_drop_); // immediate wakeup
    }
  }

  return earliest;
}

std::optional<Input> HumanPlayer::key_to_input(sf::Keyboard::Key key) const {
  auto it = key_map_.find(key);
  if (it != key_map_.end())
    return it->second;
  return std::nullopt;
}
