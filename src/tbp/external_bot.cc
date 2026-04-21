#include "tbp/external_bot.h"

#include "log.h"
#include "tbp/codec.h"
#include "tbp/conversions.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#ifndef _WIN32
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace tbp {

#ifdef _WIN32
// Windows stub: external TBP bots rely on fork/exec/pipe, which don't exist on
// Windows. Porting to CreateProcess + anonymous pipes isn't done yet, so every
// method here is a no-op and rules() returns an Error. The class still exists
// and links because versus.h and tbp_controller.cc reference it directly.

ExternalTbpBot::ExternalTbpBot(ExternalBotConfig cfg) : cfg_(std::move(cfg)) {}
ExternalTbpBot::~ExternalTbpBot() = default;

Info ExternalTbpBot::info() const { return {}; }

std::variant<Ready, Error> ExternalTbpBot::rules(const Rules &) {
  return Error{"external TBP bots not supported on Windows"};
}
void ExternalTbpBot::start(const Start &) {}
std::optional<Suggestion> ExternalTbpBot::poll_suggestion() {
  return std::nullopt;
}
void ExternalTbpBot::play(const Play &) {}
void ExternalTbpBot::new_piece(const NewPiece &) {}
void ExternalTbpBot::request_suggestion() {}
void ExternalTbpBot::stop() {}
void ExternalTbpBot::quit() {}
std::vector<Placement> ExternalTbpBot::last_plan() const { return {}; }

void ExternalTbpBot::spawn() {}
void ExternalTbpBot::reader_main() {}
void ExternalTbpBot::write_msg(const Message &) {}
std::optional<Message>
ExternalTbpBot::wait_for(const std::function<bool(const Message &)> &, int) {
  return std::nullopt;
}
void ExternalTbpBot::cleanup() {}
} // namespace tbp
#else

namespace {

// Read up to 4096 bytes at a time into a carry buffer, split on '\n', and
// invoke the callback for each complete line. Returns false on EOF or error.
template <typename F>
bool read_line_loop(int fd, std::string &carry, F &&on_line) {
  char buf[4096];
  ssize_t n;
  while ((n = ::read(fd, buf, sizeof(buf))) > 0) {
    carry.append(buf, buf + n);
    size_t pos;
    while ((pos = carry.find('\n')) != std::string::npos) {
      std::string line = carry.substr(0, pos);
      carry.erase(0, pos + 1);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (!on_line(std::move(line))) return true; // stop requested
    }
  }
  return false; // EOF (n==0) or error (n<0, e.g. EINTR not handled here)
}

} // namespace

ExternalTbpBot::ExternalTbpBot(ExternalBotConfig cfg) : cfg_(std::move(cfg)) {
  spawn();
}

ExternalTbpBot::~ExternalTbpBot() { cleanup(); }

Info ExternalTbpBot::info() const {
  std::lock_guard<std::mutex> lk(mu_);
  return cached_info_;
}

void ExternalTbpBot::spawn() {
  int in_pipe[2];  // parent[1] -> child[0]
  int out_pipe[2]; // child[1]  -> parent[0]
  if (::pipe(in_pipe) < 0 || ::pipe(out_pipe) < 0) {
    std::perror("pipe");
    return;
  }

  pid_t pid = ::fork();
  if (pid < 0) {
    std::perror("fork");
    return;
  }

  if (pid == 0) {
    // Child.
    ::dup2(in_pipe[0], STDIN_FILENO);
    ::dup2(out_pipe[1], STDOUT_FILENO);
    ::close(in_pipe[0]);
    ::close(in_pipe[1]);
    ::close(out_pipe[0]);
    ::close(out_pipe[1]);

    std::vector<std::string> argv_store;
    argv_store.push_back(cfg_.exe_path);
    for (auto &a : cfg_.args) argv_store.push_back(a);
    std::vector<char *> argv;
    for (auto &s : argv_store) argv.push_back(s.data());
    argv.push_back(nullptr);
    ::execvp(cfg_.exe_path.c_str(), argv.data());
    std::fprintf(stderr, "execvp %s failed: %s\n", cfg_.exe_path.c_str(),
                 std::strerror(errno));
    _exit(127);
  }

  // Parent.
  ::close(in_pipe[0]);
  ::close(out_pipe[1]);
  in_fd_ = in_pipe[1];
  out_fd_ = out_pipe[0];
  child_pid_ = pid;
  reader_done_.store(false);
  reader_ = std::thread(&ExternalTbpBot::reader_main, this);

  // Wait up to info_timeout_ms for the child's info message.
  auto info_msg = wait_for(
      [](const Message &m) { return std::holds_alternative<Info>(m); },
      cfg_.info_timeout_ms);
  if (info_msg) {
    std::lock_guard<std::mutex> lk(mu_);
    cached_info_ = std::get<Info>(*info_msg);
    info_ready_ = true;
  }
}

void ExternalTbpBot::reader_main() {
  std::string carry;
  read_line_loop(out_fd_, carry, [&](std::string &&line) {
    if (line.empty()) return true;
    TBP_TRACE("< {}", line);
    auto msg = parse(line);
    if (!msg) return true; // unknown type: ignore

    // Stale-suggestion guard: a Suggestion whose ordinal is <=
    // stale_cutoff_ belongs to a pre-reset / pre-stop session and must be
    // dropped before it pollutes the fresh inbox. See external_bot.h.
    if (std::holds_alternative<Suggestion>(*msg)) {
      int my_idx = suggestions_received_.fetch_add(
                       1, std::memory_order_relaxed) +
                   1;
      std::lock_guard<std::mutex> lk(mu_);
      if (my_idx <= stale_cutoff_) return true; // stale — drop silently
      inbox_.push_back(std::move(*msg));
    } else {
      std::lock_guard<std::mutex> lk(mu_);
      inbox_.push_back(std::move(*msg));
    }
    cv_.notify_all();
    return true;
  });
  reader_done_.store(true);
  cv_.notify_all();
}

void ExternalTbpBot::write_msg(const Message &m) {
  if (in_fd_ < 0) return;
  auto line = serialize(m);
  TBP_TRACE("> {}", line);
  line.push_back('\n');
  const char *p = line.data();
  size_t remaining = line.size();
  while (remaining > 0) {
    ssize_t n = ::write(in_fd_, p, remaining);
    if (n < 0) {
      if (errno == EINTR) continue;
      break; // child likely dead
    }
    p += n;
    remaining -= static_cast<size_t>(n);
  }
  // Track Suggest ordinals so the reader can tell fresh responses from
  // pre-reset leftovers. Increment AFTER the write so a Suggest that failed
  // to reach the child isn't counted against the cutoff (on failure we
  // broke out of the loop and no response will come back anyway).
  if (std::holds_alternative<Suggest>(m))
    suggests_sent_.fetch_add(1, std::memory_order_relaxed);
}

std::optional<Message>
ExternalTbpBot::wait_for(const std::function<bool(const Message &)> &match,
                         int timeout_ms) {
  std::unique_lock<std::mutex> lk(mu_);
  auto deadline = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(timeout_ms);
  while (true) {
    for (auto it = inbox_.begin(); it != inbox_.end(); ++it) {
      if (match(*it)) {
        Message m = std::move(*it);
        inbox_.erase(it);
        return m;
      }
    }
    if (reader_done_.load()) return std::nullopt;
    if (cv_.wait_until(lk, deadline) == std::cv_status::timeout) {
      // One last scan in case of spurious ordering.
      for (auto it = inbox_.begin(); it != inbox_.end(); ++it) {
        if (match(*it)) {
          Message m = std::move(*it);
          inbox_.erase(it);
          return m;
        }
      }
      return std::nullopt;
    }
  }
}

std::variant<Ready, Error> ExternalTbpBot::rules(const Rules &r) {
  write_msg(r);
  auto resp = wait_for(
      [](const Message &m) {
        return std::holds_alternative<Ready>(m) ||
               std::holds_alternative<Error>(m);
      },
      cfg_.response_timeout_ms);
  if (!resp) return Error{"timeout"};
  if (auto *rd = std::get_if<Ready>(&*resp)) return *rd;
  if (auto *er = std::get_if<Error>(&*resp)) return *er;
  return Error{"unexpected_response"};
}

void ExternalTbpBot::start(const Start &s) {
  // Per TBP spec, start() can be sent to begin a new game. If the bot was
  // already calculating (previous game), we must end that session first with
  // stop; some external bots treat unsolicited start as an error. Safe to
  // send even on first call (bot should no-op on stop-when-idle).
  {
    std::lock_guard<std::mutex> lk(mu_);
    // Any Suggestion whose ordinal is <= this cutoff is from the previous
    // session and must be dropped when it lands. Snapshotting *after*
    // write(Stop) is fine — suggests_sent_ counts writes to the wire and
    // Stop doesn't affect it.
    stale_cutoff_ = suggests_sent_.load(std::memory_order_relaxed);
    for (auto it = inbox_.begin(); it != inbox_.end();) {
      if (std::holds_alternative<Suggestion>(*it))
        it = inbox_.erase(it);
      else
        ++it;
    }
    last_plan_moves_.clear();
  }
  write_msg(s);
  // Suggest is intentionally NOT auto-written here. TbpController emits it
  // explicitly at the turn boundary, after new_piece(s) have been relayed,
  // so the bot computes on the full queue rather than the one it had when
  // start arrived.
}

std::optional<Suggestion> ExternalTbpBot::poll_suggestion() {
  std::lock_guard<std::mutex> lk(mu_);
  for (auto it = inbox_.begin(); it != inbox_.end(); ++it) {
    if (auto *s = std::get_if<Suggestion>(&*it)) {
      Suggestion sug = std::move(*s);
      inbox_.erase(it);
      // Cache the moves list for plan-overlay rendering.
      last_plan_moves_ = sug.moves;
      return sug;
    }
  }
  return std::nullopt;
}

void ExternalTbpBot::play(const Play &p) {
  write_msg(p);
  // Advance the plan cache in lockstep: if the played piece matches the
  // head of the cached moves list, pop the front (PV-like semantics);
  // otherwise clear, since the remaining alternatives were for the
  // superseded piece. Unlike a blanket clear, this leaves multi-move
  // suggestions (PV-style external bots) still renderable, and for
  // single-move bots it empties naturally until the next poll_suggestion
  // lands fresh data. The controller's type-match guard protects the
  // short window between pop and refill from rendering stale placements.
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (!last_plan_moves_.empty() &&
        last_plan_moves_.front().location.type == p.move.location.type) {
      last_plan_moves_.erase(last_plan_moves_.begin());
    } else {
      last_plan_moves_.clear();
    }
  }
  // Suggest deferred — TbpController issues it via request_suggestion()
  // once it has also relayed any new_piece() updates for this turn.
}

void ExternalTbpBot::new_piece(const NewPiece &n) { write_msg(n); }

void ExternalTbpBot::request_suggestion() { write_msg(Suggest{}); }

void ExternalTbpBot::stop() {
  // stopping_ is reserved for quit() to prevent double-sending Quit during
  // destruction; stop() may legitimately be called multiple times (once per
  // reset / game-over) and must always emit a Stop message.
  write_msg(Stop{});
  std::lock_guard<std::mutex> lk(mu_);
  // Mirror start(): any in-flight Suggestion is from the session we're
  // stopping and should be dropped on arrival.
  stale_cutoff_ = suggests_sent_.load(std::memory_order_relaxed);
  for (auto it = inbox_.begin(); it != inbox_.end();) {
    if (std::holds_alternative<Suggestion>(*it))
      it = inbox_.erase(it);
    else
      ++it;
  }
  last_plan_moves_.clear();
}

void ExternalTbpBot::quit() {
  if (stopping_.exchange(true)) return;
  write_msg(Quit{});
}

void ExternalTbpBot::cleanup() {
  if (!stopping_.exchange(true)) {
    // Best-effort quit. If write fails, we'll still close the pipe below.
    write_msg(Quit{});
  }
  if (in_fd_ >= 0) {
    ::close(in_fd_);
    in_fd_ = -1;
  }
  if (reader_.joinable()) reader_.join();
  if (out_fd_ >= 0) {
    ::close(out_fd_);
    out_fd_ = -1;
  }
  if (child_pid_ > 0) {
    // Give the child a grace period to exit, then force.
    int status = 0;
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(cfg_.shutdown_grace_ms);
    while (std::chrono::steady_clock::now() < deadline) {
      pid_t r = ::waitpid(child_pid_, &status, WNOHANG);
      if (r == child_pid_ || r < 0) {
        child_pid_ = -1;
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    ::kill(child_pid_, SIGKILL);
    ::waitpid(child_pid_, &status, 0);
    child_pid_ = -1;
  }
}

std::vector<Placement> ExternalTbpBot::last_plan() const {
  std::lock_guard<std::mutex> lk(mu_);
  std::vector<Placement> out;
  out.reserve(last_plan_moves_.size());
  for (const auto &m : last_plan_moves_)
    out.push_back(location_to_placement(m.location, spin_from_str(m.spin)));
  return out;
}

} // namespace tbp
#endif // _WIN32
