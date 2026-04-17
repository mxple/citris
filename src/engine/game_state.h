#pragma once

#include "attack.h"
#include "board.h"
#include "piece.h"
#include <optional>
#include <vector>

struct LastClear {
  int lines = 0;
  SpinKind spin = SpinKind::None;
  bool perfect_clear = false;
  int piece_gen = 0;
};

// Read-only snapshot of game state for rendering / AI.
struct GameState {
  Board board;
  Piece current_piece{PieceType::I};
  Piece ghost_piece{PieceType::I};
  std::optional<PieceType> hold_piece;
  bool hold_available = true;
  std::vector<PieceType> queue;
  AttackState attack_state;
  bool game_over = false;
  bool won = false;
  int piece_gen = 0; // incremented on spawn/hold
  int bag_draws = 0; // incremented only on spawn (= bag next() calls)
  int lines_cleared = 0;
  int total_attack = 0;
  LastClear last_clear;
};
