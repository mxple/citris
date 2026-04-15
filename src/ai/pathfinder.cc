#include "ai/pathfinder.h"
#include "ai/movegen.h" // CollisionMap
#include "engine/srs.h"
#include <algorithm>
#include <climits>
#include <deque>
#include <queue>

InputSequence find_inputs(const BoardBitset &board, const Placement &target) {
  CollisionMap cm(board, target.type);

  static constexpr int XS = CollisionMap::kXShift;
  static constexpr int XW = CollisionMap::kXWidth;
  static constexpr int YS = CollisionMap::kYShift;

  // BFS state: searched[xi][rotation] = u64 bitboard of visited Y positions.
  // Bit b represents y = b - YS (same encoding as CollisionMap).
  uint64_t searched[XW][4]{};

  struct Node {
    int8_t x;
    Rotation r;
    int8_t y;
    int inputs;
  };

  std::deque<Node> queue;
  auto spawn = spawn_position(target.type);
  int spawn_x = spawn.x;
  int spawn_y = spawn.y;
  int sxi = spawn_x + XS;
  int spawn_bit = spawn_y + YS;

  // Check spawn is valid
  if ((cm.data[sxi][0] >> spawn_bit) & 1)
    return {INT_MAX};

  searched[sxi][0] |= uint64_t(1) << spawn_bit;
  queue.push_back({(int8_t)spawn_x, Rotation::North, (int8_t)spawn_y, 0});

  auto try_enqueue = [&](int x, Rotation r, int y, int inputs) {
    int xi = x + XS;
    if (xi < 0 || xi >= XW)
      return;
    int ybit = y + YS;
    if (ybit < 0 || ybit >= 64)
      return;
    int ri = static_cast<int>(r);
    uint64_t bit = uint64_t(1) << ybit;
    if (searched[xi][ri] & bit)
      return;
    if (cm.data[xi][ri] & bit)
      return;
    searched[xi][ri] |= bit;
    queue.push_back({(int8_t)x, r, (int8_t)y, inputs});
  };

  // Compute hard drop Y: lowest non-colliding Y in same column/rotation.
  auto hard_drop_y = [&](int x, Rotation r, int y) -> int {
    int xi = x + XS;
    int ri = static_cast<int>(r);
    uint64_t col_cm = cm.data[xi][ri];
    for (int dy = y; dy >= -YS; --dy) {
      int bit = dy + YS;
      if ((col_cm >> bit) & 1)
        return dy + 1;
    }
    return -YS;
  };

  while (!queue.empty()) {
    auto [x, r, y, inputs] = queue.front();
    queue.pop_front();

    // Check hard drop to target
    int drop_y = hard_drop_y(x, r, y);
    if (x == target.x && drop_y == target.y && r == target.rotation)
      return {inputs + 1}; // +1 for the hard drop

    // Shift left/right
    try_enqueue(x - 1, r, y, inputs + 1);
    try_enqueue(x + 1, r, y, inputs + 1);

    // Soft drop
    try_enqueue(x, r, y - 1, inputs + 1);

    // Rotations with SRS kicks
    auto try_rotate = [&](Rotation target_r, const auto &kick_table) {
      int from_ri = static_cast<int>(r);
      for (auto &kick : kick_table[from_ri]) {
        int nx = x + kick.x;
        int ny = y + kick.y;
        int nxi = nx + XS;
        if (nxi < 0 || nxi >= XW)
          continue;
        int nybit = ny + YS;
        if (nybit < 0 || nybit >= 64)
          continue;
        int tri = static_cast<int>(target_r);
        if ((cm.data[nxi][tri] >> nybit) & 1)
          continue;
        uint64_t bit = uint64_t(1) << nybit;
        if (searched[nxi][tri] & bit)
          continue;
        searched[nxi][tri] |= bit;
        queue.push_back({(int8_t)nx, target_r, (int8_t)ny, inputs + 1});
        return; // First successful kick wins (shortest path BFS)
      }
    };

    bool is_I = (target.type == PieceType::I);
    // CW
    try_rotate(rotate_cw(r), is_I ? kKickCW_I : kKickCW_JLSTZ);
    // CCW
    try_rotate(rotate_ccw(r), is_I ? kKickCCW_I : kKickCCW_JLSTZ);
    // 180
    try_rotate(rotate_180(r), is_I ? kKick180_I : kKick180_JLSTZ);
  }

  return {INT_MAX};
}

std::vector<GameInput> find_path(const Board &board, const Placement &target,
                                 int start_x, int start_y,
                                 Rotation start_rot) {
  PieceType type = target.type;

  // Collision check using game engine directly.
  auto collides = [&](int x, Rotation r, int y) {
    return board.collides(Piece(type, r, x, y));
  };

  // Visited state: bitboard per (x_shifted, rotation).
  static constexpr int XS = 3;
  static constexpr int XW = 14; // x from -3 to 10
  static constexpr int YS = 3;
  uint64_t visited[XW][4]{};

  auto mark = [&](int x, Rotation r, int y) -> bool {
    int xi = x + XS;
    if (xi < 0 || xi >= XW)
      return false;
    int ybit = y + YS;
    if (ybit < 0 || ybit >= 64)
      return false;
    int ri = static_cast<int>(r);
    uint64_t bit = uint64_t(1) << ybit;
    if (visited[xi][ri] & bit)
      return false;
    visited[xi][ri] |= bit;
    return true;
  };

  struct Node {
    int8_t x;
    Rotation r;
    int8_t y;
    int parent_idx;
    GameInput move;
    int cost;
  };

  std::vector<Node> nodes;
  auto cmp = [&](int a, int b) { return nodes[a].cost > nodes[b].cost; };
  std::priority_queue<int, std::vector<int>, decltype(cmp)> pq(cmp);

  if (collides(start_x, start_rot, start_y))
    return {};

  mark(start_x, start_rot, start_y);
  nodes.push_back({(int8_t)start_x, start_rot, (int8_t)start_y, -1,
                   GameInput::HardDrop, 0});
  pq.push(0);

  auto try_enqueue = [&](int x, Rotation r, int y, GameInput move, int parent,
                         int cost) {
    if (collides(x, r, y))
      return;
    if (!mark(x, r, y))
      return;
    int new_idx = static_cast<int>(nodes.size());
    nodes.push_back({(int8_t)x, r, (int8_t)y, parent, move, cost});
    pq.push(new_idx);
  };

  // Drop to lowest valid Y (same as Game::compute_ghost).
  auto drop_y = [&](int x, Rotation r, int y) -> int {
    while (!collides(x, r, y - 1))
      y--;
    return y;
  };

  while (!pq.empty()) {
    int idx = pq.top();
    pq.pop();
    auto [x, r, y, pi, mv, cost] = nodes[idx];

    int dy = drop_y(x, r, y);
    if (x == target.x && dy == target.y && r == target.rotation) {
      std::vector<GameInput> path;
      path.push_back(GameInput::HardDrop);
      for (int i = idx; i > 0; i = nodes[i].parent_idx)
        path.push_back(nodes[i].move);
      std::reverse(path.begin(), path.end());
      return path;
    }

    int next_cost = cost + 1;

    // Fine positioning
    try_enqueue(x - 1, r, y, GameInput::Left, idx, next_cost);
    try_enqueue(x + 1, r, y, GameInput::Right, idx, next_cost);
    try_enqueue(x, r, y - 1, GameInput::SoftDrop, idx, next_cost);

    // Slide to wall
    {
      int lx = x;
      while (!collides(lx - 1, r, y))
        lx--;
      if (lx != x)
        try_enqueue(lx, r, y, GameInput::LLeft, idx, next_cost);

      int rx = x;
      while (!collides(rx + 1, r, y))
        rx++;
      if (rx != x)
        try_enqueue(rx, r, y, GameInput::RRight, idx, next_cost);
    }

    // Sonic drop
    if (dy != y)
      try_enqueue(x, r, dy, GameInput::SonicDrop, idx, next_cost);

    // Rotations with SRS kicks
    auto try_rotate = [&](Rotation target_r, GameInput input,
                          const auto &kick_table) {
      int from_ri = static_cast<int>(r);
      for (auto &kick : kick_table[from_ri]) {
        int nx = x + kick.x;
        int ny = y + kick.y;
        if (collides(nx, target_r, ny))
          continue;
        if (!mark(nx, target_r, ny))
          return; // already visited via shorter path
        int new_idx = static_cast<int>(nodes.size());
        nodes.push_back(
            {(int8_t)nx, target_r, (int8_t)ny, idx, input, next_cost});
        pq.push(new_idx);
        return;
      }
    };

    bool is_I = (type == PieceType::I);
    try_rotate(rotate_cw(r), GameInput::RotateCW,
               is_I ? kKickCW_I : kKickCW_JLSTZ);
    try_rotate(rotate_ccw(r), GameInput::RotateCCW,
               is_I ? kKickCCW_I : kKickCCW_JLSTZ);
    try_rotate(rotate_180(r), GameInput::Rotate180,
               is_I ? kKick180_I : kKick180_JLSTZ);
  }

  return {};
}
