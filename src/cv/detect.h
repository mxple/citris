#pragma once

#include "cv_image.h"
#include "engine/board.h"
#include "engine/piece.h"

#include <array>
#include <optional>
#include <utility>
#include <vector>

namespace imp {

struct Detection {
  using Grid = std::array<std::array<CellColor, Board::kWidth>,
                          Board::kVisibleHeight>;
  CvRect board{};
  std::optional<CvRect> hold;
  std::vector<CvRect> queue;
  float pitch = 0.f;
  Grid grid{};
  // Row indices can be negative (sky rows above the board: -3..-1).
  std::optional<PieceType> current_piece;
  std::vector<std::pair<int, int>> current_cells;
  std::optional<PieceType> hold_piece;
  std::vector<PieceType> queue_pieces;
};

enum class HuePreset : int { Tetrio, Jstris, PuyoPuyo, Tetris99, Count };

inline const char* hue_preset_name(HuePreset p) {
  switch (p) {
    case HuePreset::Tetrio:   return "TETR.IO";
    case HuePreset::Jstris:   return "Jstris";
    case HuePreset::PuyoPuyo: return "Puyo Puyo Tetris";
    case HuePreset::Tetris99: return "Tetris 99";
    default: return "?";
  }
}

// Per-piece hue tables (I, O, T, S, Z, J, L) in degrees 0-360.
// TODO: fill in ground-truth hues for each game.
inline constexpr std::array<float, 7> kHuePresets[int(HuePreset::Count)] = {
    // Tetrio (default)
    {198.f, 42.f, 316.f, 90.f, 348.f, 228.f, 24.f},
    // Jstris
    {198.f, 42.f, 316.f, 90.f, 348.f, 228.f, 24.f},
    // Puyo Puyo Tetris
    {198.f, 42.f, 316.f, 90.f, 348.f, 228.f, 24.f},
    // Tetris 99
    {198.f, 42.f, 316.f, 90.f, 348.f, 228.f, 24.f},
};

struct DetectParams {
  // Board finder
  float sharpen_alpha = 0.7f;
  float sharpen_sigma = 1.0f;
  float directional_ratio = 1.5f;
  int edge_percentile = 75;

  // Cell classifier
  float cell_thresh = 0.20f;
  float cell_sat_weight = 0.25f;
  float cell_fill_frac = 0.50f;
  float cell_empty_val = 0.30f;
  float cell_grey_sat = 0.25f;
  std::array<float, 7> piece_hues = kHuePresets[0];

  // Per-image calibration
  float empty_v_margin = 0.20f;
  float empty_v_min = 0.25f;
  float empty_v_max = 0.45f;

  // Hue discovery
  float piece_snap_max_deg = 40.0f;
  float hue_cluster_min_frac = 0.05f;
  int hue_cluster_min_hues = 8;

  // CC grid sampler
  float blob_v_min_thr = 0.20f;
  float blob_v_max_thr = 0.45f;
  float blob_v_otsu_backoff = 0.10f;
  float blob_s_thr = 0.30f;
  float blob_cell_overlap = 0.40f;
  float blob_min_area_frac = 0.30f;
  int blob_min_cells = 3;

  // Hold/queue shape blobs
  float piece_sat_thresh = 0.25f;
  float piece_val_thresh = 0.30f;
  float piece_pitch_agree = 0.20f;
  int piece_fill_tol = 1;
  int piece_dilate_px = 3;
  float piece_min_bbox_pitch_frac = 0.36f;
  float piece_min_cell_pitch_frac = 0.30f;

  // Queue/hold spatial constraints
  float queue_stack_overlap_eps_frac = 0.20f;
  float queue_stride_gap_factor = 1.8f;
  float queue_area_consistency_frac = 0.40f;
  float hold_y_max_board_frac = 0.44f;
  float queue_y_max_board_frac = 0.80f;
};

std::optional<Detection> detect(const CvImage& img,
                                 const DetectParams& params = {});

}  // namespace imp
