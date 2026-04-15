#pragma once

#include "engine/piece.h"
#include "engine/attack.h"
#include "vec2.h"
#include <array>
#include <cstdint>

// A legal landing position for a piece on the board.
struct Placement {
  PieceType type;
  Rotation rotation;
  int8_t x, y;                        // bounding-box bottom-left on board
  SpinKind spin{};                     // detected during movegen

  Piece to_piece() const { return Piece(type, rotation, x, y); }

  std::array<Vec2, 4> cells() const {
    const auto &rel = kPieceCells[static_cast<int>(type)][static_cast<int>(rotation)];
    std::array<Vec2, 4> out;
    for (int i = 0; i < 4; ++i)
      out[i] = {rel[i].x + x, rel[i].y + y};
    return out;
  }

  bool operator==(const Placement &) const = default;
};

// Per-piece usage counts, packed into 7 bytes.
using PieceCounts = std::array<uint8_t, 7>;
