#pragma once

// Subprocess-backed TbpBot. Launches an external executable (e.g. cold-clear,
// blockfish, or our own citris-tbp-bot) and communicates over stdin/stdout
// pipes using the TBP JSON codec.
//
// Threading:
//   - A dedicated reader thread drains the child's stdout into an inbox.
//   - Writes happen on the caller thread (typically the main loop).
//   - The inbox is mutex-guarded; a CV wakes callers waiting for a response.
//
// Lifecycle:
//   - Constructor: fork+exec the binary, spin up the reader thread, and
//     block briefly for the child's info() message (with a configurable
//     timeout — default 2s).
//   - Destructor: quit(), close stdin, join reader, waitpid. If the child
//     doesn't exit within a grace period we SIGKILL it.

#include "tbp/bot.h"
#include "tbp/types.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <sys/types.h>
#include <thread>
#include <variant>
#include <vector>

namespace tbp {

struct ExternalBotConfig {
  std::string exe_path;                // absolute or PATH-resolvable
  std::vector<std::string> args;       // extra argv (argv[0] is exe_path)
  int info_timeout_ms = 2000;          // how long to wait for initial info
  int response_timeout_ms = 5000;      // for rules/suggest replies
  int shutdown_grace_ms = 1000;        // after quit() before SIGKILL
};

class ExternalTbpBot : public TbpBot {
public:
  explicit ExternalTbpBot(ExternalBotConfig cfg);
  ~ExternalTbpBot() override;

  Info info() const override;
  std::variant<Ready, Error> rules(const Rules &) override;
  void start(const Start &) override;
  std::optional<Suggestion> poll_suggestion() override;
  void play(const Play &) override;
  void new_piece(const NewPiece &) override;
  void stop() override;
  void quit() override;

  bool alive() const { return !reader_done_.load(); }

private:
  void spawn();
  void reader_main();
  void write_msg(const Message &m);
  // Block until a message matching the predicate is found (popped and
  // returned) or the timeout elapses.
  std::optional<Message>
  wait_for(const std::function<bool(const Message &)> &match, int timeout_ms);
  void cleanup();

  ExternalBotConfig cfg_;

  pid_t child_pid_ = -1;
  int in_fd_ = -1;  // parent -> child stdin
  int out_fd_ = -1; // child stdout -> parent

  std::thread reader_;
  std::atomic<bool> reader_done_{true};
  std::atomic<bool> stopping_{false};

  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::deque<Message> inbox_;

  Info cached_info_{};
  bool info_ready_ = false;
};

} // namespace tbp
