#pragma once

#include "engine/attack.h"
#include "engine/piece.h"
#include "vec2.h"
#include <array>
#include <cstdint>

// A legal landing position for a piece on the board.
struct Placement {
  PieceType type;
  Rotation rotation;
  int8_t x, y;     // bounding-box bottom-left on board
  SpinKind spin{}; // detected during movegen

  Piece to_piece() const { return Piece(type, rotation, x, y); }

  std::array<Vec2, 4> cells() const {
    const auto &rel =
        kPieceCells[static_cast<int>(type)][static_cast<int>(rotation)];
    std::array<Vec2, 4> out;
    for (int i = 0; i < 4; ++i)
      out[i] = {rel[i].x + x, rel[i].y + y};
    return out;
  }

  // Collapse rotationally-symmetric duplicates to a single representative:
  //   O: all rotations -> North (same x, y)
  //   I/S/Z: South -> North at (x, y-1); West -> East at (x-1, y)
  // Movegen emits only canonical placements, so external placement sources
  // (e.g. TBP bots) must be normalized before comparison/application.
  Placement canonical() const {
    Placement p = *this;
    if (p.type == PieceType::O) {
      p.rotation = Rotation::North;
      return p;
    }
    if (p.type == PieceType::I || p.type == PieceType::S ||
        p.type == PieceType::Z) {
      if (p.rotation == Rotation::South) {
        p.rotation = Rotation::North;
        p.y -= 1;
      } else if (p.rotation == Rotation::West) {
        p.rotation = Rotation::East;
        p.x -= 1;
      }
    }
    return p;
  }

  // Two placements land on the same cells. Ignores `spin`, which is an
  // annotation rather than part of the placement's physical identity — many
  // bots don't track allspin and will declare `spin=None` for positions
  // movegen tags as `AllSpin`.
  bool same_landing(const Placement &other) const {
    return type == other.type && rotation == other.rotation && x == other.x &&
           y == other.y;
  }

  bool operator==(const Placement &) const = default;
};

template <> struct fmt::formatter<Placement> {
  constexpr auto parse(fmt::format_parse_context &ctx) { return ctx.begin(); }
  template <typename FormatContext>
  auto format(const Placement &p, FormatContext &ctx) const {
    return fmt::format_to(ctx.out(),
                          "Placement{{type={}, rot={}, pos=({},{}), spin={}}}",
                          p.type, p.rotation, p.x, p.y, p.spin);
  }
};

// Per-piece usage counts, packed into 7 bytes.
using PieceCounts = std::array<uint8_t, 7>;
