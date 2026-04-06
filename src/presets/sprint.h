#pragma once

#include "game_mode.h"
#include <algorithm>
#include <cstdio>

class SprintMode : public GameMode {
public:
  explicit SprintMode(int target_lines = 40) : target_lines_(target_lines) {}

  std::string title() const override {
    return "Sprint " + std::to_string(target_lines_) + "L";
  }

  // Tuning — fixed competitive defaults, not from settings.ini
  std::chrono::milliseconds gravity_interval() const override {
    return std::chrono::milliseconds{1000};
  }
  std::chrono::milliseconds lock_delay() const override {
    return std::chrono::milliseconds{500};
  }
  int max_lock_resets() const override { return 15; }

  bool undo_allowed() const override { return false; }

  void on_start(TimePoint now) override { start_time_ = now; }

  void on_piece_locked(const eng::PieceLocked &, const GameState &state,
                       CommandBuffer &cmds) override {
    if (state.lines_cleared >= target_lines_)
      cmds.push(cmd::SetGameOver{true});
  }

  void fill_hud(HudData &hud, const GameState &state,
                TimePoint now) override {
    if (state.game_over && !end_time_)
      end_time_ = now;
    float elapsed_s =
        std::chrono::duration<float>(end_time_.value_or(now) - start_time_)
            .count();

    int remaining = std::max(0, target_lines_ - state.lines_cleared);
    hud.center_text = std::to_string(remaining);
    hud.center_color = sf::Color(200, 200, 200);

    if (state.game_over && state.won) {
      hud.game_over_label = "CLEAR!";
      hud.game_over_label_color = sf::Color(255, 255, 100);

      int total_ms = static_cast<int>(elapsed_s * 1000);
      int mins = total_ms / 60000;
      int secs = (total_ms % 60000) / 1000;
      int ms = total_ms % 1000;
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%d:%02d.%03d", mins, secs, ms);
      hud.game_over_detail = buf;
      hud.game_over_detail_color = sf::Color(100, 255, 100);
      hud.game_over_detail_size = 36;
    }
  }

private:
  int target_lines_;
  TimePoint start_time_;
  std::optional<TimePoint> end_time_;
};
