#include "tbp/internal_bot.h"

#include "ai/eval/atk.h"
#include "tbp/conversions.h"

namespace tbp {

InternalTbpBot::InternalTbpBot(InternalBotConfig cfg) : cfg_(std::move(cfg)) {}

InternalTbpBot::~InternalTbpBot() {
  // BeamTask destructor cancels and joins.
}

Info InternalTbpBot::info() const {
  Info i;
  i.name = cfg_.name;
  i.version = cfg_.version;
  i.author = cfg_.author;
  // We support the randomizer extension (we don't use it for piece generation
  // since the queue is fed externally, but we accept it in `start`) and
  // emit basic move_info fields when available.
  i.features = {"randomizer", "move_info"};
  return i;
}

std::variant<Ready, Error> InternalTbpBot::rules(const Rules &) {
  // We accept every randomizer; the queue is supplied to us via start /
  // new_piece, so the underlying generation policy doesn't constrain us.
  return Ready{};
}

void InternalTbpBot::start(const Start &s) {
  board_ = tbp_to_bitset(s.board);
  queue_ = tbp_to_queue(s.queue);
  hold_.reset();
  if (s.hold) {
    if (auto p = piece_type_from_str(*s.hold)) hold_ = p;
  }
  hold_available_ = true;
  attack_ = make_attack_state(s.combo, s.back_to_back);
  // Previous game's PV is meaningless against a fresh position.
  last_pv_.clear();
  launch_search();
}

std::optional<Suggestion> InternalTbpBot::poll_suggestion() {
  if (needs_restart_) {
    launch_search();
    needs_restart_ = false;
  }
  if (!task_) return std::nullopt;
  if (!task_->ready()) return std::nullopt;

  BeamResult result = task_->get();
  task_.reset(); // single-shot — next launch_search() builds a new task

  // Cache the full PV so the controller can render it as a plan overlay.
  // Replaces any stale PV from a previous search.
  last_pv_ = std::move(result.pv);

  Suggestion sug;
  // Prefer the principal-variation head as our top choice. If pv is empty
  // (degenerate), forfeit by sending an empty moves list per the spec.
  if (!last_pv_.empty()) {
    Move m;
    m.location = placement_to_location(last_pv_.front());
    m.spin = spin_to_str(last_pv_.front().spin);
    sug.moves.push_back(std::move(m));
  }
  // Optional move_info for diagnostics.
  MoveInfo mi;
  mi.depth = result.depth;
  sug.move_info = std::move(mi);
  return sug;
}

void InternalTbpBot::play(const Play &p) {
  // Cancel any in-flight search; we'll restart from the post-play state on the
  // next poll_suggestion. (Restarting eagerly here would race with new_piece,
  // which always arrives between play and the next suggest per the TBP flow.)
  if (task_) {
    task_->cancel();
    task_.reset();
  }

  if (queue_.empty()) {
    needs_restart_ = true;
    return;
  }

  auto placed = piece_type_from_str(p.move.location.type);
  if (!placed) {
    needs_restart_ = true;
    return;
  }

  // Hold inference (per TBP spec — see internal_bot.h docstring).
  bool used_hold = (*placed != queue_.front());
  if (used_hold) {
    if (hold_) {
      // Swap: old current -> hold, played piece is old hold.
      auto new_hold = queue_.front();
      queue_.erase(queue_.begin());
      hold_ = new_hold;
    } else {
      // Empty hold: current goes to hold, played is queue_[1].
      if (queue_.size() < 2) {
        needs_restart_ = true;
        return;
      }
      hold_ = queue_.front();
      queue_.erase(queue_.begin()); // remove old current
      queue_.erase(queue_.begin()); // remove the placed piece (was queue_[1])
    }
  } else {
    queue_.erase(queue_.begin());
  }

  // Place the piece on the board (final position from the suggestion).
  Placement plc = location_to_placement(p.move.location, spin_from_str(p.move.spin));
  board_.place(plc.type, plc.rotation, plc.x, plc.y);
  int cleared = board_.clear_lines();
  (void)compute_attack_and_update_state(attack_, cleared, plc.spin);

  // Advance the cached PV in lockstep: the placement we just played was
  // last_pv_.front (assuming no external override), so drop it. If something
  // unexpected lands (type mismatch, external play()), just clear the PV —
  // it's stale and shouldn't be rendered.
  if (!last_pv_.empty() && last_pv_.front().type == plc.type)
    last_pv_.erase(last_pv_.begin());
  else
    last_pv_.clear();

  hold_available_ = true;
  needs_restart_ = true;
}

void InternalTbpBot::new_piece(const NewPiece &n) {
  if (auto p = piece_type_from_str(n.piece)) queue_.push_back(*p);
  needs_restart_ = true;
}

void InternalTbpBot::stop() {
  if (task_) {
    task_->cancel();
    task_.reset();
  }
}

void InternalTbpBot::quit() { stop(); }

void InternalTbpBot::launch_search() {
  if (task_) {
    task_->cancel();
    task_.reset();
  }
  needs_restart_ = false;
  if (queue_.empty()) return; // nothing to plan with

  BeamInput input;
  input.board = board_;
  input.queue = queue_;
  input.hold = hold_;
  input.hold_available = hold_available_;
  input.queue_draws = 0;
  input.attack = attack_;

  BeamConfig cfg;
  cfg.width = cfg_.beam_width;
  cfg.depth = cfg_.max_depth;
  cfg.sonic_only = cfg_.sonic_only;
  cfg.extend_7bag = cfg_.extend_7bag;
  cfg.evaluator = std::make_unique<AtkEvaluator>();

  task_ = start_beam_search(std::move(input), std::move(cfg));
}

} // namespace tbp
