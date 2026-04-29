#pragma once

#include "pch.h"
#include <cstdint>
#include <vector>

namespace imp {

// RGBA8 pixel buffer used by the clipboard-import classifier. Owns its bytes;
// 4 bytes per pixel, row-major, top-down (y=0 is top of source image).
struct CvImage {
  int w = 0;
  int h = 0;
  std::vector<uint8_t> rgba;  // size = w * h * 4

  bool empty() const { return w == 0 || h == 0 || rgba.empty(); }

  Color pixel(int x, int y) const {
    int i = (y * w + x) * 4;
    return {rgba[i + 0], rgba[i + 1], rgba[i + 2], rgba[i + 3]};
  }
};

// Pixel-coordinate rectangle into a CvImage. (x,y) is top-left, w/h positive.
struct CvRect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;

  bool empty() const { return w <= 0 || h <= 0; }
};

}  // namespace imp
