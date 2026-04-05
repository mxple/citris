#pragma once

#include "controller.h"
#include "settings.h"
#include <optional>
#include <unordered_map>

class PlayerController : public IController {
public:
  explicit PlayerController(const Settings &settings);

  void update(const InputEvent &ev, TimePoint now, const GameState &state,
              CommandBuffer &cmds) override;
  void check_timers(TimePoint now, CommandBuffer &cmds) override;
  std::optional<TimePoint> next_deadline() const override;
  void reset_input_state() override;
  void notify(const EngineEvent &ev, TimePoint now) override;

private:
  std::optional<Input> key_to_input(sf::Keyboard::Key key) const;

  void handle_key_down(sf::Keyboard::Key key, TimePoint now,
                       CommandBuffer &cmds);
  void handle_key_up(sf::Keyboard::Key key, TimePoint now,
                     CommandBuffer &cmds);

  void start_arr_or_burst(Input dir, TimePoint now, CommandBuffer &cmds);
  void cancel_arr0(CommandBuffer &cmds);
  void cancel_sonic_drop(CommandBuffer &cmds);

  static int dir_index(Input dir) { return dir == Input::Left ? 0 : 1; }
  static Input opposite(Input dir) {
    return dir == Input::Left ? Input::Right : Input::Left;
  }

  const Settings &settings_;
  std::unordered_map<sf::Keyboard::Key, Input> key_map_;

  // DAS/ARR state
  bool held_[2] = {};
  bool das_charged_[2] = {};
  std::optional<Input> active_direction_;
  std::optional<TimePoint> das_deadline_[2];
  std::optional<TimePoint> arr_deadline_[2];

  // Soft drop state
  bool soft_drop_held_ = false;
  bool sonic_drop_active_ = false;
  std::optional<TimePoint> soft_drop_deadline_;

  // ARR=0 state
  std::optional<Input> arr0_direction_;

  // Hard drop suppression after lock delay expiry
  TimePoint hard_drop_blocked_until_{};
};
