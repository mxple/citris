#pragma once

#include "piece.h"
#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <random>

// PieceSource — pure piece generator. Owns its RNG and the generation
// policy (7-bag, true random, exhausted/empty). NO buffer, NO preview, NO
// dispense counter — those live in PieceQueue, which owns the source.
//
// `next()` returns nullopt iff the source is exhausted (only EmptySource
// today). `clone()` deep-copies the source for snapshot/restore use.
class PieceSource {
public:
  virtual ~PieceSource() = default;
  virtual std::optional<PieceType> next() = 0;
  virtual std::unique_ptr<PieceSource> clone() const = 0;
};

// Standard 7-bag: shuffles all 7 piece types per bag, dispenses in order.
class SevenBagSource : public PieceSource {
public:
  explicit SevenBagSource(unsigned seed = std::random_device{}())
      : rng_(seed) {}

  std::optional<PieceType> next() override {
    if (idx_ >= 7) {
      bag_ = {PieceType::I, PieceType::O, PieceType::T, PieceType::S,
              PieceType::Z, PieceType::J, PieceType::L};
      std::shuffle(bag_.begin(), bag_.end(), rng_);
      idx_ = 0;
    }
    return bag_[idx_++];
  }

  std::unique_ptr<PieceSource> clone() const override {
    return std::make_unique<SevenBagSource>(*this);
  }

private:
  std::mt19937 rng_;
  std::array<PieceType, 7> bag_{};
  int idx_ = 7; // force refill on first next()
};

// Exhausted source: always returns nullopt. Used by PieceQueue when the
// queue's contents are entirely a fixed prefix and nothing further is
// generated (puzzle modes).
class EmptySource : public PieceSource {
public:
  std::optional<PieceType> next() override { return std::nullopt; }
  std::unique_ptr<PieceSource> clone() const override {
    return std::make_unique<EmptySource>();
  }
};

// Uniform-random per piece — no bag structure. Currently unused in the
// app but useful for stress-testing.
class TrueRandomSource : public PieceSource {
public:
  explicit TrueRandomSource(unsigned seed = std::random_device{}())
      : rng_(seed) {}

  std::optional<PieceType> next() override {
    return static_cast<PieceType>(rng_() % kPieceTypeN);
  }

  std::unique_ptr<PieceSource> clone() const override {
    return std::make_unique<TrueRandomSource>(*this);
  }

private:
  std::mt19937 rng_;
};
