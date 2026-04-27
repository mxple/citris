#pragma once

#include "engine/piece.h"
#include <optional>
#include <vector>

// State for the F3-toggleable debug sidebar panel. Owned by GameManager;
// passed by reference to whatever needs to read or mutate it (ToolController
// toggles `open`, the queue editor writes `pending_queue_replacement`,
// GameManager drains it once per frame).
//
// Independent of AIState: the panel hosts AI debug controls when an AI is
// wired in, but also non-AI sections (queue editor) and may host more in
// the future. Adding a new section means adding a field here and a draw
// call in src/ui/debug_panel.cc — no AIState surgery required.
struct DebugState {
  bool open = false;

  // Queue-editor commit. Drained by GameManager before the next Game::apply.
  // pieces[0] becomes the new current piece (cmd::ReplaceCurrentPiece);
  // pieces[1..] replace the buffered queue prefix (cmd::ReplaceQueuePrefix).
  std::optional<std::vector<PieceType>> pending_queue_replacement;

  // One-shot clear-hold request from the debug panel. GameManager pushes
  // cmd::ClearHold and resets this flag.
  bool clear_hold = false;
};
