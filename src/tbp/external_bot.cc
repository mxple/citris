#include "tbp/external_bot.h"

#include "tbp/codec.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace tbp {

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
    auto msg = parse(line);
    if (!msg) return true; // unknown type: ignore
    {
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
  write_msg(s);
  // The bot will start calculating; our follow-up suggest triggers the reply.
  write_msg(Suggest{});
}

std::optional<Suggestion> ExternalTbpBot::poll_suggestion() {
  std::lock_guard<std::mutex> lk(mu_);
  for (auto it = inbox_.begin(); it != inbox_.end(); ++it) {
    if (auto *s = std::get_if<Suggestion>(&*it)) {
      Suggestion sug = std::move(*s);
      inbox_.erase(it);
      return sug;
    }
  }
  return std::nullopt;
}

void ExternalTbpBot::play(const Play &p) {
  write_msg(p);
  // Request the next suggestion eagerly so the bot is always calculating
  // between our moves.
  write_msg(Suggest{});
}

void ExternalTbpBot::new_piece(const NewPiece &n) { write_msg(n); }

void ExternalTbpBot::stop() {
  if (stopping_) return;
  write_msg(Stop{});
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

} // namespace tbp
