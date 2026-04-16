#include "beam_search.h"
#include "ai.h"
#include <atomic>
#include <thread>

struct BeamTask::Impl {
  AI ai;
  std::vector<PieceType> queue;
  std::thread thread;
  std::atomic<bool> done{false};
  std::atomic<bool> cancel{false};
};

BeamTask::BeamTask(BeamInput input, BeamConfig cfg)
    : impl_(std::make_unique<Impl>()) {
  auto &impl = *impl_;

  AIConfig ai_cfg;
  ai_cfg.beam_width = cfg.width;
  ai_cfg.max_depth =
      cfg.depth == 0 ? static_cast<int>(input.queue.size()) : cfg.depth;
  ai_cfg.sonic_only = cfg.sonic_only;
  ai_cfg.extend_queue_7bag = cfg.extend_7bag;

  impl.ai.set_evaluator(std::move(cfg.evaluator));
  impl.ai.set_config(ai_cfg);

  SearchState root;
  root.board = input.board;
  root.hold = input.hold;
  root.hold_available = input.hold_available;
  root.bag_draws = input.bag_draws;
  impl.ai.reset(root);

  impl.queue = std::move(input.queue);

  impl.thread = std::thread([this] {
    impl_->ai.run_search(impl_->queue, impl_->cancel);
    impl_->done.store(true, std::memory_order_release);
  });
}

BeamTask::~BeamTask() { cancel(); }

bool BeamTask::ready() const {
  return impl_->done.load(std::memory_order_acquire);
}

BeamResult BeamTask::get() {
  if (impl_->thread.joinable())
    impl_->thread.join();

  const auto &r = impl_->ai.result();
  BeamResult result;
  result.pv = r.pv;
  result.score = r.score;
  result.root_scores = r.root_scores;
  result.hold_used = r.hold_used;
  result.depth = impl_->ai.depth();
  return result;
}

void BeamTask::cancel() {
  impl_->cancel.store(true, std::memory_order_relaxed);
  if (impl_->thread.joinable())
    impl_->thread.join();
}

std::unique_ptr<BeamTask> start_beam_search(BeamInput input, BeamConfig cfg) {
  return std::unique_ptr<BeamTask>(
      new BeamTask(std::move(input), std::move(cfg)));
}
