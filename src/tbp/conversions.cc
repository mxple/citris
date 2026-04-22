#include "tbp/conversions.h"

// clang-format off
namespace tbp {

// --- Encode -----------------------------------------------------------------
//
// Switch with no default on purpose: adding an enum case trips -Wswitch so
// both directions are updated together.

std::string_view to_wire(PieceType t) {
  switch (t) {
  case PieceType::I: return "I";
  case PieceType::O: return "O";
  case PieceType::T: return "T";
  case PieceType::S: return "S";
  case PieceType::Z: return "Z";
  case PieceType::J: return "J";
  case PieceType::L: return "L";
  }
  return "I";
}

std::string_view to_wire(Rotation r) {
  switch (r) {
  case Rotation::North: return "north";
  case Rotation::East:  return "east";
  case Rotation::South: return "south";
  case Rotation::West:  return "west";
  }
  return "north";
}

std::string_view to_wire(SpinKind s) {
  switch (s) {
  case SpinKind::None:    return "none";
  case SpinKind::Mini:    return "mini";
  case SpinKind::TSpin:   return "full";
  case SpinKind::AllSpin: return "full";
  }
  return "none";
}

std::string_view to_wire(CellColor c) {
  switch (c) {
  case CellColor::Empty:   return "";
  case CellColor::I:       return "I";
  case CellColor::O:       return "O";
  case CellColor::T:       return "T";
  case CellColor::S:       return "S";
  case CellColor::Z:       return "Z";
  case CellColor::J:       return "J";
  case CellColor::L:       return "L";
  case CellColor::Garbage: return "G";
  }
  return "";
}

std::string_view to_wire(Randomizer r) {
  switch (r) {
  case Randomizer::Unknown:    return "unknown";
  case Randomizer::Uniform:    return "uniform";
  case Randomizer::SevenBag:   return "seven_bag";
  case Randomizer::GeneralBag: return "general_bag";
  }
  return "unknown";
}

// --- Decode -----------------------------------------------------------------

std::optional<PieceType> from_wire_piece(std::string_view s) {
  if (s.size() != 1) return std::nullopt;
  switch (s[0]) {
  case 'I': return PieceType::I;
  case 'O': return PieceType::O;
  case 'T': return PieceType::T;
  case 'S': return PieceType::S;
  case 'Z': return PieceType::Z;
  case 'J': return PieceType::J;
  case 'L': return PieceType::L;
  default:  return std::nullopt;
  }
}

std::optional<Rotation> from_wire_orientation(std::string_view s) {
  if (s == "north") return Rotation::North;
  if (s == "east")  return Rotation::East;
  if (s == "south") return Rotation::South;
  if (s == "west")  return Rotation::West;
  return std::nullopt;
}

SpinKind from_wire_spin(std::string_view s, std::string_view piece) {
  if (s == "mini") return SpinKind::Mini;
  if (s == "full") {
    auto p = from_wire_piece(piece);
    if (p && *p == PieceType::T)  return SpinKind::TSpin;
    else                          return SpinKind::AllSpin;
  }
  return SpinKind::None;
}

CellColor from_wire_cell(std::string_view s) {
  if (s == "G") return CellColor::Garbage;
  if (auto pt = from_wire_piece(s)) return piece_to_cell_color(*pt);
  return CellColor::Empty;
}

Randomizer from_wire_randomizer(std::string_view s) {
  if (s == "uniform")     return Randomizer::Uniform;
  if (s == "seven_bag")   return Randomizer::SevenBag;
  if (s == "general_bag") return Randomizer::GeneralBag;
  return Randomizer::Unknown;
}

// --- Placement <-> PieceLocation (per-(piece, rotation) center offset) ----
//
// Offsets from Citris bounding-box bottom-left to TBP SRS true-rotation
// center mino. Verified against the spec (references/tbp-spec/text/0000-mvp.md
// section "piece location"):
//
//   T/S/Z/J/L: 3x3 BB, geometric center at (1,1) in all rotations.
//   O:         2x2 BB, the corner mino rotates with rotation:
//                N=(0,0), E=(0,1), S=(1,1), W=(1,0)
//   I:         4x4 BB, "middle-left/top/right/bottom" mino:
//                N=(1,2), E=(2,2), S=(2,1), W=(1,1)
//
// (See kPieceCells in src/engine/piece.h for the BB-relative cells these
// align to.)

namespace {

struct CenterOffset { int dx; int dy; };

constexpr CenterOffset kCenterOffsets[7][4] = {
    // I: N         E         S         W
    {{1, 2},  {2, 2},  {2, 1},  {1, 1}},
    // O
    {{0, 0},  {0, 1},  {1, 1},  {1, 0}},
    // T
    {{1, 1},  {1, 1},  {1, 1},  {1, 1}},
    // S
    {{1, 1},  {1, 1},  {1, 1},  {1, 1}},
    // Z
    {{1, 1},  {1, 1},  {1, 1},  {1, 1}},
    // J
    {{1, 1},  {1, 1},  {1, 1},  {1, 1}},
    // L
    {{1, 1},  {1, 1},  {1, 1},  {1, 1}},
};

CenterOffset center_offset(PieceType t, Rotation r) {
  return kCenterOffsets[static_cast<int>(t)][static_cast<int>(r)];
}

} // namespace

PieceLocation placement_to_location(const Placement &p) {
  auto off = center_offset(p.type, p.rotation);
  return PieceLocation{
      .type = p.type,
      .orientation = p.rotation,
      .x = static_cast<int>(p.x) + off.dx,
      .y = static_cast<int>(p.y) + off.dy,
  };
}

Placement location_to_placement(const PieceLocation &loc, SpinKind spin) {
  auto off = center_offset(loc.type, loc.orientation);
  // "full" decodes to AllSpin by default; promote to TSpin on the T piece
  // so downstream attack/scoring treats it correctly.
  if (loc.type == PieceType::T && spin == SpinKind::AllSpin)
    spin = SpinKind::TSpin;
  return Placement{
      .type = loc.type,
      .rotation = loc.orientation,
      .x = static_cast<int8_t>(loc.x - off.dx),
      .y = static_cast<int8_t>(loc.y - off.dy),
      .spin = spin,
  };
}

// --- Board <-> TBP board ----------------------------------------------------
// Both sides carry CellColor, so conversion is a straight copy.

Board board_to_tbp(const ::Board &b) {
  Board out{};
  for (int y = 0; y < ::Board::kTotalHeight && y < 40; ++y)
    for (int x = 0; x < ::Board::kWidth; ++x)
      out[y][x] = b.cell_color(x, y);
  return out;
}

::Board tbp_to_board(const Board &b) {
  ::Board out;
  for (int y = 0; y < ::Board::kTotalHeight && y < 40; ++y)
    for (int x = 0; x < ::Board::kWidth; ++x)
      out.set_cell(x, y, b[y][x]);
  return out;
}

BoardBitset tbp_to_bitset(const Board &b) {
  BoardBitset bb;
  for (int y = 0; y < BoardBitset::kHeight && y < 40; ++y) {
    for (int x = 0; x < BoardBitset::kWidth; ++x) {
      if (b[y][x] != CellColor::Empty) {
        bb.rows[y] |= uint16_t(1) << x;
        bb.cols[x] |= uint64_t(1) << y;
      }
    }
  }
  return bb;
}

} // namespace tbp
