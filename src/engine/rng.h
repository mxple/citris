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
  std::vector<PieceType> preview(int count) {
    while (queue_.size() < count) {
      refill();
    }
    return {queue_.begin(), queue_.begin() + count};
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

class TrueRandomizer : public BagRandomizer {
public:
  using BagRandomizer::BagRandomizer;

private:
  void refill() override {
    queue_.push_back(static_cast<PieceType>(rng_() % kPieceTypeN));
  }
};
