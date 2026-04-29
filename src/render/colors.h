#pragma once

#include "engine/board.h"
#include "pch.h"

// Reference RGB for each CellColor, indexed by `static_cast<int>(CellColor::X)`.
// Renderer uses these as fallback tints when no skin is loaded; the clipboard-
// import CV uses them as the nearest-neighbor target table when classifying
// cells from a screenshot. Single source of truth — keep these visually
// distinct enough that nearest-neighbor stays unambiguous.
inline constexpr Color kCellRefColors[] = {
    Color::Transparent(), // Empty
    Color(135, 206, 250), // I
    Color(255, 255, 0),   // O
    Color(186, 85, 211),  // T
    Color(50, 205, 50),   // S
    Color(255, 105, 180), // Z
    Color(30, 144, 255),  // J
    Color(255, 165, 0),   // L
    Color(128, 128, 128), // Garbage
};

inline constexpr Color cell_ref_color(CellColor cc) {
  return kCellRefColors[static_cast<int>(cc)];
}
