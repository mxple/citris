#include "ai/ai.h"
#include "ai/movegen.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

AI::Builder &AI::Builder::width(int w) { config_.beam_width = w; return *this; }
AI::Builder &AI::Builder::depth(int d) { config_.max_depth = d; return *this; } AI::Builder &AI::Builder::futility(float d) { config_.futility_delta = d; return *this; }
AI::Builder &AI::Builder::sonic(bool s) { config_.sonic_only = s; return *this; }
AI::Builder &AI::Builder::extend_bag(bool e) { config_.extend_queue_7bag = e; return *this; }
AI::Builder &AI::Builder::evaluator(std::unique_ptr<Evaluator> e) {
  eval_ = std::move(e);
  return *this;
}

AI AI::Builder::build() {
  AI ai;
  ai.config_ = config_;
  ai.eval_ = std::move(eval_);
  return ai;
}

AI::Builder AI::builder() { return Builder{}; }

AI::AI() = default;
AI::AI(AI &&) noexcept = default;
AI &AI::operator=(AI &&) noexcept = default;
AI::~AI() = default;

void AI::reset(SearchState root) {
  root_state_ = std::move(root);
  arena_.clear();
  arena_.resize(kArenaSize);
  arena_used_ = 0;
  free_head_ = TreeNode::kNull;
  root_idx_ = TreeNode::kNull;
  best_leaf_ = TreeNode::kNull;
  best_score_ = -1e18f;
  root_scores_.clear();
  beam_.clear();
  next_beam_.clear();
  search_depth_ = 0;
}

bool AI::advance(Placement move, bool hold) {
  if (root_idx_ == TreeNode::kNull)
    return false;

  // Find matching child of root
  auto &root = arena_[root_idx_];
  uint32_t child = root.first_child;
  while (child != TreeNode::kNull) {
    auto &node = arena_[child];
    if (node.move == move && node.hold_used == hold) {
      reroot(child);
      return true;
    }
    child = node.next_sibling;
  }
  return false;
}

void AI::set_evaluator(std::unique_ptr<Evaluator> eval) {
  eval_ = std::move(eval);
}

void AI::set_config(AIConfig config) {
  config_ = config;
}

// ---------------------------------------------------------------------------
// Arena management
// ---------------------------------------------------------------------------

uint32_t AI::alloc_node() {
  if (free_head_ != TreeNode::kNull) {
    uint32_t idx = free_head_;
    free_head_ = arena_[idx].next_sibling;
    arena_[idx] = TreeNode{};
    return idx;
  }
  if (arena_used_ < kArenaSize) {
    uint32_t idx = arena_used_++;
    arena_[idx] = TreeNode{};
    return idx;
  }
  // Arena full — expand (shouldn't happen with 64k nodes)
  arena_.push_back(TreeNode{});
  return arena_used_++;
}

void AI::free_subtree(uint32_t idx) {
  if (idx == TreeNode::kNull)
    return;
  // Iterative post-order traversal via stack
  std::vector<uint32_t> stack;
  stack.push_back(idx);
  while (!stack.empty()) {
    uint32_t n = stack.back();
    stack.pop_back();
    auto &node = arena_[n];
    uint32_t child = node.first_child;
    while (child != TreeNode::kNull) {
      stack.push_back(child);
      child = arena_[child].next_sibling;
    }
    // Return to free list
    node.next_sibling = free_head_;
    free_head_ = n;
  }
}

void AI::reroot(uint32_t new_root) {
  // Free all siblings of new_root and the old root
  auto &old_root = arena_[root_idx_];
  uint32_t child = old_root.first_child;
  while (child != TreeNode::kNull) {
    uint32_t next = arena_[child].next_sibling;
    if (child != new_root)
      free_subtree(child);
    child = next;
  }
  // Free old root node itself
  arena_[root_idx_].next_sibling = free_head_;
  free_head_ = root_idx_;

  root_idx_ = new_root;
  arena_[new_root].parent = TreeNode::kNull;
  arena_[new_root].next_sibling = TreeNode::kNull;

  // Update root state by replaying the move
  root_state_.board.place(arena_[new_root].move.type,
                          arena_[new_root].move.rotation,
                          arena_[new_root].move.x,
                          arena_[new_root].move.y);
  int cleared = root_state_.board.clear_lines();
  root_state_.lines_cleared += cleared;
  int atk = compute_attack_and_update_state(
      root_state_.attack, cleared, arena_[new_root].move.spin);
  root_state_.total_attack += atk;
  root_state_.depth++;

  if (arena_[new_root].hold_used) {
    // Hold was used — swap hold
    auto old_hold = root_state_.hold;
    // The piece that was held is the move's type on the pre-hold queue
    // For simplicity, just track that hold was used
    root_state_.hold_available = true;
  }

  best_leaf_ = TreeNode::kNull;
  best_score_ = -1e18f;
  root_scores_.clear();
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

// Compute which pieces remain in the current 7-bag after the known queue is
// exhausted.  bag_draws = total next() calls so far (from Game).  queue[0] is
// the current piece (already drawn); queue[1..] are preview pieces (next draws).
// After the full queue, total draws = bag_draws + queue.size() - 1.
// The last (total_draws % 7) entries of the queue belong to the current
// partial bag.
static uint8_t compute_initial_bag(std::span<const PieceType> queue,
                                   int bag_draws) {
  if (queue.empty())
    return 0x7F;
  int total_draws = bag_draws + static_cast<int>(queue.size()) - 1;
  int tail = total_draws % 7;
  if (tail == 0)
    return 0x7F; // queue ends exactly at a bag boundary
  uint8_t bag_used = 0;
  int start = static_cast<int>(queue.size()) - tail;
  for (int i = start; i < static_cast<int>(queue.size()); ++i)
    bag_used |= uint8_t(1) << static_cast<int>(queue[i]);
  return uint8_t(0x7F & ~bag_used);
}

void AI::init_beam_from_root() {
  beam_.clear();
  next_beam_.clear();
  search_depth_ = 0;
  best_score_ = -1e18f;
  best_leaf_ = TreeNode::kNull;
  root_scores_.clear();

  // Ensure root tree node exists
  if (root_idx_ == TreeNode::kNull) {
    root_idx_ = alloc_node();
    arena_[root_idx_].parent = TreeNode::kNull;
    arena_[root_idx_].first_child = TreeNode::kNull;
    arena_[root_idx_].next_sibling = TreeNode::kNull;
  }

  BeamNode root_beam;
  root_beam.state = root_state_;
  root_beam.score = 0.0f;
  root_beam.tree_idx = root_idx_;
  root_beam.root_move = {};
  root_beam.root_hold_used = false;
  beam_.push_back(std::move(root_beam));
}

static int estimate_inputs(PieceType type, const Placement &m) {
  int spawn_x = (type == PieceType::O) ? 4 : 3;
  int rot_cost = (m.rotation == Rotation::North)  ? 0
                 : (m.rotation == Rotation::South) ? 2
                                                   : 1;
  int dx = std::abs(m.x - spawn_x);
  return dx + rot_cost + 1;
}

void AI::try_piece(const BeamNode &parent, PieceType piece,
                   std::optional<PieceType> new_hold, bool hold_used_at_root,
                   int child_queue_idx, uint8_t child_bag,
                   float bag_weight) {
  MoveBuffer moves;
  generate_moves(parent.state.board, moves, piece, config_.sonic_only);

  for (auto &m : moves) {
    BoardBitset board = parent.state.board;
    board.place(m.type, m.rotation, m.x, m.y);

    int cleared = board.clear_lines();

    SearchState cs;
    cs.board = board;
    cs.used_counts = parent.state.used_counts;
    cs.used_counts[static_cast<int>(piece)]++;

    AttackState atk_state = parent.state.attack;
    int atk = compute_attack_and_update_state(atk_state, cleared, m.spin);

    cs.board = board;
    cs.attack = atk_state;
    cs.lines_cleared = parent.state.lines_cleared + cleared;
    cs.total_attack = parent.state.total_attack + atk;
    cs.input_count = parent.state.input_count + estimate_inputs(piece, m);
    cs.depth = parent.state.depth + 1;
    cs.hold = new_hold;
    cs.hold_available = true;
    cs.bag_remaining = child_bag;
    cs.queue_idx = child_queue_idx;

    // Evaluate
    float board_score = eval_->board_eval(board);
    float tactical = eval_->tactical_eval(m, cleared, atk, parent.state);
    float cum_tactical = parent.cumulative_tactical + tactical;
    float eff_tactical =
        eval_->accumulate_tactical() ? cum_tactical : tactical;
    float total = eval_->composite(board_score, eff_tactical, cs.depth);

    // Link tree node
    uint32_t ci = alloc_node();
    auto &tn = arena_[ci];
    tn.move = m;
    tn.score = total;
    tn.parent = parent.tree_idx;
    tn.first_child = TreeNode::kNull;
    tn.next_sibling = arena_[parent.tree_idx].first_child;
    arena_[parent.tree_idx].first_child = ci;
    arena_[parent.tree_idx].child_count++;
    tn.flags = 0;
    tn.hold_used = hold_used_at_root;
    tn.lines_cleared = static_cast<int8_t>(cleared);
    tn.attack = static_cast<int8_t>(atk);
    tn.qi = static_cast<uint8_t>(child_queue_idx);

    float child_bag_weight = parent.bag_weight * bag_weight;
    float eff_score = total * child_bag_weight;
    if (eff_score > best_score_) {
      best_score_ = eff_score;
      best_leaf_ = ci;
    }

    // Beam node — propagate root move identity for root_scores tracking
    bool at_root = (parent.tree_idx == root_idx_);
    next_beam_.push_back({
        .state = std::move(cs),
        .score = total,
        .cumulative_tactical = cum_tactical,
        .bag_weight = child_bag_weight,
        .tree_idx = ci,
        .root_move = at_root ? m : parent.root_move,
        .root_hold_used = at_root ? hold_used_at_root : parent.root_hold_used,
    });
  }
}

// Try all placements for a piece drawn from the bag at `parent`'s position.
// Covers: play direct, swap with hold, and (if hold empty) hold + draw again.
void AI::draw_from_bag(const BeamNode &parent, int qi,
                       std::optional<PieceType> hold_piece) {
  uint8_t bag = parent.state.bag_remaining;
  int k = std::popcount(static_cast<unsigned>(bag));
  float w = k > 0 ? 1.0f / k : 1.0f;

  for (int i = 0; i < 7; ++i) {
    if (!(bag & (1 << i)))
      continue;
    auto piece = static_cast<PieceType>(i);
    uint8_t cb = bag & ~uint8_t(1 << i);
    if (cb == 0)
      cb = 0x7F;

    try_piece(parent, piece, hold_piece, false, qi + 1, cb, w);

    if (hold_piece && *hold_piece != piece)
      try_piece(parent, *hold_piece, piece, true, qi + 1, cb, w);

    if (!hold_piece) {
      // Hold this draw, draw again for the play piece
      int k2 = std::popcount(static_cast<unsigned>(cb));
      float w2 = k2 > 0 ? 1.0f / k2 : 1.0f;
      for (int j = 0; j < 7; ++j) {
        if (!(cb & (1 << j)))
          continue;
        uint8_t cb2 = cb & ~uint8_t(1 << j);
        if (cb2 == 0)
          cb2 = 0x7F;
        try_piece(parent, static_cast<PieceType>(j), piece, true, qi + 2, cb2,
                  w * w2);
      }
    }
  }
}

void AI::expand_beam_node(const BeamNode &parent,
                          std::span<const PieceType> queue) {
  int qi = parent.state.queue_idx;
  auto hold = parent.state.hold;

  if (qi < (int)queue.size()) {
    PieceType piece = queue[qi];
    int nqi = qi + 1;
    uint8_t bag = parent.state.bag_remaining;

    try_piece(parent, piece, hold, false, nqi, bag);
    if (hold && *hold != piece)
      try_piece(parent, *hold, piece, true, nqi, bag);

    // Empty hold: hold queue piece, play next from queue or bag
    if (!hold) {
      if (nqi < (int)queue.size())
        try_piece(parent, queue[nqi], piece, true, nqi + 1, bag);
      else if (config_.extend_queue_7bag)
        draw_from_bag(parent, nqi, piece);
    }
  } else if (hold) {
    try_piece(parent, *hold, std::nullopt, true, qi,
              parent.state.bag_remaining);
  } else if (config_.extend_queue_7bag) {
    draw_from_bag(parent, qi, hold);
  }
}

static uint64_t hash_board_hold(const BoardBitset &b,
                                std::optional<PieceType> hold) {
  uint64_t h = 0;
  for (int i = 0; i < BoardBitset::kWidth; ++i)
    h ^= std::rotl(b.cols[i], i * 7);
  h ^= uint64_t(hold.has_value() ? static_cast<int>(*hold) + 1 : 0) << 56;
  return h;
}

void AI::expand_one_depth(std::span<const PieceType> queue) {
  next_beam_.clear();

  for (auto &node : beam_)
    expand_beam_node(node, queue);

  // Deduplicate by board + hold — keep highest effective score per state
  {
    std::unordered_map<uint64_t, size_t> best_idx;
    best_idx.reserve(next_beam_.size());
    for (size_t i = 0; i < next_beam_.size(); ++i) {
      uint64_t h = hash_board_hold(next_beam_[i].state.board,
                                   next_beam_[i].state.hold);
      auto [it, inserted] = best_idx.emplace(h, i);
      if (!inserted) {
        size_t prev = it->second;
        if (next_beam_[i].effective_score() >
            next_beam_[prev].effective_score())
          it->second = i;
      }
    }
    if (best_idx.size() < next_beam_.size()) {
      std::vector<BeamNode> deduped;
      deduped.reserve(best_idx.size());
      for (auto &[_, idx] : best_idx)
        deduped.push_back(std::move(next_beam_[idx]));
      next_beam_ = std::move(deduped);
    }
  }

  // Futility pruning (on effective score — discounts speculative branches)
  if (!next_beam_.empty()) {
    float best = next_beam_[0].effective_score();
    for (auto &n : next_beam_)
      best = std::max(best, n.effective_score());
    float threshold = best - config_.futility_delta;
    std::erase_if(next_beam_, [threshold](const BeamNode &n) {
      return n.effective_score() < threshold;
    });
  }

  // Sort and truncate
  std::sort(next_beam_.begin(), next_beam_.end(),
            [](const BeamNode &a, const BeamNode &b) {
              float sa = a.effective_score(), sb = b.effective_score();
              if (sa != sb)
                return sa > sb;
              return a.state.input_count < b.state.input_count;
            });
  if ((int)next_beam_.size() > config_.beam_width)
    next_beam_.resize(config_.beam_width);

  // Collect best score per root move — updated every depth so that
  // root_scores_ always reflects the best descendant, not just depth-1.
  // next_beam_ is sorted by score desc, so first hit per move wins.
  {
    root_scores_.clear();
    std::unordered_set<uint64_t> seen;
    for (auto &n : next_beam_) {
      auto cells = n.root_move.cells();
      uint64_t key = uint64_t(n.root_hold_used) << 52;
      for (int i = 0; i < 4; ++i)
        key ^= uint64_t(cells[i].y * 10 + cells[i].x) << (i * 13);
      if (seen.insert(key).second)
        root_scores_.push_back({n.root_move, n.effective_score()});
    }
  }

  // Prune dead leaves — free children that didn't survive beam selection
  std::unordered_set<uint32_t> survivors;
  survivors.reserve(next_beam_.size());
  for (auto &n : next_beam_)
    survivors.insert(n.tree_idx);

  for (auto &parent : beam_) {
    uint32_t pidx = parent.tree_idx;
    uint32_t prev = TreeNode::kNull;
    uint32_t child = arena_[pidx].first_child;
    uint16_t kept = 0;
    while (child != TreeNode::kNull) {
      uint32_t next = arena_[child].next_sibling;
      if (survivors.contains(child)) {
        prev = child;
        kept++;
      } else {
        // Unlink and free
        if (prev == TreeNode::kNull)
          arena_[pidx].first_child = next;
        else
          arena_[prev].next_sibling = next;
        arena_[child].next_sibling = free_head_;
        free_head_ = child;
      }
      child = next;
    }
    arena_[pidx].child_count = kept;
  }

  beam_ = std::move(next_beam_);
  search_depth_++;
}

void AI::run_search(std::span<const PieceType> queue,
                    const std::atomic<bool> &cancel) {
  if (!eval_)
    return;

  if (config_.extend_queue_7bag)
    root_state_.bag_remaining = compute_initial_bag(queue, root_state_.bag_draws);

  init_beam_from_root();

  for (int d = 0; d < config_.max_depth && !beam_.empty(); ++d) {
    expand_one_depth(queue);
    if (cancel.load(std::memory_order_relaxed))
      break;
  }
}

// --- Query ---

SearchResult AI::result() const {
  SearchResult r;
  if (best_leaf_ == TreeNode::kNull)
    return r;

  r.score = best_score_;
  r.root_scores = root_scores_;

  // Walk back from best leaf to root to extract PV
  std::vector<Placement> pv;
  uint32_t n = best_leaf_;
  bool first_hold = false;
  while (n != TreeNode::kNull && n != root_idx_) {
    pv.push_back(arena_[n].move);
    first_hold = arena_[n].hold_used;
    n = arena_[n].parent;
  }
  std::reverse(pv.begin(), pv.end());

  if (!pv.empty()) {
    r.best_move = pv[0];
    r.hold_used = first_hold;
  }
  r.pv = std::move(pv);
  return r;
}

MoveRating AI::rate(Placement player_move) const {
  MoveRating mr{};
  mr.rank = -1;
  mr.player_score = -1e18f;
  mr.best_score = -1e18f;

  if (root_scores_.empty())
    return mr;

  mr.best_score = root_scores_[0].second;
  mr.best_move = root_scores_[0].first;

  for (int i = 0; i < (int)root_scores_.size(); ++i) {
    if (root_scores_[i].first == player_move) {
      mr.rank = i;
      mr.player_score = root_scores_[i].second;
      break;
    }
  }

  mr.delta = mr.best_score - mr.player_score;
  return mr;
}

bool AI::searching() const { return !beam_.empty(); }
int AI::depth() const { return search_depth_; }
const SearchState &AI::root_state() const { return root_state_; }
