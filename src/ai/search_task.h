#pragma once

#include "ai.h"
#include <atomic>
#include <thread>
#include <vector>

class SearchTask {
public:
  SearchTask(AI &ai, std::vector<PieceType> queue)
      : ai_(&ai), queue_(std::move(queue)) {
    thread_ = std::thread([this] {
      ai_->run_search(queue_, cancel_);
      done_.store(true, std::memory_order_release);
    });
  }
  ~SearchTask() { cancel(); }

  SearchTask(const SearchTask &) = delete;
  SearchTask &operator=(const SearchTask &) = delete;

  bool ready() const { return done_.load(std::memory_order_acquire); }

  void cancel() {
    cancel_.store(true, std::memory_order_relaxed);
    if (thread_.joinable())
      thread_.join();
  }

  void join() {
    if (thread_.joinable())
      thread_.join();
  }

private:
  AI *ai_;
  std::vector<PieceType> queue_;
  std::thread thread_;
  std::atomic<bool> done_{false};
  std::atomic<bool> cancel_{false};
};
