// citris-tbp-bot — TBP bot executable.
//
// Reads JSON-per-line on stdin, writes JSON-per-line on stdout. Stderr is
// reserved for diagnostic logging (only when --verbose is passed).
//
// Lifecycle (per TBP spec, references/tbp-spec/text/0000-mvp.md):
//   1. Emit `info` immediately.
//   2. Receive `rules`, emit `ready` or `error`.
//   3. Receive `start`, begin calculating.
//   4. Receive `suggest`, emit `suggestion` (blocks until the search has at
//      least one move, subject to a timeout).
//   5. Receive `play` / `new_piece` — advance internal state.
//   6. Loop 4–5 until `stop`.
//   7. `start` begins a new game, or `quit` exits cleanly.

#include "tbp/codec.h"
#include "tbp/internal_bot.h"
#include "tbp/types.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <variant>

namespace {

bool g_verbose = false;

void emit(const tbp::Message &m) {
  std::string line = tbp::serialize(m);
  std::fputs(line.c_str(), stdout);
  std::fputc('\n', stdout);
  std::fflush(stdout);
}

// Spin-poll poll_suggestion until a result is ready or the deadline elapses.
// Returns a suggestion with at least one move on success, or a minimal
// forfeit (empty moves list) if the search couldn't produce one in time.
tbp::Suggestion wait_for_suggestion(tbp::InternalTbpBot &bot, int timeout_ms) {
  using clk = std::chrono::steady_clock;
  auto deadline = clk::now() + std::chrono::milliseconds(timeout_ms);
  tbp::Suggestion last{};
  while (clk::now() < deadline) {
    if (auto s = bot.poll_suggestion()) {
      last = std::move(*s);
      if (!last.moves.empty()) return last;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return last; // may be empty moves list => frontend should stop / forfeit
}

} // namespace

int main(int argc, char **argv) {
  // Stdin must stay in line mode; cin/stdout tied-off so reads don't
  // needlessly flush stdout each time (we flush ourselves after emit).
  std::ios::sync_with_stdio(false);
  std::cin.tie(nullptr);

  // Parse flags.
  int suggest_timeout_ms = 2000;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--verbose") == 0 ||
        std::strcmp(argv[i], "-v") == 0) {
      g_verbose = true;
    } else if (std::strcmp(argv[i], "--timeout-ms") == 0 && i + 1 < argc) {
      suggest_timeout_ms = std::atoi(argv[++i]);
    }
  }

  tbp::InternalTbpBot bot;

  // 1. Immediately advertise identity.
  emit(bot.info());

  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.empty()) continue;
    auto parsed = tbp::parse(line);
    if (!parsed) continue; // unknown type / parse error: ignore per spec.

    std::visit(
        [&](auto &&msg) {
          using T = std::decay_t<decltype(msg)>;
          if constexpr (std::is_same_v<T, tbp::Rules>) {
            auto resp = bot.rules(msg);
            if (auto *r = std::get_if<tbp::Ready>(&resp)) emit(*r);
            else if (auto *e = std::get_if<tbp::Error>(&resp)) emit(*e);
          } else if constexpr (std::is_same_v<T, tbp::Start>) {
            bot.start(msg);
          } else if constexpr (std::is_same_v<T, tbp::Suggest>) {
            auto sug = wait_for_suggestion(bot, suggest_timeout_ms);
            emit(sug);
          } else if constexpr (std::is_same_v<T, tbp::Play>) {
            bot.play(msg);
          } else if constexpr (std::is_same_v<T, tbp::NewPiece>) {
            bot.new_piece(msg);
          } else if constexpr (std::is_same_v<T, tbp::Stop>) {
            bot.stop();
          } else if constexpr (std::is_same_v<T, tbp::Quit>) {
            bot.quit();
            std::exit(0);
          }
          // Other message kinds (Info, Ready, Error, Suggestion) are only
          // sent bot -> frontend; we ignore them on inbound per spec.
        },
        *parsed);
  }

  // EOF on stdin — treat as implicit quit.
  bot.quit();
  return 0;
}
