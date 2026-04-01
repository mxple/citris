#include "human_player.h"
#include "board.h"

HumanPlayer::HumanPlayer(const Settings &settings, Stats &stats,
                         TimerManager &timers)
    : settings_(settings), stats_(stats), timers_(timers) {
  key_map_[settings.move_left] = Input::Left;
  key_map_[settings.move_right] = Input::Right;
  key_map_[settings.rotate_cw] = Input::RotateCW;
  key_map_[settings.rotate_ccw] = Input::RotateCCW;
  key_map_[settings.rotate_180] = Input::Rotate180;
  key_map_[settings.hard_drop] = Input::HardDrop;
  key_map_[settings.soft_drop] = Input::SoftDrop;
  key_map_[settings.hold] = Input::Hold;
}

bool HumanPlayer::process(const Event &ev, TimePoint now,
                          std::vector<Event> &pending) {
  return std::visit(
      [&](auto &&e) -> bool {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, KeyPressed>) {
          return handle_key_pressed(e, now, pending);
        } else if constexpr (std::is_same_v<T, KeyReleased>) {
          return handle_key_released(e, now, pending);
        } else if constexpr (std::is_same_v<T, DASCharged>) {
          handle_das_charged(e, now, pending);
          return true;
        } else if constexpr (std::is_same_v<T, ARRTick>) {
          handle_arr_tick(e, now, pending);
          return true;
        } else if constexpr (std::is_same_v<T, SoftDropTick>) {
          handle_soft_drop_tick(now, pending);
          return true;
        } else {
          return false;
        }
      },
      ev);
}

bool HumanPlayer::handle_key_pressed(const KeyPressed &e, TimePoint now,
                                     std::vector<Event> &pending) {
  auto input = key_to_input(e.key);
  if (!input)
    return false;

  stats_.add_input();

  switch (*input) {
  case Input::Left:
  case Input::Right: {
    int idx = dir_index(*input);
    if (!held_[idx]) {
      held_[idx] = true;
      pending.push_back(MoveInput{*input});
      if (arr0_direction_ && active_direction_ != *input)
        cancel_arr0(pending);
      das_charged_[idx] = false;
      timers_.schedule(das_timer(*input), now + settings_.das,
                       DASCharged{*input});
    }
    active_direction_ = *input;
    break;
  }
  case Input::SoftDrop: {
    if (!soft_drop_held_) {
      soft_drop_held_ = true;
      pending.push_back(MoveInput{Input::SoftDrop});
      if (settings_.soft_drop_interval == std::chrono::milliseconds{0}) {
        for (int i = 0; i < Board::kTotalHeight; ++i)
          pending.push_back(MoveInput{Input::SoftDrop});
        pending.push_back(SoftDropActive{});
        sonic_drop_active_ = true;
      } else {
        timers_.schedule(TimerKind::SoftDrop,
                         now + settings_.soft_drop_interval, SoftDropTick{});
      }
    }
    break;
  }
  default:
    pending.push_back(MoveInput{*input});
    break;
  }
  return true;
}

bool HumanPlayer::handle_key_released(const KeyReleased &e, TimePoint now,
                                      std::vector<Event> &pending) {
  auto input = key_to_input(e.key);
  if (!input)
    return false;

  switch (*input) {
  case Input::Left:
  case Input::Right: {
    int idx = dir_index(*input);
    held_[idx] = false;
    timers_.cancel(das_timer(*input));
    timers_.cancel(arr_timer(*input));
    das_charged_[idx] = false;
    cancel_arr0(pending);

    if (active_direction_ == *input) {
      Input other = opposite(*input);
      int other_idx = dir_index(other);
      if (held_[other_idx]) {
        active_direction_ = other;
        if (settings_.das_preserve_charge && das_charged_[other_idx]) {
          start_arr_or_burst(other, now, pending);
        } else if (!timers_.active(das_timer(other))) {
          das_charged_[other_idx] = false;
          timers_.schedule(das_timer(other), now + settings_.das,
                           DASCharged{other});
        }
        // else: DAS timer for other direction still running, will fire
        // naturally
      } else {
        active_direction_ = std::nullopt;
      }
    }
    break;
  }
  case Input::SoftDrop:
    soft_drop_held_ = false;
    timers_.cancel(TimerKind::SoftDrop);
    cancel_sonic_drop(pending);
    break;
  default:
    break;
  }
  return true;
}

void HumanPlayer::handle_das_charged(const DASCharged &e, TimePoint now,
                                     std::vector<Event> &pending) {
  int idx = dir_index(e.direction);
  // Guard against stale timer events — key may have been released after
  // collect_expired moved this event into pending but before dispatch.
  if (!held_[idx])
    return;

  das_charged_[idx] = true;

  if (active_direction_ != e.direction)
    return;

  start_arr_or_burst(e.direction, now, pending);
}

void HumanPlayer::handle_arr_tick(const ARRTick &e, TimePoint now,
                                  std::vector<Event> &pending) {
  if (active_direction_ != e.direction)
    return;

  pending.push_back(MoveInput{e.direction});
  timers_.schedule(arr_timer(e.direction), now + settings_.arr,
                   ARRTick{e.direction});
}

void HumanPlayer::handle_soft_drop_tick(TimePoint now,
                                        std::vector<Event> &pending) {
  if (!soft_drop_held_)
    return;

  pending.push_back(MoveInput{Input::SoftDrop});
  if (settings_.soft_drop_interval > std::chrono::milliseconds{0}) {
    timers_.schedule(TimerKind::SoftDrop, now + settings_.soft_drop_interval,
                     SoftDropTick{});
  } else {
    for (int i = 0; i < Board::kTotalHeight; ++i)
      pending.push_back(MoveInput{Input::SoftDrop});
  }
}

void HumanPlayer::start_arr_or_burst(Input dir, TimePoint now,
                                     std::vector<Event> &pending) {
  if (settings_.arr == std::chrono::milliseconds{0}) {
    for (int i = 0; i < Board::kWidth; ++i)
      pending.push_back(MoveInput{dir});
    pending.push_back(ARRActive{dir});
    arr0_direction_ = dir;
  } else {
    pending.push_back(MoveInput{dir});
    timers_.schedule(arr_timer(dir), now + settings_.arr, ARRTick{dir});
  }
}

void HumanPlayer::cancel_arr0(std::vector<Event> &pending) {
  if (arr0_direction_) {
    pending.push_back(ARRInactive{});
    arr0_direction_.reset();
  }
}

void HumanPlayer::cancel_sonic_drop(std::vector<Event> &pending) {
  if (sonic_drop_active_) {
    pending.push_back(SoftDropInactive{});
    sonic_drop_active_ = false;
  }
}

std::optional<Input> HumanPlayer::key_to_input(sf::Keyboard::Key key) const {
  auto it = key_map_.find(key);
  if (it != key_map_.end())
    return it->second;
  return std::nullopt;
}
