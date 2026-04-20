#pragma once

#include "engine/game_state.h"
#include "engine/piece.h"
#include "match.h"
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

  // Versus: queued garbage waiting to materialize on this board. Zero in
  // single-player (no attack source exists to produce it).
  int pending_garbage_lines = 0;
};

// A single planned piece placement for the overlay.
struct PlannedPlacement {
  std::array<Vec2, 4> cells;   // absolute board coordinates (adjusted for prior clears)
  PieceType type;
  int step_number = 0;
};

struct ViewModel {
  GameState state;
  Stats::Snapshot stats;
  std::optional<HudData> hud;
  std::vector<PlannedPlacement> plan_overlay;  // empty when no plan active
};

// Two-player view model for versus mode. Each side has the same shape as a
// single-player ViewModel; `match` carries winner tracking populated by
// the GameManager when either side tops out.
struct VersusViewModel {
  ViewModel left;
  ViewModel right;
  MatchState match;
};
