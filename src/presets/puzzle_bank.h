#pragma once

#include "engine/board.h"
#include "engine/piece.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <random>
#include <thread>
#include <vector>

struct GeneratedSetup {
  Board board;
  std::vector<PieceType> queue;
};

// Pre-generates puzzles in a background thread. Modes pop on restart.
class PuzzleBank {
public:
  // Generator returns nullopt to signal a bad candidate (retry).
  using Generator =
      std::function<std::optional<GeneratedSetup>(std::mt19937 &)>;

  explicit PuzzleBank(Generator gen, int pool_size = 5)
      : generator_(std::move(gen)), target_size_(pool_size) {
    worker_ = std::thread(&PuzzleBank::worker_loop, this);
  }

  ~PuzzleBank() {
    stop_.store(true, std::memory_order_release);
    cv_.notify_all();
    if (worker_.joinable())
      worker_.join();
  }

  PuzzleBank(const PuzzleBank &) = delete;
  PuzzleBank &operator=(const PuzzleBank &) = delete;

  // Non-blocking. Returns nullopt if pool is empty.
  std::optional<GeneratedSetup> pop() {
    std::lock_guard lock(mutex_);
    if (pool_.empty())
      return std::nullopt;
    auto setup = std::move(pool_.back());
    pool_.pop_back();
    cv_.notify_one(); // room in pool -> add another
    return setup;
  }

  bool has_setup() const {
    std::lock_guard lock(mutex_);
    return !pool_.empty();
  }

private:
  void worker_loop() {
    std::mt19937 rng{std::random_device{}()};

    while (!stop_.load(std::memory_order_acquire)) {
      // Wait if pool is full.
      {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [&] {
          return stop_.load(std::memory_order_acquire) ||
                 static_cast<int>(pool_.size()) < target_size_;
        });
      }

      if (stop_.load(std::memory_order_acquire))
        break;

      auto setup = generator_(rng);
      if (!setup)
        continue; // bad candidate, retry

      std::lock_guard lock(mutex_);
      if (static_cast<int>(pool_.size()) < target_size_)
        pool_.push_back(std::move(*setup));
    }
  }

  Generator generator_;
  int target_size_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<GeneratedSetup> pool_;
  std::thread worker_;
  std::atomic<bool> stop_{false};
};
