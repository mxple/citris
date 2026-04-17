#include "piece_queue.h"
#include "hold_shuffle.h"
#include <vector>

PieceQueue::PieceQueue(std::unique_ptr<PieceSource> source,
                       std::vector<PieceType> prefix)
    : source_(std::move(source)) {
  for (PieceType t : prefix)
    buffer_.push_back(t);
}

void PieceQueue::prefetch(int n) {
  while (static_cast<int>(buffer_.size()) < n) {
    auto next = source_->next();
    if (!next)
      return;
    buffer_.push_back(*next);
  }
}

std::optional<PieceType> PieceQueue::pop() {
  if (buffer_.empty())
    prefetch(1);
  if (buffer_.empty())
    return std::nullopt;
  PieceType t = buffer_.front();
  buffer_.pop_front();
  ++draws_;
  return t;
}

std::vector<PieceType> PieceQueue::peek(int n) {
  prefetch(n);
  int k = std::min(n, static_cast<int>(buffer_.size()));
  return std::vector<PieceType>(buffer_.begin(), buffer_.begin() + k);
}

void PieceQueue::shuffle(ShufflePolicy policy, std::mt19937 &rng) {
  // Operates only on the currently buffered pieces. Caller is responsible
  // for prefetching (via peek()) if they want to shuffle further ahead.
  std::vector<PieceType> snapshot(buffer_.begin(), buffer_.end());
  std::vector<PieceType> shuffled;
  switch (policy) {
  case ShufflePolicy::HoldEquivalent:
    shuffled = shuffle_hold_equivalent(snapshot, rng);
    break;
  }
  buffer_.assign(shuffled.begin(), shuffled.end());
}

void PieceQueue::insert(int pos, PieceType t) {
  buffer_.insert(buffer_.begin() + pos, t);
}

void PieceQueue::replace(int pos, PieceType t) { buffer_[pos] = t; }

void PieceQueue::erase(int pos) { buffer_.erase(buffer_.begin() + pos); }

PieceQueue::Snapshot PieceQueue::snapshot() const {
  return Snapshot{buffer_, source_->clone(), draws_};
}

void PieceQueue::restore(const Snapshot &snap) {
  buffer_ = snap.buffer;
  source_ = snap.source->clone();
  draws_ = snap.draws;
}
