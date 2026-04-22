#pragma once

// Plain-data structs mirroring the Tetris Bot Protocol message schema.
// Spec: references/tbp-spec/text/0000-mvp.md (+ 0001-randomizer.md, 0003-move_info.md).
//
// These types are transport-agnostic. The codec (codec.h) maps them to/from
// JSON, and is the only layer that touches std::string for enumerated
// domains. Everything enumerable (piece letters, orientations, spin kinds,
// cell colors, randomizer tags) is carried as a typed enum reused from the
// engine where possible.
//
// Wire spelling is centralized in conversions.h: to_wire(x) for encode,
// from_wire_*(sv) for decode. Both directions are backed by the same
// switch, so adding an enum case trips -Wswitch in both places.

#include "engine/attack.h" // SpinKind
#include "engine/board.h"  // CellColor
#include "engine/piece.h"  // PieceType, Rotation

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace tbp {

// TBP 0001 randomizer hint. "Unknown" absorbs missing/unrecognized values so
// forward-compat doesn't require an extra optional layer.
enum class Randomizer : uint8_t { Unknown, Uniform, SevenBag, GeneralBag };

// ---- Sub-objects -----------------------------------------------------------

struct PieceLocation {
  PieceType type = PieceType::I;
  Rotation orientation = Rotation::North;
  int x = 0; // SRS true-rotation center x (column from left)
  int y = 0; // SRS true-rotation center y (row from bottom)
};

struct Move {
  PieceLocation location;
  // SpinKind::AllSpin is wire-equivalent to SpinKind::TSpin (both map to
  // "full"). conversions.cc disambiguates on re-entry based on piece type.
  SpinKind spin = SpinKind::None;
};

// 40 rows x 10 cols. board[0] = bottom row, board[39] = top row.
using Board = std::array<std::array<CellColor, 10>, 40>;

// ---- Randomizer state (0001 extension) ------------------------------------

struct UniformRandomizer {};

struct SevenBagRandomizer {
  // Pieces remaining in the current bag at the END of the provided queue.
  // Non-empty per spec.
  std::vector<PieceType> bag_state;
};

struct GeneralBagRandomizer {
  std::array<int, 7> current_bag{}; // I,O,T,S,Z,J,L counts
  std::array<int, 7> filled_bag{};
};

using RandomizerState =
    std::variant<UniformRandomizer, SevenBagRandomizer, GeneralBagRandomizer>;

// ---- move_info (0003 extension) -------------------------------------------

struct MoveInfo {
  std::optional<double> nodes;
  std::optional<double> nps;
  std::optional<int> depth;
  // Spec: arbitrary free-form text. Not an enumerated domain.
  std::optional<std::string> extra;
};

// ---- Messages (Bot -> Frontend) -------------------------------------------

// name/version/author/features are implementation-defined free-form strings.
struct Info {
  std::string name;
  std::string version;
  std::string author;
  std::vector<std::string> features;
};

struct Ready {};

struct Error {
  // Spec suggests codes like "unsupported_rules" but permits any string.
  std::string reason;
};

struct Suggestion {
  std::vector<Move> moves;
  std::optional<MoveInfo> move_info;
};

// ---- Messages (Frontend -> Bot) -------------------------------------------

struct Rules {
  std::optional<Randomizer> randomizer;
};

struct Start {
  std::optional<PieceType> hold;
  std::vector<PieceType> queue;
  int combo = 0;
  bool back_to_back = false;
  Board board{};
  std::optional<RandomizerState> randomizer;
};

struct Stop {};
struct Suggest {};
struct Play { Move move; };
struct NewPiece { PieceType piece; };
struct Quit {};

// ---- Tagged union for all messages ----------------------------------------

using Message = std::variant<Info, Ready, Error, Suggestion, Rules, Start, Stop,
                              Suggest, Play, NewPiece, Quit>;

} // namespace tbp
