#pragma once

#include "ai/board_bitset.h"
#include "ai/eval/eval.h"
#include "ai/movegen.h"
#include "ai/pathfinder.h"
#include "ai/plan.h"
#include "ai/plan_overlay.h"
#include "command.h"
#include "engine/board.h"
#include "engine/piece.h"
#include "engine/piece_queue.h"
#include "engine/piece_source.h"
#include "game_mode.h"
#include "log.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <imgui.h>
#include <random>
#include <span>
#include <string>
#include <vector>

// Finesse trainer: each scenario is generated synchronously per-call — a
// fresh random board, one random allowed piece, enumerate every legal
// placement, filter by eval quality + difficulty criterion, pick one
// uniformly. Advanced is two-phase: try hard for a spin scenario first,
// fall back to "spin or inputs>=4" only if spin generation exhausts. No
// worker/bank — per-scenario generation keeps boards unique and piece
// distribution truly uniform over allowed pieces.
enum class FinesseDifficulty { Normal, Advanced };

class FinesseTrainerMode : public GameMode {
public:
  explicit FinesseTrainerMode(FinesseDifficulty diff) : difficulty_(diff) {
    regenerate_scenario();
  }

  std::string title() const override {
    return difficulty_ == FinesseDifficulty::Normal ? "Finesse: Normal"
                                                    : "Finesse: Advanced";
  }

  // Untimed: no gravity, no lock delay.
  std::chrono::milliseconds gravity_interval() const override {
    return std::chrono::milliseconds{-1};
  }
  std::chrono::milliseconds lock_delay() const override {
    return std::chrono::milliseconds{-1};
  }
  int max_lock_resets() const override { return -1; }
  bool hold_allowed() const override { return false; }
  bool undo_allowed() const override { return false; }
  bool auto_restart() const override { return true; }
  bool has_sidebar() const override { return true; }

  PieceQueue create_queue(unsigned /*seed*/) const override {
    return PieceQueue(std::make_unique<EmptySource>(), {target_piece_});
  }

  int queue_visible() const override { return 0; }

  void on_start(TimePoint now) override {
    GameMode::on_start(now);
    user_inputs_this_piece_ = 0;
    reviewing_fail_ = false;
  }

  void setup_board(Board &board) override {
    if (regenerate_next_) {
      regenerate_scenario();
      regenerate_next_ = false;
    }
    board = scenario_board_;
    user_inputs_this_piece_ = 0;
    reviewing_fail_ = false;
  }

  void on_piece_locked(const eng::PieceLocked &ev, const GameState &,
                       CommandBuffer &cmds) override {
    Placement locked{ev.type, ev.rotation, ev.x, ev.y, ev.spin};
    bool correct_landing =
        locked.canonical().same_landing(target_placement_.canonical());
    bool input_ok =
        user_inputs_this_piece_ <= static_cast<int>(optimal_inputs_.size());
    bool won = correct_landing && input_ok;

    attempts_++;
    last_user_inputs_ = user_inputs_this_piece_;
    last_correct_landing_ = correct_landing;
    if (won) {
      solves_++;
      regenerate_next_ = true;
    } else {
      regenerate_next_ = false;
      reviewing_fail_ = true;
    }
    cmds.push(cmd::SetGameOver{won});
  }

  void on_input_registered(GameInput inp, const GameState &state) override {
    if (!state.game_over) {
      user_inputs_this_piece_++;
    } else if (reviewing_fail_ && inp == GameInput::HardDrop) {
      pending_restart_ = true;
    }
  }

  void fill_plan_overlay(ViewModel &vm, const GameState &state) override {
    if (state.game_over)
      return;
    Plan::Step step{};
    step.placement = target_placement_;
    BoardBitset sim = BoardBitset::from_board(state.board);
    vm.plan_overlay = build_plan_overlay(sim, std::span{&step, 1}, 1);
  }

  void fill_hud(HudData &hud, const GameState &state, TimePoint now) override {
    if (!state.game_over) {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "Opt: %zu", optimal_inputs_.size());
      hud.center_text = buf;
      return;
    }

    if (!end_time_)
      end_time_ = now;

    if (state.won) {
      hud.game_over_label = "NICE";
      hud.game_over_label_color = Color(100, 255, 100);
      char det[64];
      std::snprintf(det, sizeof(det), "%d / %zu inputs", last_user_inputs_,
                    optimal_inputs_.size());
      hud.game_over_detail = det;
    } else {
      hud.game_over_label = "MISS";
      hud.game_over_label_color = Color(255, 100, 100);
      std::string seq = format_optimal_sequence();
      char det[160];
      if (last_correct_landing_) {
        std::snprintf(det, sizeof(det), "%d / %zu inputs\n%s\n[HD to retry]",
                      last_user_inputs_, optimal_inputs_.size(), seq.c_str());
      } else {
        std::snprintf(det, sizeof(det), "wrong placement\n%s\n[HD to retry]",
                      seq.c_str());
      }
      hud.game_over_detail = det;
    }
    hud.game_over_detail_color = Color(220, 220, 220);
  }

  void draw_sidebar() override {
    if (!ImGui::CollapsingHeader(title().c_str(),
                                 ImGuiTreeNodeFlags_DefaultOpen))
      return;
    ImGui::Text("Attempts: %d", attempts_);
    ImGui::Text("Solves:   %d", solves_);
    if (attempts_ > 0)
      ImGui::Text("Success:  %.1f%%",
                  100.0f * static_cast<float>(solves_) / attempts_);

    ImGui::Separator();
    if (ImGui::Checkbox("Enforce 180 spins", &enforce_180_)) {
      regenerate_next_ = true;
      pending_restart_ = true;
    }
    ImGui::Separator();

    ImGui::TextUnformatted("Pieces:");
    static constexpr std::array<const char *, 7> kPieceLabels = {
        "I", "O", "T", "S", "Z", "J", "L"};
    bool any_changed = false;
    if (ImGui::BeginTable("##pieces", 7, ImGuiTableFlags_SizingStretchSame)) {
      for (int i = 0; i < 7; ++i) {
        ImGui::TableNextColumn();
        if (ImGui::Checkbox(kPieceLabels[i], &allowed_pieces_[i]))
          any_changed = true;
      }
      ImGui::EndTable();
    }
    if (any_changed) {
      bool any_on = std::any_of(allowed_pieces_.begin(), allowed_pieces_.end(),
                                [](bool b) { return b; });
      if (!any_on)
        allowed_pieces_[static_cast<int>(target_piece_)] = true;
      regenerate_next_ = true;
      pending_restart_ = true;
    }
  }

  bool consume_restart_request() override {
    bool r = pending_restart_;
    pending_restart_ = false;
    return r;
  }

private:
  // Filter criterion applied to path-verified candidates.
  //   Easy: no SonicDrop in path (straight-drop finesse).
  //   Medium: SonicDrop required.
  //   Hard: SonicDrop required and path length > 5.
  enum class Criterion { Easy, Medium, Hard };

  void regenerate_scenario() {
    if (difficulty_ == FinesseDifficulty::Advanced) {
      for (int i = 0; i < 50; ++i)
        if (try_generate_one(Criterion::Hard))
          return;
      for (int i = 0; i < 50; ++i)
        if (try_generate_one(Criterion::Medium))
          return;
    }
    for (int i = 0; i < 80; ++i)
      if (try_generate_one(Criterion::Easy))
        return;
  }

  bool try_generate_one(Criterion crit) {
    std::array<int, 7> allowed_idx;
    int n_allowed = 0;
    for (int i = 0; i < 7; ++i)
      if (allowed_pieces_[i])
        allowed_idx[n_allowed++] = i;
    if (n_allowed == 0)
      return false;
    PieceType ptype = static_cast<PieceType>(
        allowed_idx[std::uniform_int_distribution<>(0, n_allowed - 1)(rng_)]);

    BoardBitset bb = generate_random_board(rng_);
    MoveBuffer buf;
    generate_moves(bb, buf, ptype, true);
    if (buf.count == 0)
      return false;

    Board display_board = board_from_bitset(bb);
    Vec2 sp = spawn_position(ptype);
    BoardWeights weights{};

    struct Candidate {
      Placement placement;
      std::vector<GameInput> path;
    };
    std::vector<Candidate> candidates;

    for (int i = 0; i < buf.count; ++i) {
      const Placement &p = buf.moves[i];

      auto path = find_path(display_board, p, sp.x, sp.y, Rotation::North,
                            enforce_180_, false, true);
      if (path.empty())
        continue;

      bool has_sd = std::any_of(path.begin(), path.end(), [](GameInput g) {
        return g == GameInput::SonicDrop;
      });
      switch (crit) {
      case Criterion::Easy:
        if (has_sd)
          continue;
        break;
      case Criterion::Medium:
        if (!has_sd)
          continue;
        break;
      case Criterion::Hard:
        if (!has_sd || path.size() <= 5)
          continue;
        break;
      }

      BoardBitset after = bb;
      after.place(p.type, p.rotation, p.x, p.y);
      after.clear_lines();
      if (board_eval_default(after, weights) < kQualityFloor)
        continue;

      candidates.push_back({p, std::move(path)});
    }
    if (candidates.empty())
      return false;

    auto &chosen = candidates[std::uniform_int_distribution<size_t>(
        0, candidates.size() - 1)(rng_)];
    scenario_board_ = std::move(display_board);
    target_piece_ = ptype;
    target_placement_ = chosen.placement;
    optimal_inputs_ = std::move(chosen.path);
    return true;
  }

  static constexpr float kQualityFloor = -40.0f;

  // Random-garbage board with surface variation. Start with 2–6 rows of
  // single-hole cheese garbage, then play 0–8 random legal placements on top
  // to create overhangs, stacked holes, hooks, and T-slot cavities that pure
  // cheese doesn't produce. Bounded to keep placements in-bounds.
  static BoardBitset generate_random_board(std::mt19937 &rng) {
    BoardBitset bb;
    int garbage_rows = std::uniform_int_distribution<>(2, 6)(rng);
    for (int y = 0; y < garbage_rows; ++y) {
      int hole = std::uniform_int_distribution<>(0, 9)(rng);
      bb.rows[y] = static_cast<uint16_t>(0x3FF & ~(uint16_t(1) << hole));
    }
    for (int x = 0; x < BoardBitset::kWidth; ++x) {
      uint64_t c = 0;
      for (int y = 0; y < BoardBitset::kHeight; ++y)
        if (bb.rows[y] & (uint16_t(1) << x))
          c |= uint64_t(1) << y;
      bb.cols[x] = c;
    }

    int random_pieces = std::uniform_int_distribution<>(0, 8)(rng);
    for (int i = 0; i < random_pieces; ++i) {
      auto ptype =
          static_cast<PieceType>(std::uniform_int_distribution<>(0, 6)(rng));
      MoveBuffer buf;
      generate_moves(bb, buf, ptype, true);
      if (buf.count == 0)
        break;
      int idx = std::uniform_int_distribution<>(0, buf.count - 1)(rng);
      const Placement &p = buf.moves[idx];
      bb.place(p.type, p.rotation, p.x, p.y);
      bb.clear_lines();
      if (bb.max_height() > 14)
        break;
    }
    return bb;
  }

  static Board board_from_bitset(const BoardBitset &bs) {
    Board b;
    for (int y = 0; y < Board::kTotalHeight; ++y)
      for (int x = 0; x < Board::kWidth; ++x)
        if (bs.occupied(x, y))
          b.set_cell(x, y, CellColor::Garbage);
    return b;
  }

  static const char *input_short(GameInput inp) {
    switch (inp) {
    case GameInput::Left:
      return "L";
    case GameInput::Right:
      return "R";
    case GameInput::RotateCW:
      return "CW";
    case GameInput::RotateCCW:
      return "CCW";
    case GameInput::Rotate180:
      return "180";
    case GameInput::HardDrop:
      return "HD";
    case GameInput::LLeft:
      return "LL";
    case GameInput::RRight:
      return "RR";
    case GameInput::SonicDrop:
      return "SD";
    default:
      LOG_WARN("Invalid game input! {}", inp);
      return "?";
    }
  }

  std::string format_optimal_sequence() const {
    std::string out;
    for (size_t i = 0; i < optimal_inputs_.size(); ++i) {
      if (i)
        out += ", ";
      out += input_short(optimal_inputs_[i]);
    }
    return out;
  }

  FinesseDifficulty difficulty_;
  Board scenario_board_;
  PieceType target_piece_ = PieceType::T;
  Placement target_placement_{};
  std::vector<GameInput> optimal_inputs_;

  int user_inputs_this_piece_ = 0;
  int last_user_inputs_ = 0;
  bool last_correct_landing_ = false;

  int attempts_ = 0;
  int solves_ = 0;

  bool reviewing_fail_ = false;
  bool regenerate_next_ = false;
  bool pending_restart_ = false;
  bool enforce_180_ = true;
  std::array<bool, 7> allowed_pieces_{true, true, true, true, true, true, true};

  std::mt19937 rng_{std::random_device{}()};
};
