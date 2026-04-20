#pragma once

// Plain-data structs mirroring the Tetris Bot Protocol message schema.
// Spec: references/tbp-spec/text/0000-mvp.md (+ 0001-randomizer.md, 0003-move_info.md).
//
// These types are transport-agnostic. The codec (codec.h) maps them to/from
// JSON. Conversions to Citris's internal representations live in conversions.h.

#include <array>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace tbp {

// ---- Cells, pieces, orientations -------------------------------------------

// A board cell. Empty (null) is std::nullopt; otherwise the string identifies
// what filled it: "I"/"O"/"T"/"S"/"Z"/"J"/"L" for piece-colored cells, "G"
// for garbage.
using Cell = std::optional<std::string>;

// 40 rows x 10 cols. board[0] = bottom row, board[39] = top row.
using Board = std::array<std::array<Cell, 10>, 40>;

struct PieceLocation {
  std::string type;        // "I" | "O" | "T" | "S" | "Z" | "J" | "L"
  std::string orientation; // "north" | "east" | "south" | "west"
  int x = 0;               // SRS true-rotation center x (column from left)
  int y = 0;               // SRS true-rotation center y (row from bottom)
};

struct Move {
  PieceLocation location;
  std::string spin = "none"; // "none" | "mini" | "full"
};

// ---- Randomizer state (0001 extension) -------------------------------------

struct UniformRandomizer {};

struct SevenBagRandomizer {
  // Pieces remaining in the current bag at the END of the provided queue.
  // Non-empty per spec.
  std::vector<std::string> bag_state;
};

struct GeneralBagRandomizer {
  std::array<int, 7> current_bag{}; // I,O,T,S,Z,J,L counts
  std::array<int, 7> filled_bag{};
};

using RandomizerState =
    std::variant<UniformRandomizer, SevenBagRandomizer, GeneralBagRandomizer>;

// ---- move_info (0003 extension) --------------------------------------------

struct MoveInfo {
  std::optional<double> nodes;
  std::optional<double> nps;
  std::optional<int> depth;
  std::optional<std::string> extra;
};

// ---- Messages (Bot -> Frontend) --------------------------------------------

struct Info {
  std::string name;
  std::string version;
  std::string author;
  std::vector<std::string> features; // e.g. {"randomizer", "move_info"}
};

struct Ready {};

struct Error {
  std::string reason; // e.g. "unsupported_rules"
};

struct Suggestion {
  std::vector<Move> moves;       // in order of preference, may be empty (forfeit)
  std::optional<MoveInfo> move_info;
};

// ---- Messages (Frontend -> Bot) --------------------------------------------

struct Rules {
  // Randomizer string per 0001. Empty/unknown defaults to "unknown" per spec.
  std::optional<std::string> randomizer;
};

struct Start {
  std::optional<std::string> hold;       // null in JSON => nullopt here
  std::vector<std::string> queue;
  int combo = 0;
  bool back_to_back = false;
  Board board{};
  std::optional<RandomizerState> randomizer;
};

struct Stop {};

struct Suggest {};

struct Play {
  Move move;
};

struct NewPiece {
  std::string piece;
};

struct Quit {};

// ---- Tagged union for all messages -----------------------------------------

using Message = std::variant<Info, Ready, Error, Suggestion, Rules, Start, Stop,
                              Suggest, Play, NewPiece, Quit>;

} // namespace tbp
