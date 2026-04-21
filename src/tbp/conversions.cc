#include "tbp/conversions.h"

namespace tbp {

// --- Piece type strings -----------------------------------------------------

const char *piece_type_to_str(PieceType t) {
  switch (t) {
  case PieceType::I: return "I";
  case PieceType::O: return "O";
  case PieceType::T: return "T";
  case PieceType::S: return "S";
  case PieceType::Z: return "Z";
  case PieceType::J: return "J";
  case PieceType::L: return "L";
  }
  return "?";
}

std::optional<PieceType> piece_type_from_str(std::string_view s) {
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

// --- Rotation strings -------------------------------------------------------

const char *rotation_to_str(Rotation r) {
  switch (r) {
  case Rotation::North: return "north";
  case Rotation::East:  return "east";
  case Rotation::South: return "south";
  case Rotation::West:  return "west";
  }
  return "north";
}

std::optional<Rotation> rotation_from_str(std::string_view s) {
  if (s == "north") return Rotation::North;
  if (s == "east")  return Rotation::East;
  if (s == "south") return Rotation::South;
  if (s == "west")  return Rotation::West;
  return std::nullopt;
}

// --- Spin strings -----------------------------------------------------------

const char *spin_to_str(SpinKind s) {
  switch (s) {
  case SpinKind::None:    return "none";
  case SpinKind::Mini:    return "mini";
  case SpinKind::TSpin:   return "full";
  case SpinKind::AllSpin: return "full";
  }
  return "none";
}

SpinKind spin_from_str(std::string_view s) {
  if (s == "mini") return SpinKind::Mini;
  if (s == "full") return SpinKind::AllSpin; // we lose AllSpin distinction here
  return SpinKind::None;
}

// --- Placement <-> PieceLocation (per-(piece, rotation) center offset) ------
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
      .type = piece_type_to_str(p.type),
      .orientation = rotation_to_str(p.rotation),
      .x = static_cast<int>(p.x) + off.dx,
      .y = static_cast<int>(p.y) + off.dy,
  };
}

Placement location_to_placement(const PieceLocation &loc, SpinKind spin) {
  auto type = piece_type_from_str(loc.type).value_or(PieceType::I);
  auto rot = rotation_from_str(loc.orientation).value_or(Rotation::North);
  auto off = center_offset(type, rot);
  if (type == PieceType::T && spin == SpinKind::AllSpin) 
    spin = SpinKind::TSpin;
  return Placement{
      .type = type,
      .rotation = rot,
      .x = static_cast<int8_t>(loc.x - off.dx),
      .y = static_cast<int8_t>(loc.y - off.dy),
      .spin = spin,
  };
}

// --- Board cell color -------------------------------------------------------

Cell cell_color_to_tbp(CellColor c) {
  switch (c) {
  case CellColor::Empty:   return std::nullopt;
  case CellColor::I:       return std::string("I");
  case CellColor::O:       return std::string("O");
  case CellColor::T:       return std::string("T");
  case CellColor::S:       return std::string("S");
  case CellColor::Z:       return std::string("Z");
  case CellColor::J:       return std::string("J");
  case CellColor::L:       return std::string("L");
  case CellColor::Garbage: return std::string("G");
  }
  return std::nullopt;
}

CellColor tbp_cell_to_color(const Cell &c) {
  if (!c) return CellColor::Empty;
  if (*c == "G") return CellColor::Garbage;
  auto pt = piece_type_from_str(*c);
  if (!pt) return CellColor::Garbage; // unknown -> opaque
  return piece_to_cell_color(*pt);
}

// --- Board <-> TBP board ----------------------------------------------------

Board board_to_tbp(const ::Board &b) {
  Board out{};
  for (int y = 0; y < ::Board::kTotalHeight && y < 40; ++y)
    for (int x = 0; x < ::Board::kWidth; ++x)
      out[y][x] = cell_color_to_tbp(b.cell_color(x, y));
  return out;
}

::Board tbp_to_board(const Board &b) {
  ::Board out;
  for (int y = 0; y < ::Board::kTotalHeight && y < 40; ++y)
    for (int x = 0; x < ::Board::kWidth; ++x)
      out.set_cell(x, y, tbp_cell_to_color(b[y][x]));
  return out;
}

BoardBitset tbp_to_bitset(const Board &b) {
  BoardBitset bb;
  for (int y = 0; y < BoardBitset::kHeight && y < 40; ++y) {
    for (int x = 0; x < BoardBitset::kWidth; ++x) {
      if (b[y][x].has_value()) {
        bb.rows[y] |= uint16_t(1) << x;
        bb.cols[x] |= uint64_t(1) << y;
      }
    }
  }
  return bb;
}

// --- Queue ------------------------------------------------------------------

std::vector<std::string> queue_to_tbp(const std::vector<PieceType> &q) {
  std::vector<std::string> out;
  out.reserve(q.size());
  for (auto t : q) out.emplace_back(piece_type_to_str(t));
  return out;
}

std::vector<PieceType> tbp_to_queue(const std::vector<std::string> &q) {
  std::vector<PieceType> out;
  out.reserve(q.size());
  for (auto &s : q) {
    if (auto p = piece_type_from_str(s)) out.push_back(*p);
  }
  return out;
}

} // namespace tbp
