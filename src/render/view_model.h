#pragma once

#include "engine/game_state.h"
#include "sdl_types.h"
#include "stats.h"
#include <optional>
#include <string>

struct HudData {
  // Bottom-center text (e.g. sprint remaining count, blitz countdown)
  std::string center_text;
  Color center_color{200, 200, 200};

  // Game-over overlay
  std::string game_over_label;
  Color game_over_label_color{255, 255, 100};
  std::string game_over_detail;
  Color game_over_detail_color{100, 255, 100};
  unsigned game_over_detail_size = 36;
};

struct ViewModel {
  GameState state;
  Stats::Snapshot stats;
  std::optional<HudData> hud;
};
