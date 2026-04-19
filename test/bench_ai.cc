// Headless AI search benchmark.
// Build: cmake --build build-prof --target bench_ai
// Run:   ./build-prof/bench_ai [iters=20] [beam_width=800] [max_depth=14]
//
// Reports per-scenario wall-clock stats (min, p10, p50, p90, max, mean) plus a
// signature (best move + score + reached depth) so optimization passes can
// verify equivalent search behavior.

#include "ai/ai.h"
#include "ai/board_bitset.h"
#include "ai/eval/atk.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <numeric>
#include <optional>
#include <random>
#include <string>
#include <vector>

using clk = std::chrono::steady_clock;
using fms = std::chrono::duration<double, std::milli>;

struct Scenario {
  std::string name;
  BoardBitset board;
  std::vector<PieceType> queue;
  std::optional<PieceType> hold;
};

static std::vector<PieceType> gen_queue(uint32_t seed, int n) {
  std::array<PieceType, 7> bag = {
      PieceType::I, PieceType::O, PieceType::T, PieceType::S,
      PieceType::Z, PieceType::J, PieceType::L,
  };
  std::mt19937 rng(seed);
  std::vector<PieceType> q;
  while ((int)q.size() < n) {
    auto b = bag;
    std::shuffle(b.begin(), b.end(), rng);
    for (auto p : b) q.push_back(p);
  }
  q.resize(n);
  return q;
}

// Cheese garbage: h rows, each missing one hole at a deterministic column.
static BoardBitset cheese_board(int h, uint32_t seed) {
  BoardBitset b{};
  std::mt19937 rng(seed);
  for (int y = 0; y < h; ++y) {
    int hole = static_cast<int>(rng() % 10);
    for (int x = 0; x < 10; ++x) {
      if (x != hole) {
        b.rows[y] |= uint16_t(1) << x;
        b.cols[x] |= uint64_t(1) << y;
      }
    }
  }
  return b;
}

static std::vector<Scenario> build_scenarios() {
  return {
      {"empty", BoardBitset{}, gen_queue(0xC17150, 14), std::nullopt},
      {"cheese-5", cheese_board(5, 0xCEE5E), gen_queue(0xC17151, 14),
       std::nullopt},
      {"cheese-10", cheese_board(10, 0xCEE5E), gen_queue(0xC17152, 14),
       std::nullopt},
  };
}

struct RunResult {
  double ms;
  Placement best;
  float score;
  int depth;
};

static RunResult run_one(AI &ai, const Scenario &s) {
  SearchState root;
  root.board = s.board;
  root.hold = s.hold;
  root.hold_available = true;
  ai.reset(root);
  std::atomic<bool> cancel{false};
  auto t0 = clk::now();
  ai.run_search(s.queue, cancel);
  auto t1 = clk::now();
  auto r = ai.result();
  return {fms(t1 - t0).count(), r.best_move, r.score, ai.depth()};
}

struct Stats {
  double min, p10, p50, p90, max, mean;
};
static Stats summarize(std::vector<double> v) {
  std::sort(v.begin(), v.end());
  size_t n = v.size();
  return {
      v.front(),
      v[n / 10],
      v[n / 2],
      v[(n * 9) / 10],
      v.back(),
      std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(n),
  };
}

static char piece_char(PieceType t) {
  static const char *s = "IOTSZJL";
  int i = static_cast<int>(t);
  return (i >= 0 && i < 7) ? s[i] : '?';
}

int main(int argc, char **argv) {
  int iters = (argc > 1) ? std::atoi(argv[1]) : 20;
  int beam_width = (argc > 2) ? std::atoi(argv[2]) : 800;
  int max_depth = (argc > 3) ? std::atoi(argv[3]) : 14;

  AI ai;
  ai.set_evaluator(std::make_unique<AtkEvaluator>());
  AIConfig cfg;
  cfg.beam_width = beam_width;
  cfg.max_depth = max_depth;
  ai.set_config(cfg);

  auto scenarios = build_scenarios();

  std::printf("Warming up...\n");
  for (const auto &s : scenarios)
    (void)run_one(ai, s);

  std::printf("\nBenchmark: beam_width=%d max_depth=%d iters=%d\n\n",
              beam_width, max_depth, iters);
  std::printf("%-12s %8s %8s %8s %8s %8s %8s   %s\n", "scenario", "min", "p10",
              "p50", "p90", "max", "mean", "signature");
  std::printf("%-12s %8s %8s %8s %8s %8s %8s   %s\n", "--------", "---", "---",
              "---", "---", "---", "----", "---------");

  double total_p50 = 0;
  for (const auto &s : scenarios) {
    std::vector<double> times;
    RunResult last{};
    for (int i = 0; i < iters; ++i) {
      last = run_one(ai, s);
      times.push_back(last.ms);
    }
    auto st = summarize(times);
    char sig[96];
    std::snprintf(sig, sizeof(sig), "d=%d %c%c@(%d,%d) score=%.2f", last.depth,
                  piece_char(last.best.type),
                  "NESW"[static_cast<int>(last.best.rotation)], last.best.x,
                  last.best.y, last.score);
    std::printf("%-12s %8.2f %8.2f %8.2f %8.2f %8.2f %8.2f   %s\n",
                s.name.c_str(), st.min, st.p10, st.p50, st.p90, st.max, st.mean,
                sig);
    total_p50 += st.p50;
  }
  std::printf("\nSum of p50 across scenarios: %.2f ms\n", total_p50);
  return 0;
}
