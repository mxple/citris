#pragma once

// Port of MinusKelvin's pcf piece state generation.

#include "pc_board.h"
#include <cstdint>
#include <span>

// A piece in a specific rotation at a specific vertical position within the
// 6-row PC region, possibly with hurdled (non-adjacent) rows.
struct PcPieceState {
  uint64_t board;      // piece cells as bits (relative to column 0)
  uint64_t below_mask; // cells directly below piece (for support check)
  uint8_t width;       // bounding box width in columns
  uint8_t y;           // y of the lowest row containing a piece cell
  uint8_t hurdles;     // bitmask: rows between piece rows that must be full
  uint8_t piece_idx;   // PieceType enum value (I=0..L=6)
};

// Initialize piece state tables. Called automatically on first use.
void pc_data_init();

// Piece states for a given height cap, piece type index, and target cell y.
// The target cell y is the y-coordinate of the lowest cell at column 0 in the
// piece state's local frame. States are sorted by width (ascending).
std::span<const PcPieceState> pc_piece_states(int height, int piece_idx,
                                              int cell_y);
