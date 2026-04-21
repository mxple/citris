#pragma once

#include "piece.h"
#include "piece_source.h"
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <vector>

// PieceQueue — buffered, mutable, previewable piece sequence backed by a
// PieceSource. Owns the prefetch buffer; refills lazily from the source
// when peek() / pop() demand it. The buffer can be inspected (peek), the
// dispense rate observed (draws), and the contents mutated mid-flight
// (shuffle, insert, replace, erase) — useful for puzzle modes that
// rearrange or splice pieces before play.
//
// Move-only: holds a unique_ptr to its source.
class PieceQueue {
public:
  // Construct from a source plus an optional prefix pre-loaded into the
  // buffer. `prefix[0]` will be the next piece dispensed.
  explicit PieceQueue(std::unique_ptr<PieceSource> source,
                      std::vector<PieceType> prefix = {});

  PieceQueue(PieceQueue &&) = default;
  PieceQueue &operator=(PieceQueue &&) = default;
  PieceQueue(const PieceQueue &) = delete;
  PieceQueue &operator=(const PieceQueue &) = delete;

  // Consume the front piece. Refills from the source if the buffer is
  // empty. Returns nullopt only when the buffer is empty AND the source
  // is exhausted.
  std::optional<PieceType> pop();

  // Read the next `n` pieces without consuming. Triggers source refill
  // until either `n` pieces are buffered or the source is exhausted, so
  // the returned vector may be shorter than `n`.
  std::vector<PieceType> peek(int n);

  // Total pieces popped via `pop()` over the queue's lifetime.
  int draws() const { return draws_; }

  // --- Mutations on the buffer ------------------------------------------
  // These operate on whatever's *currently buffered* (no implicit prefetch).
  // Callers wanting to mutate further upcoming pieces should peek(N) first
  // to ensure the buffer is large enough.

  enum class ShufflePolicy {
    HoldEquivalent, // permutation reachable from the original via hold ops
  };
  void shuffle(ShufflePolicy policy, std::mt19937 &rng);

  void insert(int pos, PieceType t);
  void replace(int pos, PieceType t);
  void erase(int pos);

  // Number of pieces currently buffered (does not trigger refill).
  int buffered() const { return static_cast<int>(buffer_.size()); }

  // --- Snapshot / restore (for Game's undo stack) -----------------------

  struct Snapshot {
    std::deque<PieceType> buffer;
    std::unique_ptr<PieceSource> source;
    int draws;
  };
  Snapshot snapshot() const;
  void restore(const Snapshot &snap);

  // Install a callback invoked once per piece appended to the buffer from
  // the source (i.e. on prefetch refill). Not called by insert/replace or
  // restore — those reshape existing buffer contents rather than draw new
  // pieces from the source.
  void set_on_added(std::function<void(PieceType)> cb) {
    on_added_ = std::move(cb);
  }

private:
  // Refill from source until `buffer_.size() >= n` or source is exhausted.
  void prefetch(int n);

  std::deque<PieceType> buffer_;
  std::unique_ptr<PieceSource> source_;
  int draws_ = 0;
  std::function<void(PieceType)> on_added_;
};
