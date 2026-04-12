#pragma once

#include "engine/board.h"
#include "game_mode.h"
#include <algorithm>
#include <cstdio>
#include <random>

class CheeseMode : public GameMode {
public:
  static constexpr int kTotalCheese = 100;
  static constexpr int kBoardFill = 10;

  CheeseMode() = default;

  std::string title() const override { return "Cheese Race"; }

  // Tuning — competitive defaults matching jstris cheese race
  std::chrono::milliseconds gravity_interval() const override {
    return std::chrono::milliseconds{0}; // no gravity
  }
  std::chrono::milliseconds lock_delay() const override {
    return std::chrono::milliseconds{500};
  }
  int max_lock_resets() const override { return 15; }

  bool undo_allowed() const override { return false; }


  void setup_board(Board &board) override {
    std::uniform_int_distribution<int> dist(0, Board::kWidth - 1);
    for (int i = 0; i < kBoardFill; ++i)
      board.add_garbage(1, dist(rng_));
    cheese_reserve_ = kTotalCheese - kBoardFill;
  }

  void on_piece_locked(const eng::PieceLocked &ev, const GameState &state,
                       CommandBuffer &cmds) override {
    int garbage_on_board = count_garbage_rows(state.board);

    if (cheese_reserve_ == 0 && garbage_on_board == 0) {
      cmds.push(cmd::SetGameOver{true});
      return;
    }

    if (ev.lines_cleared == 0 && ev.prev_combo > 0) {
      if (cheese_reserve_ > 0) {
        int to_add =
            std::min(kBoardFill - garbage_on_board, cheese_reserve_);
        std::uniform_int_distribution<int> dist(0, Board::kWidth - 1);
        for (int i = 0; i < to_add; ++i)
          cmds.push(cmd::AddGarbage{1, dist(rng_), true});
        cheese_reserve_ -= to_add;
      }
    }
  }

  void fill_hud(HudData &hud, const GameState &state,
                TimePoint now) override {
    int garbage_on_board = count_garbage_rows(state.board);
    int cleared = kTotalCheese - cheese_reserve_ - garbage_on_board;

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d / %d", cleared, kTotalCheese);
    hud.center_text = buf;

    if (state.game_over && !end_time_)
      end_time_ = now;

    if (state.game_over && state.won) {
      hud.game_over_label = "CLEAR!";
      hud.game_over_label_color = Color(255, 255, 100);

      float elapsed_s =
          std::chrono::duration<float>(end_time_.value_or(now) - start_time_)
              .count();
      int total_ms = static_cast<int>(elapsed_s * 1000);
      int mins = total_ms / 60000;
      int secs = (total_ms % 60000) / 1000;
      int ms = total_ms % 1000;
      std::snprintf(buf, sizeof(buf), "%d:%02d.%03d", mins, secs, ms);
      hud.game_over_detail = buf;
      hud.game_over_detail_color = Color(100, 255, 100);
      hud.game_over_detail_size = 36;
    }
  }

private:
  static int count_garbage_rows(const Board &board) {
    int count = 0;
    for (int row = 0; row < Board::kTotalHeight; ++row)
      for (int col = 0; col < Board::kWidth; ++col)
        if (board.cell_color(col, row) == CellColor::Garbage) {
          ++count;
          break;
        }
    return count;
  }

  int cheese_reserve_ = kTotalCheese - kBoardFill;
  std::mt19937 rng_{std::random_device{}()};
};
