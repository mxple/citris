#pragma once

#include "piece.h"
#include <algorithm>
#include <deque>
#include <random>

class BagRandomizer {
public:
  explicit BagRandomizer(unsigned seed = std::random_device{}()) : rng_(seed) {}
  virtual ~BagRandomizer() = default;

  // Get the next piece from the bag. Refills bag when empty.
  PieceType next() {
    if (queue_.size() < kMinPieces) {
      refill();
    }
    auto result = queue_.front();
    queue_.pop_front();
    draws_++;
    return result;
  }

  // Peek at upcoming pieces (for preview queue display).
  // Returns up to count pieces without consuming them.
  // May return fewer than count if queue is finite and nearly exhausted.
  std::vector<PieceType> preview(int count) {
    while (static_cast<int>(queue_.size()) < count) {
      auto old_size = queue_.size();
      refill();
      if (queue_.size() == old_size)
        break; // finite queue — refill can't produce more
    }
    auto n = std::min(static_cast<int>(queue_.size()), count);
    return {queue_.begin(), queue_.begin() + n};
  }

  int draws() const { return draws_; }

  struct BagSnapshot {
    std::mt19937 rng;
    std::deque<PieceType> queue;
    int draws;
  };

  BagSnapshot snapshot() const { return {rng_, queue_, draws_}; }
  void restore(const BagSnapshot &snap) {
    rng_ = snap.rng;
    queue_ = snap.queue;
    draws_ = snap.draws;
  }

protected:
  static constexpr int kMinPieces = 7;

  virtual void refill() = 0;

  std::mt19937 rng_;
  std::deque<PieceType> queue_;
  int draws_ = 0;
};

class SevenBagRandomizer : public BagRandomizer {
public:
  using BagRandomizer::BagRandomizer;

private:
  void refill() override {
    std::array<PieceType, kPieceTypeN> bag = {
        PieceType::I, PieceType::O, PieceType::T, PieceType::S,
        PieceType::Z, PieceType::J, PieceType::L};

    std::shuffle(bag.begin(), bag.end(), rng_);

    for (auto piece : bag) {
      queue_.push_back(piece);
    }
  }
};

// Plays a fixed sequence of pieces, then stops (no refill).
// Used by training modes for finite queues.
class FixedQueueRandomizer : public BagRandomizer {
public:
  explicit FixedQueueRandomizer(std::vector<PieceType> pieces,
                                unsigned seed = 0)
      : BagRandomizer(seed) {
    for (auto p : pieces)
      queue_.push_back(p);
  }

private:
  void refill() override {} // intentionally empty — finite queue
};

// Fixed prefix followed by standard 7-bag. Used for training modes
// with an initial queue that transitions into random play.
class PrefixedBagRandomizer : public SevenBagRandomizer {
public:
  PrefixedBagRandomizer(std::vector<PieceType> prefix, unsigned seed)
      : SevenBagRandomizer(seed) {
    for (auto it = prefix.rbegin(); it != prefix.rend(); ++it)
      queue_.push_front(*it);
  }
};

class TrueRandomizer : public BagRandomizer {
public:
  using BagRandomizer::BagRandomizer;

private:
  void refill() override {
    queue_.push_back(static_cast<PieceType>(rng_() % kPieceTypeN));
  }
};
