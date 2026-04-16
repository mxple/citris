#pragma once

#include "game_mode.h"
#include "ai/checkpoint.h"
#include "ai/eval/opener.h"
#include "ai/plan_overlay.h"
#include "ai_state.h"
#include "engine/board.h"
#include <memory>
#include <string>

class OpenerMode : public GameMode {
public:
  explicit OpenerMode(Opener opener) : opener_(std::move(opener)) {}

  // Called by GameManager after construction to wire up AI.
  void bind_ai(AIState &ai) { ai_ = &ai; }
  bool has_ai() const { return ai_ != nullptr; }

  std::string title() const override { return "Opener: " + opener_.name; }

  std::chrono::milliseconds gravity_interval() const override {
    return std::chrono::milliseconds{10000};
  }
  std::chrono::milliseconds lock_delay() const override {
    return std::chrono::milliseconds{5000};
  }
  bool undo_allowed() const override { return true; }

  void on_start(TimePoint now) override {
    GameMode::on_start(now);
    current_node_ = -1;
    depth_ = 0;
    deviated_ = false;
    shadow_board_ = Board{};
    placed_pieces_since_cp_.clear();

    // TODO: configure ai_ for opener search
    //   - ai_->active = true
    //   - ai_->override_beam_width = 1000
    //   - ai_->custom_evaluator = factory that creates OpenerEvaluator with collect_targets()
    //   - ai_->rebuild_ai()
    //   - ai_->needs_search = true
    configure_ai();
  }

  void on_piece_locked(const eng::PieceLocked &ev, const GameState &state,
                       CommandBuffer &cmds) override {
    Piece locked_piece(ev.type, ev.rotation, ev.x, ev.y);
    shadow_board_.place(locked_piece);
    placed_pieces_since_cp_.push_back(ev.type);

    // TODO: check checkpoints on pre-clear and post-clear shadow board
    //   - if a checkpoint matches: advance current_node_, reset deviation,
    //     reconfigure_ai for next checkpoint targets
    //   - else check if player followed the plan (compare board state),
    //     advance plan if matched, ai_->ai.advance() for tree reuse
    //   - else: deviated_ = true, ai_->needs_search = true
    check_checkpoints(ev, state);
  }

  void on_undo(const GameState &state) override {
    shadow_board_ = state.board;
    if (!placed_pieces_since_cp_.empty())
      placed_pieces_since_cp_.pop_back();
  }

  void on_tick(TimePoint now, const GameState &state,
               CommandBuffer &cmds) override {
    // Search is now driven by GameManager::pump_ai via ai_state_.
    // Nothing to do here.
  }

  void fill_hud(HudData &hud, const GameState &state, TimePoint now) override {
    int max_d = opener_.max_depth();
    if (max_d > 0) {
      hud.center_text = opener_.name + "  " + std::to_string(depth_) + "/" +
                        std::to_string(max_d);
      if (ai_->searching())
        hud.center_color = Color(200, 200, 200);
      else if (deviated_)
        hud.center_color = Color(255, 180, 80);
      else
        hud.center_color = Color(180, 220, 255);
    }

    if (state.game_over) {
      bool complete = is_complete();
      hud.game_over_label = complete ? "COMPLETE!" : "GAME OVER";
      hud.game_over_label_color =
          complete ? Color(100, 255, 100) : Color(255, 100, 100);
    }
  }

  void fill_plan_overlay(ViewModel &vm, const GameState &state) override {
    auto targets = active_target_indices();
    if (targets.empty())
      return;

    auto &cp = opener_.nodes[targets[0]].checkpoint;
    CheckpointOverlay co;
    co.rows = cp.rows;
    vm.checkpoint_overlay = std::move(co);

    if (ai_->plan.complete())
      return;

    auto remaining =
        std::span(ai_->plan.steps).subspan(ai_->plan.current_step);
    BoardBitset sim = BoardBitset::from_board(state.board);
    vm.plan_overlay = build_plan_overlay(sim, remaining);
  }

  const Opener &opener() const { return opener_; }

private:
  std::vector<int> active_target_indices() const {
    if (current_node_ < 0)
      return opener_.roots;
    return opener_.nodes[current_node_].children;
  }

  bool is_complete() const {
    if (current_node_ < 0)
      return opener_.empty();
    return opener_.nodes[current_node_].children.empty();
  }

  // TODO: build adjusted checkpoint targets (reduce min_counts by placed pieces,
  // adjust max_pieces). Return pointers into adjusted_targets_ storage.
  std::vector<const Checkpoint *> collect_targets() const {
    auto indices = active_target_indices();
    PieceCounts prior{};
    for (auto pt : placed_pieces_since_cp_)
      prior[static_cast<int>(pt)]++;

    adjusted_targets_.clear();
    adjusted_targets_.reserve(indices.size());
    for (int ni : indices) {
      Checkpoint cp = opener_.nodes[ni].checkpoint;
      if (cp.constraint.type == ConstraintType::AtLeast) {
        for (int i = 0; i < 7; ++i) {
          int adjusted =
              static_cast<int>(cp.constraint.min_counts[i]) - prior[i];
          cp.constraint.min_counts[i] =
              static_cast<uint8_t>(std::max(0, adjusted));
        }
      }
      if (cp.max_pieces > 0) {
        cp.max_pieces = std::max(1, cp.max_pieces -
                                    (int)placed_pieces_since_cp_.size());
      }
      adjusted_targets_.push_back(std::move(cp));
    }

    std::vector<const Checkpoint *> ptrs;
    ptrs.reserve(adjusted_targets_.size());
    for (auto &t : adjusted_targets_)
      ptrs.push_back(&t);
    return ptrs;
  }

  int target_max_depth() const {
    int max_p = -1;
    for (auto &cp : adjusted_targets_) {
      if (cp.max_pieces > 0)
        max_p = std::max(max_p, cp.max_pieces);
    }
    return max_p;
  }

  // TODO: set ai_->custom_evaluator to a lambda that creates OpenerEvaluator
  // with collect_targets(). Set ai_->override_beam_width = 1000.
  // Set ai_->override_max_depth based on target_max_depth() (or nullopt if deviated).
  // Call ai_->rebuild_ai() and ai_->needs_search = true.
  void configure_ai() {
    auto target_ptrs = collect_targets();
    int max_d = deviated_ ? -1 : target_max_depth();

    ai_->custom_evaluator = [this]() -> std::unique_ptr<Evaluator> {
      return std::make_unique<OpenerEvaluator>(collect_targets());
    };
    ai_->override_beam_width = 1000;
    ai_->override_max_depth = max_d > 0 ? std::optional(max_d) : std::nullopt;
    ai_->rebuild_ai();
    ai_->needs_search = true;
  }

  // TODO: implement checkpoint matching on shadow_board_ (pre-clear and post-clear),
  // plan following via board comparison, and deviation detection.
  // On checkpoint match: advance current_node_/depth_, clear placed_pieces_since_cp_,
  //   shadow_board_.clear_lines(), configure_ai().
  // On plan match: ai_->plan.advance(), ai_->ai.advance() for tree reuse.
  // On deviation: deviated_ = true, configure_ai().
  void check_checkpoints(const eng::PieceLocked &ev, const GameState &state) {
    auto targets = active_target_indices();
    if (targets.empty()) {
      shadow_board_.clear_lines();
      return;
    }

    // Check pre-clear
    BoardBitset preclr = BoardBitset::from_board(shadow_board_);
    for (int ni : targets) {
      auto &cp = opener_.nodes[ni].checkpoint;
      if (!cp.matches(preclr))
        continue;
      // TODO: validate piece annotations if cp.has_piece_annotations()
      advance_checkpoint(ni);
      return;
    }

    int cleared = shadow_board_.clear_lines();

    // Check post-clear
    if (cleared > 0) {
      BoardBitset postclr = BoardBitset::from_board(shadow_board_);
      for (int ni : targets) {
        auto &cp = opener_.nodes[ni].checkpoint;
        if (!cp.matches(postclr))
          continue;
        advance_checkpoint(ni);
        return;
      }
    }

    // Check if player followed the plan
    BoardBitset bb = BoardBitset::from_board(state.board);
    if (!ai_->plan.complete() && ai_->plan.current()) {
      auto &step = *ai_->plan.current();
      bool matches = true;
      for (int y = 0; y < BoardBitset::kHeight; ++y) {
        if (bb.rows[y] != step.board_after.rows[y]) {
          matches = false;
          break;
        }
      }
      if (matches) {
        ai_->plan.advance();
        deviated_ = false;
        Placement played{ev.type, ev.rotation, (int8_t)ev.x, (int8_t)ev.y,
                         ev.spin};
        ai_->ai.advance(played, false);
        return;
      }
    }

    // Deviated
    deviated_ = true;
    configure_ai();
  }

  void advance_checkpoint(int node_idx) {
    current_node_ = node_idx;
    ++depth_;
    deviated_ = false;
    placed_pieces_since_cp_.clear();
    shadow_board_.clear_lines();
    configure_ai();
  }

  Opener opener_;
  AIState *ai_ = nullptr;
  Board shadow_board_;
  std::vector<PieceType> placed_pieces_since_cp_;
  mutable std::vector<Checkpoint> adjusted_targets_;
  int current_node_ = -1;
  int depth_ = 0;
  bool deviated_ = false;
};
