#pragma once

// Bidirectional conversions between Citris internals and TBP wire types.
// JSON does not appear here — it lives in codec.{h,cc}. This file deals only
// with translating between Citris's PieceType/Rotation/Placement/Board and
// TBP's strings/arrays/PieceLocation.

#include "ai/board_bitset.h"
#include "ai/placement.h"
#include "engine/attack.h"
#include "engine/board.h"
#include "engine/piece.h"
#include "tbp/types.h"

#include <optional>
#include <string>
#include <string_view>

namespace tbp {

// ---- Piece type ↔ TBP string -----------------------------------------------

// "I"|"O"|"T"|"S"|"Z"|"J"|"L"
const char *piece_type_to_str(PieceType t);
std::optional<PieceType> piece_type_from_str(std::string_view s);

// ---- Rotation ↔ TBP orientation string -------------------------------------

const char *rotation_to_str(Rotation r);
std::optional<Rotation> rotation_from_str(std::string_view s);

// ---- SpinKind ↔ TBP spin string --------------------------------------------

// SRS spin kinds:
//   None      -> "none"
//   Mini      -> "mini"
//   TSpin     -> "full"
//   AllSpin   -> "full"   (TBP's spec only distinguishes none/mini/full)
const char *spin_to_str(SpinKind s);
SpinKind spin_from_str(std::string_view s);

// ---- Placement ↔ TBP PieceLocation -----------------------------------------

// Citris stores a Placement as the bounding-box bottom-left (x,y). TBP uses
// the SRS true-rotation center mino, which is per-(piece, rotation). The
// table below is the offset from BB-bottom-left to the center mino.
PieceLocation placement_to_location(const Placement &p);

// Inverse: TBP location -> Placement (without spin info — set spin separately
// from the surrounding TbpMove if needed).
Placement location_to_placement(const PieceLocation &loc, SpinKind spin);

// ---- Board cell color ↔ TBP cell ------------------------------------------

Cell cell_color_to_tbp(CellColor c);
CellColor tbp_cell_to_color(const Cell &c);

// ---- Board ↔ TBP board -----------------------------------------------------

Board board_to_tbp(const ::Board &b);
::Board tbp_to_board(const Board &b);
BoardBitset tbp_to_bitset(const Board &b);

// ---- Queue (vector<string> ↔ vector<PieceType>) ----------------------------

std::vector<std::string> queue_to_tbp(const std::vector<PieceType> &q);
std::vector<PieceType> tbp_to_queue(const std::vector<std::string> &q);

// ---- AttackState helpers (combo/b2b come straight across the wire as ints/bool) ---

inline AttackState make_attack_state(int combo, bool b2b) {
  return AttackState{combo, b2b ? 1 : 0};
}

} // namespace tbp
