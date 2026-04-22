#pragma once

#include "engine/attack.h"
#include "engine/piece.h"
#include "notification.h"
#include <optional>
#include <variant>

// These are events the engine emits after applying input and timer events

namespace eng {
struct PieceLocked {
  PieceType type;
  Rotation rotation;
  int8_t x, y;
  int lines_cleared;
  SpinKind spin;
  bool perfect_clear;
  int attack;
  int prev_combo;
  int new_combo;
  int new_b2b;
};
struct PieceSpawned {
  PieceType type;
};
struct HoldUsed {
  PieceType swapped_in;
  std::optional<PieceType> swapped_out;
};
struct GameOver {
  bool won;
};
struct GarbageMaterialized {
  int lines;
};
struct UndoPerformed {};
struct LockDelayExpired {};
struct IllegalPlacement {};
struct QueueRefill {
  PieceType piece;
};
} // namespace eng

using EngineEvent =
    std::variant<eng::PieceLocked, eng::PieceSpawned, eng::HoldUsed,
                 eng::GameOver, eng::GarbageMaterialized, eng::UndoPerformed,
                 eng::LockDelayExpired, eng::IllegalPlacement,
                 eng::QueueRefill, Notification>;
