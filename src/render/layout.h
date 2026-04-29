#pragma once

#include "engine/board.h"

struct RenderLayout {
  static constexpr int kBoardCols = Board::kWidth;        // 10
  static constexpr int kPlayRows = Board::kVisibleHeight; // 20
  static constexpr int kPadRowsNorth = 3;                 // spawn area
  static constexpr int kPadRowsSouth = 1; // 1 cell padding under play area
  static constexpr int kSideCols = 5;     // hold / next width
  static constexpr int kMiniRows = 3;     // hold / preview slot height

  static constexpr int kSceneCols = kSideCols + kBoardCols + kSideCols;
  static constexpr int kSceneRows = kPlayRows + kPadRowsNorth + kPadRowsSouth;

  // Column offsets of the three main columns.
  static constexpr int kHoldColX = 0;
  static constexpr int kBoardColX = kSideCols;
  static constexpr int kNextColX = kSideCols + kBoardCols;

  // Skin texture coords — pixel coords in skin.png
  static constexpr int kTileSize = 30;
  static constexpr int kSkinPitch = 31;
  static constexpr int kSkinTiles = 12;

  static constexpr int kBoardOutline = 4; // border thickness in pixels

  static constexpr int kSkinGhost = 7;
  static constexpr int kSkinGarbage = 8;
  static constexpr int kSkinGreyedHold = 10;
};
