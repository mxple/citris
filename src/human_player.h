#pragma once

#include "player.h"
#include "settings.h"
#include "stats.h"
#include "timer_manager.h"
#include <optional>
#include <unordered_map>

class HumanPlayer : public IPlayer {
public:
  HumanPlayer(const Settings &settings, Stats &stats, TimerManager &timers);

  bool process(const Event &ev, TimePoint now,
               std::vector<Event> &pending) override;

private:
  std::optional<Input> key_to_input(sf::Keyboard::Key key) const;

  bool handle_key_pressed(const KeyPressed &e, TimePoint now,
                          std::vector<Event> &pending);
  bool handle_key_released(const KeyReleased &e, TimePoint now,
                           std::vector<Event> &pending);
  void handle_das_charged(const DASCharged &e, TimePoint now,
                          std::vector<Event> &pending);
  void handle_arr_tick(const ARRTick &e, TimePoint now,
                       std::vector<Event> &pending);
  void handle_soft_drop_tick(TimePoint now, std::vector<Event> &pending);

  void start_arr_or_burst(Input dir, TimePoint now,
                          std::vector<Event> &pending);
  void cancel_arr0(std::vector<Event> &pending);
  void cancel_sonic_drop(std::vector<Event> &pending);

  static int dir_index(Input dir) { return dir == Input::Left ? 0 : 1; }
  static Input opposite(Input dir) {
    return dir == Input::Left ? Input::Right : Input::Left;
  }
  static TimerKind das_timer(Input dir) {
    return dir == Input::Left ? TimerKind::DAS_Left : TimerKind::DAS_Right;
  }
  static TimerKind arr_timer(Input dir) {
    return dir == Input::Left ? TimerKind::ARR_Left : TimerKind::ARR_Right;
  }

  const Settings &settings_;
  Stats &stats_;
  TimerManager &timers_;

  std::unordered_map<sf::Keyboard::Key, Input> key_map_;

  bool held_[2] = {}; // [0]=Left, [1]=Right
  bool das_charged_[2] = {};
  std::optional<Input> active_direction_;

  bool soft_drop_held_ = false;
  bool sonic_drop_active_ = false;
  std::optional<Input> arr0_direction_;
};
