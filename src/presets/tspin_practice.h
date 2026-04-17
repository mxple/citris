#pragma once

#include "ai/board_bitset.h"
#include "ai/plan.h"
#include "ai/plan_overlay.h"
#include "ai/puzzle_gen.h"
#include "engine/bag.h"
#include "engine/board.h"
#include "game_mode.h"
#include "puzzle_bank.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <imgui.h>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <vector>

// TSpin-practice preset built on the 1-to-1 port of script7.js's TSD
// challenge puzzle generator. Supports two variants:
//   - TSD:     downstack cleanly + score one T-Spin Double. Reserved: {T}.
//   - TSDQuad: same, but the player must ALSO score a Tetris (quad line
//              clear) with the reserved I. Reserved: {T, I}. 4 cheese rows.
enum class TSpinVariant { TSD, TSDQuad };

class TSpinPracticeMode : public GameMode {
public:
  explicit TSpinPracticeMode(int num_pieces = 5,
                             TSpinVariant variant = TSpinVariant::TSD)
      : num_pieces_(num_pieces), slider_pieces_(num_pieces), variant_(variant) {
    bank_ = std::make_unique<PuzzleBank>(
        [this](std::mt19937 &rng) { return generate(rng); });
  }

  std::string title() const override {
    return variant_ == TSpinVariant::TSDQuad ? "TSD + Quad Practice"
                                             : "TSD Practice";
  }

  bool undo_allowed() const override { return true; }
  bool infinite_hold() const override { return true; }
  bool auto_restart() const override { return true; }
  bool has_sidebar() const override { return true; }

  std::unique_ptr<BagRandomizer> create_bag(unsigned seed) const override {
    return std::make_unique<FixedQueueRandomizer>(current_.queue, seed);
  }

  void on_start(TimePoint now) override {
    GameMode::on_start(now);
    pieces_placed_ = 0;
    tsds_ = 0;
    quads_ = 0;
  }

  void setup_board(Board &board) override {
    if (!has_current_ || last_was_win_) {
      std::optional<GeneratedSetup> s = bank_->pop();
      if (!s) {
        for (int i = 0; i < 100; ++i)
          if ((s = generate(fallback_rng_)))
            break;
      }
      if (s)
        current_ = std::move(*s);
      has_current_ = true;
    }
    board = current_.board;
    pieces_placed_ = 0;
    tsds_ = 0;
    quads_ = 0;
    no_float_ok_ = true; // empty residual stack trivially satisfies
    last_was_win_ = false;
    show_hints_ = false;
  }

  void on_piece_locked(const eng::PieceLocked &ev, const GameState &state,
                       CommandBuffer &cmds) override {
    pieces_placed_++;
    if (ev.spin == SpinKind::TSpin && ev.lines_cleared == 2)
      tsds_++;
    if (ev.lines_cleared == 4)
      quads_++;
    // Cache the floating-cells check so draw_sidebar (which has no GameState
    // access) can reflect it live in the goal list.
    no_float_ok_ = no_floating_cells(state.board);

    int total = num_pieces_.load() + reserved_count();
    if (pieces_placed_ >= total) {
      // Win conditions mirror script7.js:detect_win:
      //   TSD:     >=1 T-Spin Double scored AND final stack has no floats.
      //   TSDQuad: TSD AND >=1 Tetris AND no floats.
      bool goals_met = (tsds_ >= 1) &&
                       (variant_ != TSpinVariant::TSDQuad || quads_ >= 1);
      bool won = goals_met && no_floating_cells(state.board);
      last_was_win_ = won;
      attempts_++;
      if (won)
        solves_++;
      cmds.push(cmd::SetGameOver{won});
    }
  }

  void on_undo(const GameState &state) override {
    // Recompute from game state rather than blindly decrementing.
    // spawn_piece() pushes snapshots for hold actions too (not just locks),
    // so a naive decrement under-counts when a hold-self is undone.
    int total = num_pieces_.load() + reserved_count();
    int in_system = 1 // current piece
                    + (state.hold_piece.has_value() ? 1 : 0)
                    + static_cast<int>(state.queue.size());
    pieces_placed_ = std::max(0, total - in_system);
    no_float_ok_ = no_floating_cells(state.board);
  }

  // Expose the reverse-constructed downstack solution as a plan overlay.
  // `show_hints_` gates visibility so the player can toggle spoilers on/off.
  void fill_plan_overlay(ViewModel &vm, const GameState &state) override {
    if (!show_hints_)
      return;
    if (pieces_placed_ >= static_cast<int>(current_.solution.size()))
      return;

    auto remaining = std::span<const Placement>(current_.solution)
                         .subspan(pieces_placed_);

    // Wrap placements as Plan::Step so we can reuse build_plan_overlay's
    // line-clear-aware row_map logic. We only populate `placement`;
    // uses_hold / lines_cleared / board_after are unused by the overlay.
    std::vector<Plan::Step> steps;
    steps.reserve(remaining.size());
    for (const auto &p : remaining) {
      Plan::Step s{};
      s.placement = p;
      steps.push_back(s);
    }

    BoardBitset sim = BoardBitset::from_board(state.board);
    vm.plan_overlay =
        build_plan_overlay(sim, steps, static_cast<int>(steps.size()));
  }

  void fill_hud(HudData &hud, const GameState &state, TimePoint now) override {
    int total = num_pieces_.load() + reserved_count();
    int remaining = std::max(0, total - pieces_placed_);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", remaining);
    hud.center_text = buf;

    if (state.game_over && !end_time_)
      end_time_ = now;

    if (state.game_over) {
      hud.game_over_label = state.won ? "CLEAR!" : "FAILED";
      hud.game_over_label_color = state.won ? Color(100, 255, 100)
                                            : Color(255, 100, 100);
      char det[64];
      std::snprintf(det, sizeof(det), "%d / %d", solves_, attempts_);
      hud.game_over_detail = det;
      hud.game_over_detail_color = Color(200, 200, 200);
    }
  }

  void draw_sidebar() override {
    if (!ImGui::CollapsingHeader(title().c_str(), ImGuiTreeNodeFlags_DefaultOpen))
      return;

    // Goal list — shows the active variant's win criteria with live
    // progress markers so the player can verify what's scored and what's
    // still needed before the queue runs out.
    ImGui::TextUnformatted("Goals:");
    draw_goal("T-Spin Double", tsds_ >= 1);
    if (variant_ == TSpinVariant::TSDQuad)
      draw_goal("Tetris (Quad)", quads_ >= 1);
    draw_goal("No floating cells", no_float_ok_);

    ImGui::Separator();
    ImGui::Text("Bank: %zu / %d", bank_->pool_size(), bank_->target_size());

    // slider_pieces_ persists across frames so dragging isn't snapped back
    // by a per-frame reload from num_pieces_. Commit to num_pieces_ only
    // on mouse release, so mid-drag values don't repeatedly trash the bank.
    ImGui::SliderInt("Pieces", &slider_pieces_, 2, 6);
    if (ImGui::IsItemDeactivatedAfterEdit() &&
        slider_pieces_ != num_pieces_.load()) {
      num_pieces_.store(slider_pieces_);
      bank_->clear_pool();
      request_fresh_puzzle();
    }

    float btn_w = (ImGui::GetContentRegionAvail().x -
                   ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Reset bank", ImVec2(btn_w, 0)))
      bank_->clear_pool();
    ImGui::SameLine();
    ImGui::BeginDisabled(show_hints_);
    if (ImGui::Button("Hint", ImVec2(-FLT_MIN, 0)))
      show_hints_ = true;
    ImGui::EndDisabled();
  }

  bool consume_restart_request() override {
    bool r = pending_restart_;
    pending_restart_ = false;
    return r;
  }

private:
  // Force setup_board to pull a new puzzle (instead of replaying current_)
  // and ask the manager to reset the game on the next frame.
  void request_fresh_puzzle() {
    last_was_win_ = true;
    pending_restart_ = true;
  }


  std::optional<GeneratedSetup> generate(std::mt19937 &rng) {
    PuzzleRequest req;
    req.num_pieces = num_pieces_.load();
    req.allow_skims = true;
    req.smooth_surface = true;
    req.unique_pieces = 1;
    req.max_non_cheese_holes = 0;
    // TSD: 0..2 cheese lines (matches script7.js:902 `random(0..2)`).
    // TSDQuad: fixed 4 lines — the reserved I clears them as a Tetris.
    if (variant_ == TSpinVariant::TSDQuad) {
      req.garbage_below = 4;
      req.reserved = {PieceType::T, PieceType::I};
    } else {
      req.garbage_below = std::uniform_int_distribution<int>(0, 2)(rng);
      req.reserved = {PieceType::T};
    }
    auto p = generate_puzzle(req, rng);
    if (!p)
      return std::nullopt;
    return GeneratedSetup{std::move(p->board), std::move(p->queue),
                          std::move(p->solution)};
  }

  int reserved_count() const {
    return variant_ == TSpinVariant::TSDQuad ? 2 : 1;
  }

  // Row in the goals list: checkmark or bullet + label, colored by state.
  static void draw_goal(const char *label, bool done) {
    ImVec4 color = done ? ImVec4(0.45f, 0.9f, 0.45f, 1.f)
                        : ImVec4(0.7f, 0.7f, 0.7f, 1.f);
    ImGui::TextColored(color, "%s %s", done ? "[x]" : "[ ]", label);
  }

  static bool no_floating_cells(const Board &b) {
    for (int x = 0; x < Board::kWidth; ++x) {
      bool saw_empty = false;
      for (int y = 0; y < Board::kTotalHeight; ++y) {
        bool filled = b.filled(x, y);
        if (!filled)
          saw_empty = true;
        else if (saw_empty)
          return false;
      }
    }
    return true;
  }

  // Atomic: written from the ImGui main thread, read by the bank's
  // generator lambda on the worker thread.
  std::atomic<int> num_pieces_;
  // Staging value for the slider — only pushed to num_pieces_ on release.
  int slider_pieces_;
  TSpinVariant variant_;
  bool show_hints_ = false;
  std::unique_ptr<PuzzleBank> bank_;
  GeneratedSetup current_;
  bool has_current_ = false;
  bool last_was_win_ = false;
  bool pending_restart_ = false;
  int pieces_placed_ = 0;
  int tsds_ = 0;
  int quads_ = 0;
  bool no_float_ok_ = true;
  int solves_ = 0;
  int attempts_ = 0;
  std::mt19937 fallback_rng_{std::random_device{}()};
};
