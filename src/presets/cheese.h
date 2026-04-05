#pragma once

#include "engine/board.h"
#include "game_mode.h"
#include <cstdio>
#include <random>

class CheeseMode : public GameMode {
public:
  explicit CheeseMode(int garbage_rows = 9) : garbage_rows_(garbage_rows) {}

  std::string title() const override { return "Cheese Race"; }

  void on_start(TimePoint now) override { start_time_ = now; }

  void setup_board(Board &board) override {
    std::uniform_int_distribution<int> dist(0, Board::kWidth - 1);
    for (int i = 0; i < garbage_rows_; ++i)
      board.add_garbage(1, dist(rng_));
  }

  void on_piece_locked(const eng::PieceLocked &ev, const GameState &,
                       CommandBuffer &cmds) override {
    if (ev.lines_cleared > 0 && ev.perfect_clear) {
      cmds.push(cmd::SetGameOver{true});
      return;
    }
    if (ev.lines_cleared == 0 && ev.prev_combo > 0) {
      std::uniform_int_distribution<int> dist(0, Board::kWidth - 1);
      cmds.push(cmd::AddGarbage{1, dist(rng_), true});
    }
  }

  void fill_hud(HudData &hud, const GameState &state,
                TimePoint now) override {
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
  int garbage_rows_;
  TimePoint start_time_;
  std::mt19937 rng_{std::random_device{}()};
};
