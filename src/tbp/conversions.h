#pragma once

// Bidirectional translation between Citris internals and TBP wire domains.
//
// For each enumerated wire domain (piece letter, orientation, spin, cell,
// randomizer tag):
//
//   to_wire(enum)           -> std::string_view  (zero-alloc lookup)
//   from_wire_*(string_view) -> enum / optional<enum>
//
// Both directions are implemented as switch/if-chains in conversions.cc so
// -Wswitch catches an unhandled enum value at compile time, and the wire
// spellings for in/out are adjacent in source and hard to desync.
//
// Structural converters (placement <-> location, board copies, bitset) do
// more than an enum lookup and stay as their own functions. JSON appears
// only in codec.{h,cc}.

#include "ai/board_bitset.h"
#include "ai/placement.h"
#include "engine/attack.h"
#include "engine/board.h"
#include "engine/piece.h"
#include "tbp/types.h"

#include <optional>
#include <string_view>

namespace tbp {

// ---- Encode: enum -> wire string_view -------------------------------------

std::string_view to_wire(PieceType t);
std::string_view to_wire(Rotation r);
// TSpin and AllSpin both map to "full"; TBP does not distinguish them.
std::string_view to_wire(SpinKind s);
// CellColor::Empty maps to "" — codec callers should emit JSON null for
// empty cells instead of writing the empty string. The mapping exists so
// exhaustiveness stays compiler-checked.
std::string_view to_wire(CellColor c);
std::string_view to_wire(Randomizer r);

// ---- Decode: wire string_view -> enum -------------------------------------

std::optional<PieceType> from_wire_piece(std::string_view s);
std::optional<Rotation> from_wire_orientation(std::string_view s);

// "full" is ambiguous between TSpin and AllSpin on the wire; the piece
// letter disambiguates (T-piece + "full" => TSpin, else AllSpin).
SpinKind from_wire_spin(std::string_view s, std::string_view piece);

// Unrecognized strings decode to Empty — the safest fallback is to treat
// the cell as unfilled rather than block a live mino.
CellColor from_wire_cell(std::string_view s);

// Unrecognized / missing -> Unknown, per 0001 spec guidance.
Randomizer from_wire_randomizer(std::string_view s);

// ---- Placement <-> PieceLocation ------------------------------------------
//
// Citris stores a Placement as the bounding-box bottom-left (x,y). TBP uses
// the SRS true-rotation center mino, which is per-(piece, rotation). The
// math lives in conversions.cc.

PieceLocation placement_to_location(const Placement &p);
Placement location_to_placement(const PieceLocation &loc, SpinKind spin);

// ---- Board <-> TBP board (both carry CellColor, conversion is a re-pack) --

Board board_to_tbp(const ::Board &b);
::Board tbp_to_board(const Board &b);
BoardBitset tbp_to_bitset(const Board &b);

// ---- Misc -----------------------------------------------------------------

inline AttackState make_attack_state(int combo, bool b2b) {
  return AttackState{combo, b2b ? 1 : 0};
}

} // namespace tbp
