#pragma once

#include "cv_image.h"
#include <optional>

namespace imp {

// Read an image from the system clipboard. Returns nullopt if no image is
// present, the platform doesn't expose binary clipboard data (e.g. Emscripten),
// or decoding fails. Tries image/png first, falls back to image/bmp (Windows
// often only exposes DIB).
//
// Caller owns the returned buffer (it's an RGBA8 copy, not a borrowed pointer).
std::optional<CvImage> read_clipboard_image();

}  // namespace imp
