#include "player_controller.h"

PlayerController::PlayerController(const Settings &settings)
    : settings_(settings) {
  key_map_[settings.move_left] = Input::Left;
  key_map_[settings.move_right] = Input::Right;
  key_map_[settings.rotate_cw] = Input::RotateCW;
  key_map_[settings.rotate_ccw] = Input::RotateCCW;
  key_map_[settings.rotate_180] = Input::Rotate180;
  key_map_[settings.hard_drop] = Input::HardDrop;
  key_map_[settings.soft_drop] = Input::SoftDrop;
  key_map_[settings.hold] = Input::Hold;
}

void PlayerController::handle_event(const InputEvent &ev, TimePoint now,
                                    const GameState &, CommandBuffer &cmds) {
  std::visit(
      [&](auto &&e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, KeyDown>) {
          handle_key_down(e.key, now, cmds);
        } else if constexpr (std::is_same_v<T, KeyUp>) {
          handle_key_up(e.key, now, cmds);
        }
      },
      ev);
}

void PlayerController::tick(TimePoint now, const GameState &,
                            CommandBuffer &cmds) {
  // DAS timers
  for (int i = 0; i < 2; ++i) {
    if (das_deadline_[i] && now >= *das_deadline_[i]) {
      TimePoint expired = *das_deadline_[i];
      das_deadline_[i].reset();
      Input dir = (i == 0) ? Input::Left : Input::Right;
      if (!held_[i])
        continue;
      das_charged_[i] = true;
      if (active_direction_ != dir)
        continue;
      start_arr_or_burst(dir, expired, cmds);
    }
  }

  // ARR timers
  for (int i = 0; i < 2; ++i) {
    if (arr_deadline_[i] && now >= *arr_deadline_[i]) {
      TimePoint expired = *arr_deadline_[i];
      arr_deadline_[i].reset();
      Input dir = (i == 0) ? Input::Left : Input::Right;
      if (active_direction_ != dir)
        continue;
      cmds.push(cmd::MovePiece{dir});
      arr_deadline_[i] = expired + settings_.arr;
    }
  }

  // Soft drop timer
  if (soft_drop_deadline_ && now >= *soft_drop_deadline_) {
    TimePoint expired = *soft_drop_deadline_;
    soft_drop_deadline_.reset();
    if (!soft_drop_held_)
      return;
    cmds.push(cmd::MovePiece{Input::SoftDrop});
    if (settings_.soft_drop_interval > std::chrono::milliseconds{0}) {
      soft_drop_deadline_ = expired + settings_.soft_drop_interval;
    } else {
      for (int i = 0; i < Board::kTotalHeight; ++i)
        cmds.push(cmd::MovePiece{Input::SoftDrop});
    }
  }
}

std::optional<TimePoint> PlayerController::next_deadline() const {
  std::optional<TimePoint> earliest;
  auto consider = [&](const std::optional<TimePoint> &tp) {
    if (tp && (!earliest || *tp < *earliest))
      earliest = tp;
  };
  consider(das_deadline_[0]);
  consider(das_deadline_[1]);
  consider(arr_deadline_[0]);
  consider(arr_deadline_[1]);
  consider(soft_drop_deadline_);
  return earliest;
}

void PlayerController::handle_key_down(KeyCode key, TimePoint now,
                                       CommandBuffer &cmds) {
  auto input = key_to_input(key);
  if (!input)
    return;

  cmds.push(cmd::Passthrough{note::InputRegistered{}});

  switch (*input) {
  case Input::Left:
  case Input::Right: {
    int idx = dir_index(*input);
    if (!held_[idx]) {
      held_[idx] = true;
      cmds.push(cmd::MovePiece{*input});
      if (arr0_direction_ && active_direction_ != *input)
        cancel_arr0(cmds);
      das_charged_[idx] = false;
      das_deadline_[idx] = now + settings_.das;
      arr_deadline_[idx].reset();
    }
    active_direction_ = *input;
    break;
  }
  case Input::SoftDrop: {
    if (!soft_drop_held_) {
      soft_drop_held_ = true;
      cmds.push(cmd::MovePiece{Input::SoftDrop});
      if (settings_.soft_drop_interval == std::chrono::milliseconds{0}) {
        for (int i = 0; i < Board::kTotalHeight; ++i)
          cmds.push(cmd::MovePiece{Input::SoftDrop});
        cmds.push(cmd::SetSoftDropActive{true});
        sonic_drop_active_ = true;
      } else {
        soft_drop_deadline_ = now + settings_.soft_drop_interval;
      }
    }
    break;
  }
  case Input::HardDrop:
    if (now < hard_drop_blocked_until_)
      break;
    cmds.push(cmd::MovePiece{*input});
    break;
  default:
    cmds.push(cmd::MovePiece{*input});
    break;
  }
}

void PlayerController::handle_key_up(KeyCode key, TimePoint now,
                                     CommandBuffer &cmds) {
  auto input = key_to_input(key);
  if (!input)
    return;

  switch (*input) {
  case Input::Left:
  case Input::Right: {
    int idx = dir_index(*input);
    held_[idx] = false;
    das_deadline_[idx].reset();
    arr_deadline_[idx].reset();
    das_charged_[idx] = false;
    cancel_arr0(cmds);

    if (active_direction_ == *input) {
      Input other = opposite(*input);
      int other_idx = dir_index(other);
      if (held_[other_idx]) {
        active_direction_ = other;
        if (settings_.das_preserve_charge && das_charged_[other_idx]) {
          start_arr_or_burst(other, now, cmds);
        } else if (!das_deadline_[other_idx]) {
          das_charged_[other_idx] = false;
          das_deadline_[other_idx] = now + settings_.das;
        }
      } else {
        active_direction_ = std::nullopt;
      }
    }
    break;
  }
  case Input::SoftDrop:
    soft_drop_held_ = false;
    soft_drop_deadline_.reset();
    cancel_sonic_drop(cmds);
    break;
  default:
    break;
  }
}

void PlayerController::start_arr_or_burst(Input dir, TimePoint now,
                                          CommandBuffer &cmds) {
  int idx = dir_index(dir);
  if (settings_.arr == std::chrono::milliseconds{0}) {
    for (int i = 0; i < Board::kWidth; ++i)
      cmds.push(cmd::MovePiece{dir});
    cmds.push(cmd::SetARRDirection{dir});
    arr0_direction_ = dir;
  } else {
    cmds.push(cmd::MovePiece{dir});
    arr_deadline_[idx] = now + settings_.arr;
  }
}

void PlayerController::cancel_arr0(CommandBuffer &cmds) {
  if (arr0_direction_) {
    cmds.push(cmd::SetARRDirection{std::nullopt});
    arr0_direction_.reset();
  }
}

void PlayerController::cancel_sonic_drop(CommandBuffer &cmds) {
  if (sonic_drop_active_) {
    cmds.push(cmd::SetSoftDropActive{false});
    sonic_drop_active_ = false;
  }
}

void PlayerController::reset_input_state() {
  held_[0] = held_[1] = false;
  das_charged_[0] = das_charged_[1] = false;
  active_direction_.reset();
  das_deadline_[0].reset();
  das_deadline_[1].reset();
  arr_deadline_[0].reset();
  arr_deadline_[1].reset();
  soft_drop_held_ = false;
  soft_drop_deadline_.reset();
  sonic_drop_active_ = false;
  arr0_direction_.reset();
  hard_drop_blocked_until_ = {};
}

void PlayerController::notify(const EngineEvent &ev, TimePoint now) {
  if (auto *lde = std::get_if<eng::LockDelayExpired>(&ev)) {
    hard_drop_blocked_until_ = now + lde->hard_drop_delay;
  }
}

std::optional<Input>
PlayerController::key_to_input(KeyCode key) const {
  auto it = key_map_.find(key);
  if (it != key_map_.end())
    return it->second;
  return std::nullopt;
}
