#include "color.h"

#include <algorithm>
#include <cmath>

namespace imp {

Hsv rgb_to_hsv(Color c) {
  float r = c.r / 255.f, g = c.g / 255.f, b = c.b / 255.f;
  float mx = std::max({r, g, b});
  float mn = std::min({r, g, b});
  float d = mx - mn;
  Hsv out;
  out.v = mx;
  out.s = (mx <= 0.f) ? 0.f : (d / mx);
  if (d <= 0.f) {
    out.h = 0.f;
  } else if (mx == r) {
    out.h = 60.f * std::fmod((g - b) / d, 6.f);
  } else if (mx == g) {
    out.h = 60.f * (((b - r) / d) + 2.f);
  } else {
    out.h = 60.f * (((r - g) / d) + 4.f);
  }
  if (out.h < 0.f) out.h += 360.f;
  return out;
}

}  // namespace imp
