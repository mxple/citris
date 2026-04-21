#pragma once

// Abstract bot interface. Both Citris's in-process AI (InternalTbpBot) and
// external subprocess bots (ExternalTbpBot, Phase 3) implement this.
//
// Methods correspond 1:1 with TBP messages, but pass typed structs instead of
// JSON. JSON encoding/decoding is done at the transport boundary (codec.h).
//
// Lifetime mirrors the TBP spec lifecycle:
//   info() -> rules() -> start() -> poll_suggestion()
//                                -> play() -> new_piece() -> poll_suggestion()
//                                -> ...
//                                -> stop()
//   Then start() can be called again for a new game, or quit() to terminate.

#include "ai/placement.h"
#include "tbp/types.h"

#include <optional>
#include <variant>
#include <vector>

namespace tbp {

class TbpBot {
public:
  virtual ~TbpBot() = default;

  // Identity advertised to a frontend immediately after the process spawns.
  virtual Info info() const = 0;

  // Frontend negotiates rules. Bot returns Ready (accepts) or Error
  // (rejects, e.g. "unsupported_rules").
  virtual std::variant<Ready, Error> rules(const Rules &) = 0;

  // Begin calculating from the supplied position. Replaces any previous game.
  virtual void start(const Start &) = 0;

  // Non-blocking. Returns nullopt if no suggestion is ready yet.
  // The caller is responsible for spin/wait policy.
  virtual std::optional<Suggestion> poll_suggestion() = 0;

  // Frontend tells the bot a move has been played. Bot advances state.
  virtual void play(const Play &) = 0;

  // Frontend appends a piece to the queue.
  virtual void new_piece(const NewPiece &) = 0;

  // Ask the bot for its current best move. For external bots this emits a
  // TBP Suggest message over the wire; for in-process bots it's typically a
  // no-op (they're always calculating). Decoupling this from play() lets the
  // controller issue Suggest *after* relaying new_piece() updates, so the
  // bot searches on the full queue rather than a stale prefix.
  virtual void request_suggestion() {}

  // Pause calculation but stay alive (next start() may begin a new game).
  virtual void stop() = 0;

  // Tear down. After this, the bot should not be used.
  virtual void quit() = 0;

  // Cached principal variation / plan from the most recent poll, as engine
  // Placements. Used for plan-overlay rendering. Default: empty (bots that
  // don't expose a PV simply render nothing). The returned vector is a
  // snapshot — callers can consume it without locking.
  virtual std::vector<Placement> last_plan() const { return {}; }
};

} // namespace tbp
