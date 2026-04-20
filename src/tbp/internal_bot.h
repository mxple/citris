#pragma once

// In-process TbpBot wrapping Citris's BeamTask. Skips JSON entirely — the
// state stays as native C++ structs and the search runs on a background
// thread exactly the same way the GUI uses it.
//
// Lifecycle:
//   - start() converts the TBP position to a BeamInput and spawns BeamTask.
//   - poll_suggestion() polls the task; if ready, translates BeamResult.pv
//     into a TBP Suggestion (multiple preferred moves from the principal
//     variation, head first).
//   - play() applies the played move to the local state (board, queue, hold,
//     attack), then marks the search dirty so the next poll_suggestion()
//     restarts it.
//   - new_piece() appends to the local queue and marks dirty.
//   - stop() / quit() cancel the in-flight task.

#include "ai/beam_search.h"
#include "ai/board_bitset.h"
#include "engine/attack.h"
#include "engine/piece.h"
#include "tbp/bot.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tbp {

struct InternalBotConfig {
  std::string name = "Citris";
  std::string version = "0.1.0";
  std::string author = "mxple";
  int beam_width = 800;
  int max_depth = 14;
  bool sonic_only = true;
  bool extend_7bag = true;
};

class InternalTbpBot : public TbpBot {
public:
  explicit InternalTbpBot(InternalBotConfig cfg = {});
  ~InternalTbpBot() override;

  Info info() const override;
  std::variant<Ready, Error> rules(const Rules &) override;
  void start(const Start &) override;
  std::optional<Suggestion> poll_suggestion() override;
  void play(const Play &) override;
  void new_piece(const NewPiece &) override;
  void stop() override;
  void quit() override;

private:
  // Cancel any in-flight task and start a new one from the current state_.
  void launch_search();

  InternalBotConfig cfg_;

  // Live game state — mutated by start/play/new_piece.
  BoardBitset board_;
  std::vector<PieceType> queue_; // queue_[0] = current piece
  std::optional<PieceType> hold_;
  bool hold_available_ = true;
  AttackState attack_{};

  // Async search.
  std::unique_ptr<BeamTask> task_;
  bool needs_restart_ = false;
};

} // namespace tbp
