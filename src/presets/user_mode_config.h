#pragma once

#include "engine/piece.h"
#include <cstdint>
#include <string>
#include <vector>

struct UserModeConfig {
  // --- Meta ---
  std::string name = "Custom";
  std::string description;

  // --- Queue ---
  // Initial fixed piece sequence (empty = none).
  std::vector<PieceType> initial_queue;
  bool shuffle_initial = false;

  // What happens after the initial queue is consumed.
  enum class Continuation { SevenBag, Random, None };
  Continuation continuation = Continuation::SevenBag;

  // Total piece count (initial + continuation combined). 0 = infinite.
  int count = 0;

  // --- Board ---
  enum class BoardType { Empty, Predetermined, Generated };
  BoardType board_type = BoardType::Empty;

  // Predetermined: 10-bit row bitmasks, index 0 = bottom row.
  std::vector<uint16_t> board_rows;

  // Generated board parameters (stub — algorithm TBD).
  int gen_max_height = 6;
  float gen_bumpiness = 3.f;
  float gen_sparseness = 0.3f;
  bool gen_allow_holes = false;

  // --- Goal ---
  // All conditions must be met simultaneously to win.
  // Counts of 0 mean "no requirement" for that condition.
  // Validation for setup generation is derived from these same conditions.
  bool use_all_queue = false; // must place all pieces before evaluating
  int pc = 0;                 // perfect clears achieved
  int tsd = 0;                // T-spin doubles achieved
  int tst = 0;                // T-spin triples achieved
  int quad = 0;               // quads achieved
  int combo = 0;              // max combo reached
  int b2b = 0;                // max B2B reached
  int lines = 0;              // total lines cleared
  int max_overhangs = -1;     // end board: max blocked columns (-1 = ignore)
  int max_holes = -1;         // end board: max holes (-1 = ignore)

  // --- Derived ---
  // Whether generated boards need AI validation (true if board is generated
  // AND goals require specific clears like PC/TSD that depend on board+queue).
  bool needs_validation() const {
    return board_type == BoardType::Generated;
  }
};

UserModeConfig parse_user_mode(const std::string &path);
