#pragma once

#include "ai/ai.h"
#include "ai/board_bitset.h"
#include "ai/checkpoint.h"
#include "ai/eval.h"
#include "ai/eval_cheese.h"
#include "ai/eval_sprint.h"
#include "ai/eval_tsd.h"
#include "ai/search_task.h"
#include "engine/game_state.h"
#include "engine_event.h"

struct AIState {
  enum class EvalType { Tsd, Sprint, Cheese, Default };

  AI ai;
  Plan plan;
  std::unique_ptr<SearchTask> search_task;
  std::vector<PieceType> queue;
  BoardBitset search_board;

  // Cached results (safe to read from main thread after poll_search returns true)
  SearchResult last_result;
  int last_depth = 0;

  bool active = false;
  bool plan_computed = false;
  bool needs_search = false;
  bool autoplay = false;
  EvalType eval_type = EvalType::Tsd;
  int max_visible = 7;
  int input_interval_ms = 250;

  bool searching() const { return search_task && !search_task->ready(); }

  std::unique_ptr<Evaluator> make_evaluator() const {
    switch (eval_type) {
    case EvalType::Tsd:
      return std::make_unique<TsdEvaluator>();
    case EvalType::Sprint:
      return std::make_unique<SprintEvaluator>();
    case EvalType::Cheese:
      return std::make_unique<CheeseEvaluator>();
    case EvalType::Default: {
      struct DefaultEvaluator : Evaluator {
        float board_eval(const BoardBitset &board) const override {
          return board_eval_default(board, {});
        }
        float tactical_eval(const Placement &, int lines_cleared, int,
                            const SearchState &) const override {
          return static_cast<float>(lines_cleared) * 3.0f;
        }
        float composite(float board_score, float tactical_score,
                        int) const override {
          return board_score + tactical_score;
        }
        bool accumulate_tactical() const override { return true; }
      };
      return std::make_unique<DefaultEvaluator>();
    }
    }
    return std::make_unique<TsdEvaluator>();
  }

  void rebuild_ai() {
    search_task.reset();
    ai = AI::builder()
             .width(800)
             .depth(12)
             .sonic(true)
             .evaluator(make_evaluator())
             .build();
  }

  void start_search(const GameState &state) {
    search_task.reset();

    BoardBitset bb = BoardBitset::from_board(state.board);
    queue.clear();
    queue.push_back(state.current_piece.type);
    for (auto &p : state.preview)
      queue.push_back(p);

    search_board = bb;
    ai.set_evaluator(make_evaluator());

    SearchState root;
    root.board = bb;
    root.hold = state.hold_piece;
    root.hold_available = state.hold_available;
    root.bag_draws = state.bag_draws;
    ai.reset(root);

    search_task = std::make_unique<SearchTask>(ai, queue);
    plan = Plan{};
    plan_computed = false;
  }

  bool poll_search() {
    if (!search_task || !search_task->ready())
      return false;
    search_task->join();
    search_task.reset();
    last_result = ai.result();
    last_depth = ai.depth();
    build_plan(last_result);
    plan_computed = !last_result.pv.empty();
    return true;
  }

  void on_piece_locked(const eng::PieceLocked &ev) {
    if (plan_computed && !plan.complete() && plan.current()) {
      auto &step = *plan.current();
      auto &p = step.placement;
      if (ev.type == p.type && ev.rotation == p.rotation &&
          ev.x == p.x && ev.y == p.y) {
        plan.advance();
        ai.advance(p, step.uses_hold);
      }
    }
    needs_search = true;
  }

  void deactivate() {
    search_task.reset();
    plan = Plan{};
    plan_computed = false;
    needs_search = false;
    autoplay = false;
  }

private:
  void build_plan(const SearchResult &result) {
    plan.steps.clear();
    plan.current_step = 0;

    if (result.pv.empty())
      return;

    BoardBitset sim = search_board;
    for (size_t i = 0; i < result.pv.size(); ++i) {
      auto &p = result.pv[i];
      Plan::Step step;
      step.placement = p;
      step.uses_hold = (i == 0) ? result.hold_used : false;
      sim.place(p.type, p.rotation, p.x, p.y);
      step.lines_cleared = sim.clear_lines();
      step.board_after = sim;
      plan.steps.push_back(std::move(step));
    }
  }
};
