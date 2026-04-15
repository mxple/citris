#pragma once

#include "engine/game_state.h"
#include "engine/piece.h"
#include "sdl_types.h"
#include "stats.h"
#include "vec2.h"
#include <array>
#include <optional>
#include <string>
#include <vector>

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

// A single planned piece placement for the overlay.
struct PlannedPlacement {
  std::array<Vec2, 4> cells;   // absolute board coordinates (adjusted for prior clears)
  PieceType type;
  float alpha = 0.20f;
  int step_number = 0;
};

// Target checkpoint silhouette rendered as a dim board overlay.
struct CheckpointOverlay {
  std::vector<uint16_t> rows;  // bitmask per row (10 bits)
  float alpha = 0.05f;
};

struct ViewModel {
  GameState state;
  Stats::Snapshot stats;
  std::optional<HudData> hud;
  std::vector<PlannedPlacement> plan_overlay;          // empty when no plan active
  std::optional<CheckpointOverlay> checkpoint_overlay;  // target silhouette
};
