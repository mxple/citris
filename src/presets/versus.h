#pragma once

#include "freeplay.h"
#include "settings.h"
#include "tbp/bot.h"
#include "tbp/external_bot.h"
#include "tbp/internal_bot.h"

#include <memory>
#include <string>
#include <utility>

// Versus: side-by-side 1v1 where each player is independently a human, the
// in-process Citris AI (InternalTbpBot), or an external subprocess bot
// (ExternalTbpBot). P1 defaults to Human, P2 to Citris AI.
//
// Player-specific knobs (beam_width, max_depth, think_time_ms, external bot
// path) live on PlayerConfig so the menu can round-trip them through a
// single struct instead of threading many setters.
class VersusMode : public FreeplayMode {
public:
  enum class Kind { Human, CitrisAi, ExternalBot };

  struct PlayerConfig {
    Kind kind = Kind::Human;
    std::string external_path;
    // CitrisAi search params (ignored for Human / ExternalBot).
    int beam_width = 800;
    int max_depth = 14;
    // Minimum ms between placements — 0 = no cap. Applied by TbpController
    // when configured > 0; useful to keep AI-vs-AI games watchable in the
    // GUI rather than terminating in milliseconds.
    int think_time_ms = 0;
  };

  using FreeplayMode::FreeplayMode;

  std::string title() const override { return "Versus"; }

  std::unique_ptr<GameMode>
  opponent_mode(const Settings &settings) const override {
    return std::make_unique<FreeplayMode>(settings);
  }

  // Build the TbpBot that will drive the given player. nullptr means the
  // player is Human (the GameManager will wire a PlayerController instead).
  std::unique_ptr<tbp::TbpBot> make_player_bot(int player_idx) const override {
    const PlayerConfig &cfg = player_idx == 0 ? p1_ : p2_;
    switch (cfg.kind) {
    case Kind::Human:
      return nullptr;
    case Kind::CitrisAi: {
      tbp::InternalBotConfig bc;
      bc.beam_width = cfg.beam_width;
      bc.max_depth = cfg.max_depth;
      return std::make_unique<tbp::InternalTbpBot>(std::move(bc));
    }
    case Kind::ExternalBot: {
      tbp::ExternalBotConfig bc;
      bc.exe_path = cfg.external_path;
      return std::make_unique<tbp::ExternalTbpBot>(std::move(bc));
    }
    }
    return nullptr;
  }

  // Back-compat for callers using the simpler single-opponent hook — maps
  // to player 2's configuration.
  std::unique_ptr<tbp::TbpBot> make_opponent_bot() const override {
    return make_player_bot(1);
  }

  // Think-time cap (ms) for a given player's TbpController. 0 = no cap.
  int think_time_ms(int player_idx) const override {
    return (player_idx == 0 ? p1_ : p2_).think_time_ms;
  }

  // Menu configuration setters / getters.
  VersusMode &set_player(int idx, PlayerConfig cfg) {
    (idx == 0 ? p1_ : p2_) = std::move(cfg);
    return *this;
  }
  const PlayerConfig &player(int idx) const {
    return idx == 0 ? p1_ : p2_;
  }

private:
  PlayerConfig p1_{.kind = Kind::Human};
  PlayerConfig p2_{.kind = Kind::CitrisAi};
};
