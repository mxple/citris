#pragma once

#include "cv_image.h"
#include "engine/board.h"
#include "pch.h"
#include <array>

namespace imp {

// HSV with H in degrees [0,360), S/V in [0,1].
struct Hsv {
  float h = 0.f;
  float s = 0.f;
  float v = 0.f;
};

Hsv rgb_to_hsv(Color c);

// Engine-coordinate board (40 rows × 10 cols, row 0 at the bottom).
using BoardCells = std::array<std::array<CellColor, Board::kWidth>,
                              Board::kTotalHeight>;

}  // namespace imp
