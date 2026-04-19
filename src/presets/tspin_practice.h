#pragma once

#include "ai/board_bitset.h"
#include "ai/plan.h"
#include "ai/plan_overlay.h"
#include "ai/puzzle_gen.h"
#include "engine/piece_queue.h"
#include "engine/piece_source.h"
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

// TSpin-practice preset built on the downstack-practice puzzle generator.
// Supports five variants (see script6.js + script7.js), all sharing the
// same reverse-construction pipeline — only the keyhole, reserved pieces,
// cheese-row count, and win criteria differ.
//
//   - TSD:      1 TSD. Reserved {T}.           (script7.js)
//   - TSDQuad:  1 TSD + 1 Tetris. Reserved {T, I}. Fixed 4 cheese rows.
//   - DTCannon: 1 TSD + 1 TST. Reserved {T, T}.  (script6.js mode='dt')
//   - Cspin:    1 TSD + 1 TST. Reserved {T, T}.  (script6.js mode='cspin')
//   - Fractal:  2 TSDs.        Reserved {T, T}.  (script6.js mode='fractal')
enum class TSpinVariant { TSD, TSDQuad, DTCannon, Cspin, Fractal };

// Declarative per-variant spec. Replaces the previous hard-coded
// `if (variant == TSDQuad)` branches so adding a new variant is a
// single table entry. TODO: populate with the four non-TSD rows.
struct TSpinVariantSpec {
  const char *title;
  PuzzleMode puzzle_mode;
  std::vector<PieceType> reserved;
  int required_tsds;   // min T-Spin Doubles to win
  int required_tsts;   // min T-Spin Triples to win
  int required_quads;  // min Tetrises (quad line clears) to win
  int garbage_below_min; // cheese rows, inclusive min
  int garbage_below_max; // inclusive max; use min==max for fixed count
  // Non-cheese hole budget during reverse construction. TSD uses 0
  // (strict) so the overhang is forced to uncarve, hiding the setup.
  // DT/Cspin/Fractal keyholes have too many stamped N-cells to satisfy a
  // strict budget, so they use a permissive cap (matches the reference's
  // `mdhole_ind = true` relaxation, see script6.js:1307-1308).
  int max_non_cheese_holes;
  // Whether the win check requires a no-floating-cells board. TSD demands
  // clean downstack (script7.js:detect_win calls all_grounded). DT/Cspin
  // etc. score on spins only (script6.js:1385-1410 has no grounding check)
  // — a TST inherently leaves an overhang, so requiring it is impossible.
  bool require_no_float;
  // Default skim policy for the "Normal" difficulty tier. TSD randomizes
  // (script7.js:1048 `skim_ind = Math.random()>0.5`); script6.js modes
  // default to false (script6.js:6 `'skim_ind':false`).
  bool allow_skims_default;
};

inline TSpinVariantSpec variant_spec(TSpinVariant v) {
  switch (v) {
  case TSpinVariant::TSD:
    return {"TSD Practice", PuzzleMode::TSD,
            {PieceType::T}, 1, 0, 0, 0, 2, 0, true, true};
  case TSpinVariant::TSDQuad:
    return {"TSD + Quad Practice", PuzzleMode::TSD,
            {PieceType::T, PieceType::I}, 1, 0, 1, 4, 4, 0, true, true};
  case TSpinVariant::DTCannon:
    return {"DT Cannon Practice", PuzzleMode::DTCannon,
            {PieceType::T, PieceType::T}, 1, 1, 0, 0, 2, 99, false, false};
  case TSpinVariant::Cspin:
    return {"C-Spin Practice", PuzzleMode::Cspin,
            {PieceType::T, PieceType::T}, 1, 1, 0, 0, 2, 99, false, false};
  case TSpinVariant::Fractal:
    return {"Fractal Practice", PuzzleMode::Fractal,
            {PieceType::T, PieceType::T}, 2, 0, 0, 0, 2, 99, false, false};
  }
  return {};
}

class TSpinPracticeMode : public GameMode {
public:
  explicit TSpinPracticeMode(int num_pieces = 5,
                             TSpinVariant variant = TSpinVariant::TSD)
      : num_pieces_(num_pieces), slider_pieces_(num_pieces), variant_(variant) {
    bank_ = std::make_unique<PuzzleBank>(
        [this](std::mt19937 &rng) { return generate(rng); });
  }

  std::string title() const override { return variant_spec(variant_).title; }

  bool undo_allowed() const override { return true; }
  bool infinite_hold() const override { return true; }
  bool auto_restart() const override { return true; }
  bool has_sidebar() const override { return true; }
  std::chrono::milliseconds gravity_interval() const override {
    return std::chrono::milliseconds{-1};
  }
  std::chrono::milliseconds lock_delay() const override {
    return std::chrono::milliseconds{-1};
  }
  int max_lock_resets() const override { return -1; }

  PieceQueue create_queue(unsigned /*seed*/) const override {
    // Puzzle pieces are pre-loaded into the buffer (already hold-shuffled
    // by the puzzle generator); the source is exhausted (no further
    // pieces beyond the puzzle).
    return PieceQueue(std::make_unique<EmptySource>(), current_.queue);
  }

  void on_start(TimePoint now) override {
    GameMode::on_start(now);
    pieces_placed_ = 0;
    tsds_ = 0;
    tsts_ = 0;
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
    tsts_ = 0;
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
    if (ev.spin == SpinKind::TSpin && ev.lines_cleared == 3)
      tsts_++;
    if (ev.lines_cleared == 4)
      quads_++;
    // Cache the floating-cells check so draw_sidebar (which has no GameState
    // access) can reflect it live in the goal list.
    no_float_ok_ = no_floating_cells(state.board);

    int total = num_pieces_.load() + reserved_count();
    if (pieces_placed_ >= total) {
      // Win conditions are data-driven from variant_spec. Mirrors
      // script7.js:detect_win (TSD/TSDQuad — checks all_grounded) and
      // script6.js:1385-1410 (DT/Cspin/Fractal — score only; a TST leaves
      // an overhang, so no-floating would be impossible).
      auto s = variant_spec(variant_);
      bool goals_met = tsds_ >= s.required_tsds &&
                       tsts_ >= s.required_tsts &&
                       quads_ >= s.required_quads;
      bool won = goals_met &&
                 (!s.require_no_float || no_floating_cells(state.board));
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
    auto s = variant_spec(variant_);
    char lbl[32];
    if (s.required_tsds > 0) {
      if (s.required_tsds == 1)
        draw_goal("T-Spin Double", tsds_ >= 1);
      else {
        std::snprintf(lbl, sizeof(lbl), "%dx T-Spin Double", s.required_tsds);
        draw_goal(lbl, tsds_ >= s.required_tsds);
      }
    }
    if (s.required_tsts > 0) {
      if (s.required_tsts == 1)
        draw_goal("T-Spin Triple", tsts_ >= 1);
      else {
        std::snprintf(lbl, sizeof(lbl), "%dx T-Spin Triple", s.required_tsts);
        draw_goal(lbl, tsts_ >= s.required_tsts);
      }
    }
    if (s.required_quads > 0)
      draw_goal("Tetris (Quad)", quads_ >= s.required_quads);
    if (s.require_no_float)
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

    // Difficulty slider — controls allow_skims + cheese-row range.
    //   Easy  : no skims, min cheese
    //   Normal: variant-default skims, full cheese range
    //   Hard  : skims on, max cheese
    static const char *const kDiffLabels[] = {"Easy", "Normal", "Hard"};
    ImGui::SliderInt("Difficulty", &slider_difficulty_, 0, 2,
                     kDiffLabels[slider_difficulty_]);
    if (ImGui::IsItemDeactivatedAfterEdit() &&
        slider_difficulty_ != difficulty_.load()) {
      difficulty_.store(slider_difficulty_);
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
    auto s = variant_spec(variant_);
    int d = difficulty_.load(); // 0=Easy, 1=Normal, 2=Hard
    PuzzleRequest req;
    req.mode = s.puzzle_mode;
    req.num_pieces = num_pieces_.load();
    // Easy: always no skims + min cheese. Normal: variant default skims +
    // full cheese range. Hard: force skims on + max cheese.
    req.allow_skims = (d == 0) ? false
                     : (d == 2) ? true
                                : s.allow_skims_default;
    req.smooth_surface = true;
    req.unique_pieces = 1;
    req.max_non_cheese_holes = s.max_non_cheese_holes;
    req.reserved = s.reserved;
    int cheese_lo = (d == 2) ? s.garbage_below_max : s.garbage_below_min;
    int cheese_hi = (d == 0) ? s.garbage_below_min : s.garbage_below_max;
    req.garbage_below =
        std::uniform_int_distribution<int>(cheese_lo, cheese_hi)(rng);
    auto p = generate_puzzle(req, rng);
    if (!p)
      return std::nullopt;
    return GeneratedSetup{std::move(p->board), std::move(p->queue),
                          std::move(p->solution)};
  }

  int reserved_count() const {
    return static_cast<int>(variant_spec(variant_).reserved.size());
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
  std::atomic<int> difficulty_{1};  // 0=Easy, 1=Normal, 2=Hard
  int slider_difficulty_ = 1;
  TSpinVariant variant_;
  bool show_hints_ = false;
  std::unique_ptr<PuzzleBank> bank_;
  GeneratedSetup current_;
  bool has_current_ = false;
  bool last_was_win_ = false;
  bool pending_restart_ = false;
  int pieces_placed_ = 0;
  int tsds_ = 0;
  int tsts_ = 0;
  int quads_ = 0;
  bool no_float_ok_ = true;
  int solves_ = 0;
  int attempts_ = 0;
  std::mt19937 fallback_rng_{std::random_device{}()};
};
