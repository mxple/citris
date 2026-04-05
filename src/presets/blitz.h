#pragma once

#include "game_mode.h"
#include <chrono>
#include <cstdio>

class BlitzMode : public GameMode {
public:
  explicit BlitzMode(std::chrono::seconds limit = std::chrono::seconds{120})
      : limit_(limit) {}

  std::string title() const override {
    return "Blitz " + std::to_string(limit_.count() / 60) + "min";
  }

  bool undo_allowed() const override { return false; }

  void on_start(TimePoint now) override { start_time_ = now; }

  void on_tick(TimePoint now, const GameState &,
               CommandBuffer &cmds) override {
    float elapsed = std::chrono::duration<float>(now - start_time_).count();
    if (elapsed >= static_cast<float>(limit_.count()))
      cmds.push(cmd::SetGameOver{true});
  }

  void fill_hud(HudData &hud, const GameState &state,
                TimePoint now) override {
    float elapsed = std::chrono::duration<float>(now - start_time_).count();
    float remaining = static_cast<float>(limit_.count()) - elapsed;
    if (remaining < 0.f)
      remaining = 0.f;

    int secs = static_cast<int>(remaining);
    int mins = secs / 60;
    secs %= 60;
    int tenths =
        static_cast<int>((remaining - static_cast<int>(remaining)) * 10);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d:%02d.%d", mins, secs, tenths);

    hud.center_text = buf;
    hud.center_color = remaining < 30.f ? sf::Color(255, 100, 100)
                                        : sf::Color(200, 200, 200);

    if (state.game_over) {
      hud.game_over_label = "TIME!";
      hud.game_over_label_color = sf::Color(255, 255, 100);
      hud.game_over_detail = std::to_string(state.total_attack) + " ATK";
      hud.game_over_detail_color = sf::Color(100, 200, 255);
      hud.game_over_detail_size = 36;
    }
  }

private:
  std::chrono::seconds limit_;
  TimePoint start_time_;
};
