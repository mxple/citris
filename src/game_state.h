#pragma once

#include "board.h"
#include "piece.h"
#include <array>
#include <optional>

// Read-only snapshot of game state for rendering / AI.
// Cheap to copy (Board is ~80 bytes + a few scalars).
struct GameState {
  Board board;
  Piece current_piece{PieceType::I};
  Piece ghost_piece{PieceType::I};
  std::optional<PieceType> hold_piece;
  bool hold_available = true;
  std::array<PieceType, 6> preview;
  AttackState attack_state;
  bool game_over = false;
  int piece_gen = 0; // incremented on spawn/hold
};
