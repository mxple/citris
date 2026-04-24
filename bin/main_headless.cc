// citris-headless — AI-vs-AI match runner.
//
// Spawns two TbpBots (internal or external subprocess), runs N matches with
// shared per-match seeds, and prints a TSV line per match.
//
// Usage:
//   citris-headless --bot1 <internal|path/to/exe>
//                   --bot2 <internal|path/to/exe>
//                   --games N
//                   --seed S
//                   [--max-ticks T]
//                   [--think-time-ms X]

#include "command.h"
#include "controller/tbp_controller.h"
#include "engine/game.h"
#include "log.h"
#include "match.h"
#include "presets/freeplay.h"
#include "settings.h"
#include "tbp/bot.h"
#include "tbp/external_bot.h"
#include "tbp/internal_bot.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

namespace {

struct Args {
  std::string bot1 = "internal";
  std::string bot2 = "internal";
  int games = 1;
  unsigned seed = 0;
  int max_ticks = 60000; // safety cap (~60 s wall at 1 ms/tick)
  int think_time_ms = 0;
};

void usage(const char *prog) {
  std::fprintf(stderr,
               "Usage: %s --bot1 <internal|path> --bot2 <internal|path> "
               "--games N --seed S [--max-ticks T] [--think-time-ms X]\n",
               prog);
}

std::optional<Args> parse_args(int argc, char **argv) {
  Args a;
  for (int i = 1; i < argc; ++i) {
    auto eq = [&](const char *flag) { return std::strcmp(argv[i], flag) == 0; };
    if ((eq("--bot1") || eq("--bot2") || eq("--games") || eq("--seed") ||
         eq("--max-ticks") || eq("--think-time-ms")) &&
        i + 1 >= argc) {
      std::fprintf(stderr, "missing value for %s\n", argv[i]);
      return std::nullopt;
    }
    if (eq("--bot1"))
      a.bot1 = argv[++i];
    else if (eq("--bot2"))
      a.bot2 = argv[++i];
    else if (eq("--games"))
      a.games = std::atoi(argv[++i]);
    else if (eq("--seed"))
      a.seed = static_cast<unsigned>(std::atoi(argv[++i]));
    else if (eq("--max-ticks"))
      a.max_ticks = std::atoi(argv[++i]);
    else if (eq("--think-time-ms"))
      a.think_time_ms = std::atoi(argv[++i]);
    else if (eq("-h") || eq("--help")) {
      usage(argv[0]);
      return std::nullopt;
    } else {
      std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
      return std::nullopt;
    }
  }
  if (a.games <= 0) {
    std::fprintf(stderr, "--games must be > 0\n");
    return std::nullopt;
  }
  return a;
}

std::unique_ptr<tbp::TbpBot> make_bot(const std::string &spec) {
  if (spec == "internal")
    return std::make_unique<tbp::InternalTbpBot>();
  tbp::ExternalBotConfig cfg;
  cfg.exe_path = spec;
  return std::make_unique<tbp::ExternalTbpBot>(std::move(cfg));
}

// Drain events from one game; notify its controllers and let the mode react
// on PieceLocked. Mirrors GameManager::process_engine_events /
// process_p2_events minus the Stats / AIState / undo plumbing (none of which
// the headless runner needs).
void drain_and_notify(Game &g, GameMode &mode,
                      std::vector<std::unique_ptr<IController>> &ctrls,
                      CommandBuffer &rule_cmds, TimePoint now) {
  for (auto &ev : g.drain_events()) {
    auto st = g.state();
    for (auto &ctrl : ctrls)
      ctrl->notify(ev, now, st);
    if (auto *pl = std::get_if<eng::PieceLocked>(&ev))
      mode.on_piece_locked(*pl, g.state(), rule_cmds);
  }
  auto post_state = g.state();
  for (auto &ctrl : ctrls)
    ctrl->post_hook(now, post_state);
  mode.on_tick(now, g.state(), rule_cmds);
}

struct MatchResult {
  int winner = -1; // -1 = inconclusive (tick cap)
  int p1_lines = 0, p2_lines = 0;
  int p1_attack = 0, p2_attack = 0;
  int ticks = 0;
  long wall_ms = 0;
};

MatchResult run_match(const Args &args, unsigned seed1, unsigned seed2,
                      const Settings &settings) {
  // Each match gets its own modes + controllers. We rebuild the bots per match.
  auto mode1 = std::make_unique<FreeplayMode>(settings);
  auto mode2 = std::make_unique<FreeplayMode>(settings);

  Board b1, b2;
  mode1->setup_board(b1);
  mode2->setup_board(b2);
  // Separate per-player seeds. With the same seed on both games, identical
  // bots would deterministically mirror each other and the match would
  // stalemate forever (attacks cancel via LIFO, nobody tops out).
  auto game1 = std::make_unique<Game>(*mode1, std::move(b1), seed1);
  auto game2 = std::make_unique<Game>(*mode2, std::move(b2), seed2);
  game1->drain_events();
  game2->drain_events();

  auto bot1 = make_bot(args.bot1);
  auto bot2 = make_bot(args.bot2);

  std::vector<std::unique_ptr<IController>> ctrls1;
  std::vector<std::unique_ptr<IController>> ctrls2;
  ctrls1.push_back(
      std::make_unique<TbpController>(std::move(bot1), args.think_time_ms));
  ctrls2.push_back(
      std::make_unique<TbpController>(std::move(bot2), args.think_time_ms));

  MatchState ms;
  std::mt19937 gap_rng(seed1 ^ (seed2 << 1));

  CommandBuffer cmds1, cmds2;
  auto wall_start = std::chrono::steady_clock::now();

  int tick = 0;
  for (; tick < args.max_ticks && !ms.over(); ++tick) {
    auto now = SdlClock::now();

    auto s1 = game1->state();
    auto s2 = game2->state();
    for (auto &c : ctrls1)
      c->tick(now, s1, cmds1);
    for (auto &c : ctrls2)
      c->tick(now, s2, cmds2);

    game1->apply(cmds1);
    game1->tick(now);
    cmds1.clear();
    game2->apply(cmds2);
    game2->tick(now);
    cmds2.clear();

    CommandBuffer rule_cmds1, rule_cmds2;
    drain_and_notify(*game1, *mode1, ctrls1, rule_cmds1, now);
    drain_and_notify(*game2, *mode2, ctrls2, rule_cmds2, now);

    route_garbage_between(*game1, *game2, rule_cmds1, rule_cmds2, gap_rng);
    update_match_state(ms, *game1, game2.get());

    if (!rule_cmds1.empty()) {
      game1->apply(rule_cmds1);
      CommandBuffer discard;
      drain_and_notify(*game1, *mode1, ctrls1, discard, now);
    }
    if (!rule_cmds2.empty()) {
      game2->apply(rule_cmds2);
      CommandBuffer discard;
      drain_and_notify(*game2, *mode2, ctrls2, discard, now);
    }

    // Give the bot search threads a slice before polling again.
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  auto wall_end = std::chrono::steady_clock::now();

  MatchResult r;
  r.winner = ms.winner.value_or(-1);
  r.p1_lines = game1->state().lines_cleared;
  r.p2_lines = game2->state().lines_cleared;
  r.p1_attack = game1->state().total_attack;
  r.p2_attack = game2->state().total_attack;
  r.ticks = tick;
  r.wall_ms = std::chrono::duration_cast<std::chrono::milliseconds>(wall_end -
                                                                    wall_start)
                  .count();
  return r;
}

} // namespace

int main(int argc, char **argv) {
  Log::init();
  spdlog::set_level(spdlog::level::err);

  auto parsed = parse_args(argc, argv);
  if (!parsed)
    return 2;
  Args args = *parsed;

  Settings settings(argv[0]);

  std::fprintf(stderr, "citris-headless: bot1=%s bot2=%s games=%d seed=%u\n",
               args.bot1.c_str(), args.bot2.c_str(), args.games, args.seed);

  // Header
  std::printf(
      "match\twinner\tp1_lines\tp2_lines\tp1_atk\tp2_atk\tticks\twall_ms\n");
  std::fflush(stdout);

  for (int i = 0; i < args.games; ++i) {
    unsigned seed1 = args.seed + 2u * static_cast<unsigned>(i);
    unsigned seed2 = seed1 + 1u;
    auto r = run_match(args, seed1, seed2, settings);
    std::printf("%d\t%d\t%d\t%d\t%d\t%d\t%d\t%ld\n", i, r.winner, r.p1_lines,
                r.p2_lines, r.p1_attack, r.p2_attack, r.ticks, r.wall_ms);
    std::fflush(stdout);
  }

  return 0;
}
