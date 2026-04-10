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
  std::optional<GameInput> key_to_input(KeyCode key) const;

  void handle_key_down(KeyCode key, TimePoint now, CommandBuffer &cmds);
  void handle_key_up(KeyCode key, TimePoint now, CommandBuffer &cmds);

  void start_arr_or_burst(GameInput dir, TimePoint now, CommandBuffer &cmds);
  void cancel_arr0(CommandBuffer &cmds);
  void cancel_sonic_drop(CommandBuffer &cmds);

  static int dir_index(GameInput dir) { return dir == GameInput::Left ? 0 : 1; }
  static GameInput opposite(GameInput dir) {
    return dir == GameInput::Left ? GameInput::Right : GameInput::Left;
  }

  const Settings &settings_;
  std::unordered_map<KeyCode, GameInput> key_map_;

  // DAS/ARR state
  bool held_[2] = {};
  bool das_charged_[2] = {};
  std::optional<GameInput> active_direction_;
  std::optional<TimePoint> das_deadline_[2];
  std::optional<TimePoint> arr_deadline_[2];

  // Soft drop state
  bool soft_drop_held_ = false;
  bool sonic_drop_active_ = false;
  std::optional<TimePoint> soft_drop_deadline_;

  // ARR=0 state
  std::optional<GameInput> arr0_direction_;

  // Hard drop suppression after lock delay expiry
  TimePoint hard_drop_blocked_until_{};
};
