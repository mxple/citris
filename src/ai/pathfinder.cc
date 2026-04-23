#include "ai/pathfinder.h"
#include "ai/movegen.h" // CollisionMap
#include "engine/srs.h"
#include <algorithm>
#include <climits>
#include <concepts>
#include <deque>
#include <type_traits>

namespace {

// A pathfinding oracle answers two questions:
//   collides(x, r, y) — would the piece at (x, r, y) collide / leave bounds?
//   is_filled(cx, cy) — is that specific board cell occupied? (Only used at
//                       termination to run the T-spin 3-corner test; see the
//                       match_spin block below.)
template <typename T>
concept CollisionOracle = requires(const T &o, int x, Rotation r, int y) {
  { o.collides(x, r, y) } -> std::same_as<bool>;
  { o.is_filled(x, y) } -> std::same_as<bool>;
};

// Oracle backed by a precomputed CollisionMap (O(1) bit-test) + BoardBitset
// (cell-level). Used by count_inputs, which already pays for the CollisionMap.
struct CollisionMapOracle {
  const CollisionMap &cm;
  const BoardBitset &board;
  bool collides(int x, Rotation r, int y) const {
    int xi = x + CollisionMap::kXShift;
    int bit = y + CollisionMap::kYShift;
    if (xi < 0 || xi >= CollisionMap::kXWidth || bit < 0 || bit >= 64)
      return true;
    return cm.data[xi][static_cast<int>(r)] & (uint64_t(1) << bit);
  }
  bool is_filled(int cx, int cy) const {
    if (cx < 0 || cx >= 10 || cy < 0 || cy >= 40)
      return true; // walls / floor / ceiling treated as filled
    return board.occupied(cx, cy);
  }
};

// Oracle backed by a live engine Board. Keeps find_path bit-for-bit
// identical to the engine's own collision rules.
struct BoardOracle {
  const Board &board;
  PieceType type;
  bool collides(int x, Rotation r, int y) const {
    return board.collides(Piece(type, r, x, y));
  }
  bool is_filled(int cx, int cy) const {
    return board.filled(cx, cy); // handles OOB as filled
  }
};

constexpr bool is_rotation_input(GameInput g) {
  return g == GameInput::RotateCW || g == GameInput::RotateCCW ||
         g == GameInput::Rotate180;
}

// Unified BFS. See pathfinder.h for the semantics of the template + flag
// parameters.
template <CollisionOracle Oracle, bool CollectPath>
auto pathfind_impl(const Oracle &oracle, const Placement &raw_target,
                   int start_x, int start_y, Rotation start_rot, bool allow_180,
                   bool match_spin, bool sonic_only)
    -> std::conditional_t<CollectPath, std::vector<GameInput>, int> {

  const Placement target = raw_target.canonical();

  constexpr int XS = 3;
  constexpr int XW = 14; // x in [-3, 10]; superset of CollisionMap's range
  constexpr int YS = 3;

  // Dual-visited: rotation arrivals tracked separately so a cheaper
  // translation arrival doesn't block a later rotation arrival.
  uint64_t visited[2][XW][4]{};

  struct Node {
    int8_t x;
    Rotation r;
    int8_t y;
    int cost;
    bool via_rot;
    int parent_idx;
    GameInput move;
  };

  std::vector<Node> nodes;
  std::deque<int> queue;

  auto mark = [&](int x, Rotation r, int y, bool via_rot) -> bool {
    int xi = x + XS;
    if (xi < 0 || xi >= XW)
      return false;
    int ybit = y + YS;
    if (ybit < 0 || ybit >= 64)
      return false;
    int ri = static_cast<int>(r);
    uint64_t bit = uint64_t(1) << ybit;
    int v = via_rot ? 1 : 0;
    if (visited[v][xi][ri] & bit)
      return false;
    visited[v][xi][ri] |= bit;
    return true;
  };

  auto ret_none = [&]() {
    if constexpr (CollectPath)
      return std::vector<GameInput>{};
    else
      return INT_MAX;
  };

  if (oracle.collides(start_x, start_rot, start_y))
    return ret_none();

  mark(start_x, start_rot, start_y, false);
  nodes.push_back({(int8_t)start_x, start_rot, (int8_t)start_y, 0, false, -1,
                   GameInput::HardDrop});
  queue.push_back(0);

  auto try_enqueue = [&](int x, Rotation r, int y, GameInput move, int parent,
                         int cost) {
    if (oracle.collides(x, r, y))
      return;
    bool via_rot = is_rotation_input(move);
    if (!mark(x, r, y, via_rot))
      return;
    int idx = static_cast<int>(nodes.size());
    nodes.push_back({(int8_t)x, r, (int8_t)y, cost, via_rot, parent, move});
    queue.push_back(idx);
  };

  auto drop_to = [&](int x, Rotation r, int y) -> int {
    while (!oracle.collides(x, r, y - 1))
      y--;
    return y;
  };

  while (!queue.empty()) {
    int idx = queue.front();
    queue.pop_front();
    int8_t x = nodes[idx].x;
    Rotation r = nodes[idx].r;
    int8_t y = nodes[idx].y;
    int cost = nodes[idx].cost;
    bool via_rot = nodes[idx].via_rot;

    int dy = drop_to(x, r, y);
    Placement cur{target.type, r, x, (int8_t)dy};
    bool cells_match = cur.canonical().same_landing(target);
    bool can_terminate = cells_match;
    if (can_terminate && match_spin) {
      // Spin target: engine's detect_spin only runs when the last input was
      // a rotation, so require via_rot. Movegen already verified the
      // position's spin criteria, so cell-match is sufficient for Mini vs
      // TSpin vs AllSpin — no need to re-run the classifier.
      //
      // None target: allow either arrival. For non-T this is always safe
      // (4-way immobility implies the position is only reachable via
      // rotation during movegen's flood-fill, so movegen would have
      // labeled it AllSpin — it can't be a None target). For T, however,
      // the 3-corner criterion is weaker than 4-way immobility, so a
      // 3-corner position can legitimately be reached via translation and
      // labeled None — and pathfinder can still find a separate via_rot
      // route that the engine would upgrade to Mini/TSpin. That's the T
      // edge case we have to block.
      bool wants_spin = (target.spin != SpinKind::None);
      if (wants_spin && !via_rot) {
        can_terminate = false;
      } else if (!wants_spin && via_rot && target.type == PieceType::T) {
        int corners = int(oracle.is_filled(x, dy + 2)) +
                      int(oracle.is_filled(x + 2, dy + 2)) +
                      int(oracle.is_filled(x, dy)) +
                      int(oracle.is_filled(x + 2, dy));
        if (corners >= 3)
          can_terminate = false;
      }
    }
    if (can_terminate) {
      if constexpr (CollectPath) {
        std::vector<GameInput> path;
        path.push_back(GameInput::HardDrop);
        for (int i = idx; i > 0; i = nodes[i].parent_idx)
          path.push_back(nodes[i].move);
        std::reverse(path.begin(), path.end());
        return path;
      } else {
        return cost + 1; // +1 for the hard drop
      }
    }

    int next_cost = cost + 1;

    try_enqueue(x - 1, r, y, GameInput::Left, idx, next_cost);
    try_enqueue(x + 1, r, y, GameInput::Right, idx, next_cost);

    // Vertical movement: atomic SoftDrop vs teleport SonicDrop.
    // sonic_only replaces y-by-y SoftDrop with a single SonicDrop edge
    // (mirrors movegen's sonic_only mode — useful when soft-drop tucks
    // should be off the table).
    if (!sonic_only)
      try_enqueue(x, r, y - 1, GameInput::SoftDrop, idx, next_cost);
    if constexpr (CollectPath) {
      int lx = x;
      while (!oracle.collides(lx - 1, r, y))
        lx--;
      if (lx != x)
        try_enqueue(lx, r, y, GameInput::LLeft, idx, next_cost);

      int rx = x;
      while (!oracle.collides(rx + 1, r, y))
        rx++;
      if (rx != x)
        try_enqueue(rx, r, y, GameInput::RRight, idx, next_cost);

      if (dy != y)
        try_enqueue(x, r, dy, GameInput::SonicDrop, idx, next_cost);
    } else if (sonic_only) {
      // Count-only + sonic_only: expose SonicDrop so vertical movement is
      // still possible without SoftDrop.
      if (dy != y)
        try_enqueue(x, r, dy, GameInput::SonicDrop, idx, next_cost);
    }

    // Rotations with SRS kicks. First non-colliding kick wins.
    auto try_rotate = [&](Rotation target_r, GameInput input,
                          const auto &kick_table) {
      int from_ri = static_cast<int>(r);
      for (auto &kick : kick_table[from_ri]) {
        int nx = x + kick.x;
        int ny = y + kick.y;
        if (oracle.collides(nx, target_r, ny))
          continue;
        if (!mark(nx, target_r, ny, true))
          return;
        int nidx = static_cast<int>(nodes.size());
        nodes.push_back(
            {(int8_t)nx, target_r, (int8_t)ny, next_cost, true, idx, input});
        queue.push_back(nidx);
        return;
      }
    };

    bool is_I = (target.type == PieceType::I);
    try_rotate(rotate_cw(r), GameInput::RotateCW,
               is_I ? kKickCW_I : kKickCW_JLSTZ);
    try_rotate(rotate_ccw(r), GameInput::RotateCCW,
               is_I ? kKickCCW_I : kKickCCW_JLSTZ);
    if (allow_180) {
      if (is_I)
        try_rotate(rotate_180(r), GameInput::Rotate180, kKick180_I);
      else
        try_rotate(rotate_180(r), GameInput::Rotate180, kKick180_JLSTZ);
    }
  }

  return ret_none();
}

} // namespace

int count_inputs(const BoardBitset &board, const Placement &target,
                          bool allow_180, bool match_spin, bool sonic_only) {
  CollisionMap cm(board, target.type);
  auto spawn = spawn_position(target.type);
  return pathfind_impl<CollisionMapOracle, /*CollectPath=*/false>(
      CollisionMapOracle{cm, board}, target, spawn.x, spawn.y, Rotation::North,
      allow_180, match_spin, sonic_only);
}

std::vector<GameInput> find_path(const Board &board, const Placement &target,
                                 int start_x, int start_y, Rotation start_rot,
                                 bool allow_180, bool match_spin,
                                 bool sonic_only) {
  return pathfind_impl<BoardOracle, /*CollectPath=*/true>(
      BoardOracle{board, target.type}, target, start_x, start_y, start_rot,
      allow_180, match_spin, sonic_only);
}
