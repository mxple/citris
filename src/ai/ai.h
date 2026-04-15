#pragma once

#include "eval/eval.h"
#include "placement.h"
#include <atomic>
#include <memory>
#include <span>
#include <vector>

struct AIConfig {
  int beam_width = 800;
  int max_depth = 14;
  float futility_delta = 50.0f;
  bool sonic_only = true;
  bool extend_queue_7bag = true;
  int quiescence_max = 3;
};

// --- Search output types ---

struct SearchProgress {
  int depth;
  float best_score;
};

struct SearchResult {
  Placement best_move;
  bool hold_used = false;
  float score = 0.0f;
  std::vector<Placement> pv;
  std::vector<std::pair<Placement, float>> root_scores;
};

struct MoveRating {
  int rank;
  float player_score;
  float best_score;
  float delta;
  Placement best_move;
};

// --- Internal tree node (32 bytes, arena-allocated) ---

struct TreeNode {
  Placement move;
  float score;
  uint32_t parent;
  uint32_t first_child;
  uint32_t next_sibling;
  uint16_t child_count;
  uint8_t flags;
  bool hold_used;
  int8_t lines_cleared;
  int8_t attack;
  uint8_t qi;  // queue index after consuming (child_queue_idx)

  static constexpr uint8_t kExpanded = 1;
  static constexpr uint8_t kPruned = 2;
  static constexpr uint32_t kNull = UINT32_MAX;
};

// --- Beam node — temporary working set for current depth ---

struct BeamNode {
  SearchState state;
  float score;
  float cumulative_tactical = 0.0f;
  float bag_weight = 1.0f;  // cumulative probability from bag extension draws
  uint32_t tree_idx;
  Placement root_move;
  bool root_hold_used;

  // Effective score for beam selection — discounts speculative branches
  float effective_score() const { return score * bag_weight; }
};

// ---------------------------------------------------------------------------
// AI class
// ---------------------------------------------------------------------------

class AI {
public:
  class Builder {
  public:
    Builder &width(int beam_width);
    Builder &depth(int max_depth);
    Builder &futility(float delta);
    Builder &sonic(bool sonic_only);
    Builder &extend_bag(bool extend);
    Builder &evaluator(std::unique_ptr<Evaluator> eval);
    AI build();

  private:
    AIConfig config_;
    std::unique_ptr<Evaluator> eval_;
  };

  static Builder builder();

  AI();
  AI(AI &&) noexcept;
  AI &operator=(AI &&) noexcept;
  ~AI();

  // Lifecycle
  void reset(SearchState root);
  bool advance(Placement move, bool hold);
  void set_evaluator(std::unique_ptr<Evaluator> eval);
  void set_config(AIConfig config);

  // Search (blocking — meant to run on a background thread)
  void run_search(std::span<const PieceType> queue,
                  const std::atomic<bool> &cancel);

  // Query (available any time, returns best-so-far)
  SearchResult result() const;
  MoveRating rate(Placement player_move) const;

  // State
  bool searching() const;
  int depth() const;
  const SearchState &root_state() const;

private:
  // Arena management
  uint32_t alloc_node();
  void free_subtree(uint32_t idx);
  void reroot(uint32_t new_root);

  // Search internals
  void init_beam_from_root();
  void expand_one_depth(std::span<const PieceType> queue);
  void expand_beam_node(const BeamNode &parent,
                        std::span<const PieceType> queue);
  void try_piece(const BeamNode &parent, PieceType piece,
                 std::optional<PieceType> new_hold, bool hold_used_at_root,
                 int child_queue_idx, uint8_t child_bag,
                 float bag_weight = 1.0f);
  void draw_from_bag(const BeamNode &parent, int qi,
                     std::optional<PieceType> hold_piece);

  AIConfig config_;
  std::unique_ptr<Evaluator> eval_;

  // Arena
  static constexpr uint32_t kArenaSize = 65536;
  std::vector<TreeNode> arena_;
  uint32_t free_head_ = TreeNode::kNull;
  uint32_t arena_used_ = 0;

  // Tree state
  uint32_t root_idx_ = TreeNode::kNull;
  SearchState root_state_;

  // Search working set
  std::vector<BeamNode> beam_;
  std::vector<BeamNode> next_beam_;
  int search_depth_ = 0;
  float best_score_ = -1e18f;

  // Best result tracking
  uint32_t best_leaf_ = TreeNode::kNull;
  std::vector<std::pair<Placement, float>> root_scores_;
};
