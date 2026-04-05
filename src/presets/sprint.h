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

  bool undo_allowed() const override { return false; }

  void on_start(TimePoint now) override { start_time_ = now; }

  void on_piece_locked(const eng::PieceLocked &, const GameState &state,
                       CommandBuffer &cmds) override {
    if (state.lines_cleared >= target_lines_)
      cmds.push(cmd::SetGameOver{true});
  }

  void fill_hud(HudData &hud, const GameState &state,
                TimePoint now) override {
    int remaining = std::max(0, target_lines_ - state.lines_cleared);
    hud.center_text = std::to_string(remaining);
    hud.center_color = sf::Color(200, 200, 200);

    if (state.game_over && state.won) {
      hud.game_over_label = "CLEAR!";
      hud.game_over_label_color = sf::Color(255, 255, 100);

      float elapsed_s =
          std::chrono::duration<float>(now - start_time_).count();
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
};
