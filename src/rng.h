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

protected:
  static constexpr int kMinPieces = 7;

  virtual void refill() = 0;

  std::mt19937 rng_;
  std::deque<PieceType> queue_;
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
