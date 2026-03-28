#pragma once

#include "player.h"
#include "settings.h"
#include <deque>
#include <unordered_map>

class HumanPlayer : public IPlayer {
public:
  explicit HumanPlayer(const Settings &settings);

  std::optional<Input> poll(const GameState &state) override;
  void on_key_pressed(sf::Keyboard::Key key) override;
  void on_key_released(sf::Keyboard::Key key) override;
  void tick(TimePoint now) override;
  std::optional<TimePoint> next_wakeup() const override;

private:
  std::optional<Input> key_to_input(sf::Keyboard::Key key) const;

  struct DASState {
    bool held = false;
    TimePoint press_time{};
    TimePoint last_repeat{};
    bool das_charged = false;
  };

  void update_das(DASState &das, Input input, TimePoint now);
  void update_soft_drop(TimePoint now);

  const Settings &settings_;
  std::deque<Input> buffer_;

  DASState das_left_;
  DASState das_right_;

  // Last pressed wins
  std::optional<Input> active_direction_;

  bool soft_drop_held_ = false;
  TimePoint last_soft_drop_{};

  std::unordered_map<sf::Keyboard::Key, Input> key_map_;
};
