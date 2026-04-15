#include "ai/movegen.h"
#include "engine/srs.h"
#include <bit>
#include <cstdio>

// Set to true + fill in target to trace how a specific placement is reached.
static constexpr bool kTrace = false;
static constexpr int kTraceXI = 5;  // target xi (x + XS) = 2+3
static constexpr int kTraceRI = 3;  // target rotation index (West)
static constexpr int kTraceBit = 3; // target bit (y + YS) = 0+3

static const char *dbg_rot(int ri) {
  constexpr const char *n[] = {"N", "E", "S", "W"};
  return n[ri];
}

// --- Helpers ---

static bool in_bounds(PieceType type, Rotation rot, int col) {
  const auto &cells =
      kPieceCells[static_cast<int>(type)][static_cast<int>(rot)];
  for (auto &c : cells) {
    if (col + c.x < 0 || col + c.x >= 10)
      return false;
  }
  return true;
}

static bool is_group2(PieceType type) {
  return type == PieceType::I || type == PieceType::S || type == PieceType::Z;
}

static int canonical_count(PieceType type) {
  switch (type) {
  case PieceType::O:
    return 1;
  case PieceType::I:
  case PieceType::S:
  case PieceType::Z:
    return 2;
  default:
    return 4;
  }
}

static constexpr int XS = CollisionMap::kXShift;
static constexpr int XW = CollisionMap::kXWidth;
static constexpr int YS = CollisionMap::kYShift;

// --- CollisionMap construction ---

CollisionMap::CollisionMap(const BoardBitset &board, PieceType type) {
  int ti = static_cast<int>(type);

  for (int xi = 0; xi < kXWidth; ++xi) {
    int col = xi - kXShift;
    for (int r = 0; r < 4; ++r) {
      const auto &cells = kPieceCells[ti][r];

      bool oob = false;
      for (auto &c : cells) {
        if (col + c.x < 0 || col + c.x >= 10) {
          oob = true;
          break;
        }
      }
      if (oob) {
        data[xi][r] = ~uint64_t(0);
        continue;
      }

      // Build collision bitmask with Y shift.
      // Bit b represents piece at y = b - kYShift.
      // Cell (dx, dy) occupies board row (b - kYShift + dy) = b + (dy -
      // kYShift). Let net = dy - kYShift.
      //   net >= 0: result |= cols[cx] >> net
      //   net < 0:  result |= ~(~cols[cx] << (-net))
      //     The ~(~col << n) trick encodes: occupancy shifted up by n bits,
      //     PLUS bits 0..n-1 set as floor collision (board row < 0).
      uint64_t result = 0;
      for (auto &c : cells) {
        int cx = col + c.x;
        int net = c.y - kYShift;
        uint64_t col_bits = board.cols[cx];
        if (net >= 0) {
          result |= col_bits >> net;
        } else if (net > -64) {
          result |= ~(~col_bits << (-net));
        } else {
          result |= ~uint64_t(0);
        }
      }

      // Mark bits above max useful range as blocked
      if (kMaxBit < 64)
        result |= ~uint64_t(0) << kMaxBit;

      data[xi][r] = result;
    }
  }
}

// --- Spin detection ---

// Allspin immobility check: piece is a spin if it cannot move in any of the
// 4 cardinal directions.
static SpinKind detect_allspin(const CollisionMap &cm, int xi, int ri,
                               int bit) {
  // Check if piece can move down
  if (bit > 0 && !(cm.data[xi][ri] & (uint64_t(1) << (bit - 1))))
    return SpinKind::None;

  // Check if piece can move up
  if (bit + 1 < CollisionMap::kMaxBit &&
      !(cm.data[xi][ri] & (uint64_t(1) << (bit + 1))))
    return SpinKind::None;

  // Check if piece can move left
  if (xi > 0 && !(cm.data[xi - 1][ri] & (uint64_t(1) << bit)))
    return SpinKind::None;

  // Check if piece can move right
  if (xi + 1 < XW && !(cm.data[xi + 1][ri] & (uint64_t(1) << bit)))
    return SpinKind::None;

  return SpinKind::AllSpin;
}

// --- Flood-fill move generation ---

void generate_moves(const BoardBitset &board, MoveBuffer &out, PieceType type,
                    bool sonic_only) {
  out.clear();

  CollisionMap cm(board, type);

  // Precompute T-spin corner bitboards. For xi, the piece's left column is
  // px = xi - XS and right column is px+2. Bit b = py + YS, so:
  //   TL (px, py+2): board row b-YS+2 = b-1 → (cols[px] << 1) | 0x1
  //   TR (px+2, py+2): same shift on right col
  //   BL (px, py):   board row b-YS   = b-3 → (cols[px] << 3) | 0x7
  //   BR (px+2, py):  same shift on right col
  // Wall/floor columns use ~0 (always filled).
  uint64_t spin_3corner[XW]{};
  uint64_t spin_front[XW][4]{};
  if (type == PieceType::T) {
    for (int xi = 0; xi < XW; ++xi) {
      int px = xi - XS;
      int pr = px + 2;
      auto col = [&](int cx) -> uint64_t {
        if (cx < 0 || cx >= 10)
          return ~uint64_t(0);
        return board.cols[cx];
      };
      uint64_t tl = (col(px) << 1) | uint64_t(0x1);
      uint64_t tr = (col(pr) << 1) | uint64_t(0x1);
      uint64_t bl = (col(px) << 3) | uint64_t(0x7);
      uint64_t br = (col(pr) << 3) | uint64_t(0x7);
      spin_3corner[xi] = (tl & tr & (bl | br)) | (bl & br & (tl | tr));
      spin_front[xi][0] = tl & tr; // North
      spin_front[xi][1] = tr & br; // East
      spin_front[xi][2] = bl & br; // South
      spin_front[xi][3] = tl & bl; // West
    }
  }

  uint64_t to_search[XW][4]{};
  uint64_t searched[XW][4]{};
  uint64_t move_set[XW][4]{};
  uint64_t last_rotated[XW]
                       [4]{}; // bit set if position was reached via rotation
  uint64_t remaining = 0;

  // Pre-fill searched with collision map (blocked positions)
  for (int xi = 0; xi < XW; ++xi)
    for (int ri = 0; ri < 4; ++ri)
      searched[xi][ri] = cm.data[xi][ri];

  auto spawn = spawn_position(type);
  int spawn_x = spawn.x;
  int spawn_y = spawn.y;
  int spawn_bit = spawn_y + YS;
  int max_h = board.max_height();
  int can_count = canonical_count(type);

  if (max_h <= spawn_y - 3) {
    // Normal path: seed all reachable (col, rot) surface positions.
    // Iterate over the extended x range to catch negative BB-corner positions
    // (e.g., I-East at x=-2 places cells at column 0).
    for (int x = -XS; x <= 9; ++x) {
      int xi = x + XS;
      for (int ri = 0; ri < can_count; ++ri) {
        auto rot = static_cast<Rotation>(ri);
        if (!in_bounds(type, rot, x))
          continue;

        uint64_t collision = cm.data[xi][ri];
        // Surface: bits in [top_of_stack, spawn_bit) that are collision-free.
        // Mask to bits below spawn to ignore sentinel bits above kMaxBit.
        uint64_t spawn_mask =
            spawn_bit < 64 ? (uint64_t(1) << spawn_bit) - 1 : ~uint64_t(0);
        uint64_t relevant = collision & spawn_mask;
        int y = relevant == 0 ? 0 : 64 - std::countl_zero(relevant);
        uint64_t floor_mask =
            y > 0 && y < 64 ? ~((uint64_t(1) << y) - 1) : ~uint64_t(0);
        uint64_t surface = spawn_mask & floor_mask & ~collision;

        if (surface) {
          searched[xi][ri] |= surface;
          to_search[xi][ri] = surface;
          remaining |= uint64_t(1) << (xi * 4 + ri);
          if (kTrace && xi == kTraceXI && ri == kTraceRI &&
              (surface & (uint64_t(1) << kTraceBit)))
            std::fprintf(stderr,
                         "[trace] SEED x=%d %s bit=%d  "
                         "collision=0x%lx y=%d floor=0x%lx spawn=0x%lx "
                         "surface=0x%lx\n",
                         x, dbg_rot(ri), kTraceBit, (unsigned long)collision, y,
                         (unsigned long)floor_mask, (unsigned long)spawn_mask,
                         (unsigned long)surface);
        }
      }
    }
  } else {
    // Slow path: seed only from exact spawn position
    int sxi = spawn_x + XS;
    uint64_t spawn_b = uint64_t(1) << spawn_bit;
    if (!(cm.data[sxi][0] & spawn_b)) {
      to_search[sxi][0] = spawn_b;
      searched[sxi][0] |= spawn_b;
      remaining |= uint64_t(1) << (sxi * 4);
    } else {
      return;
    }
  }

  // BFS loop
  while (remaining != 0) {
    int index = std::countr_zero(remaining);
    int xi = index >> 2;
    int ri = index & 3;
    int x = xi - XS;
    auto rot = static_cast<Rotation>(ri);

    if (!sonic_only) {
      // 1. Flood-fill downward (soft drop propagation)
      uint64_t m =
          (to_search[xi][ri] >> 1) & ~to_search[xi][ri] & ~searched[xi][ri];
      while (m) {
        to_search[xi][ri] |= m;
        m = (m >> 1) & ~to_search[xi][ri] & ~searched[xi][ri];
      }

      // 2a. Extract grounded moves (all positions above a collision)
      uint64_t col_cm = cm.data[xi][ri];
      uint64_t grounded = to_search[xi][ri] & ((col_cm << 1) | 1);
      if (grounded) {
        bool g2 = is_group2(type);
        uint64_t bits = grounded;
        while (bits) {
          int b = std::countr_zero(bits);
          bits &= bits - 1;
          // Map to canonical rotation for group2 pieces
          int cxi = xi, cri = ri, cb = b;
          if (g2) {
            if (ri == 2) {
              cri = 0;
              cb = b - 1;
            } else if (ri == 3) {
              cri = 1;
              cxi = xi - 1;
            }
            if (cb < 0 || cxi < 0)
              continue;
          }
          if (move_set[cxi][cri] & (uint64_t(1) << cb))
            continue;
          move_set[cxi][cri] |= uint64_t(1) << cb;
          int y = b - YS;
          SpinKind spin = SpinKind::None;
          if (last_rotated[xi][ri] & (uint64_t(1) << b)) {
            if (type == PieceType::T) {
              uint64_t bm = uint64_t(1) << b;
              if (spin_3corner[xi] & bm)
                spin = (spin_front[xi][ri] & bm) ? SpinKind::TSpin
                                                 : SpinKind::Mini;
            } else {
              spin = detect_allspin(cm, xi, ri, b);
            }
          }
          auto canon_rot = static_cast<Rotation>(cri);
          int cx = cxi - XS;
          int cy = cb - YS;
          out.push(Placement{type, canon_rot, (int8_t)cx, (int8_t)cy, spin});
        }
      }
    } else {
      // 2b. Sonic drop: for each position, hard-drop to the landing spot.
      // No soft-drop propagation — only the final landing is emitted.
      bool g2 = false && is_group2(type); // DISABLED for testing
      uint64_t col_cm = cm.data[xi][ri];
      uint64_t bits = to_search[xi][ri];
      while (bits) {
        int b = std::countr_zero(bits);
        bits &= bits - 1;
        // Highest collision bit below b determines the landing
        uint64_t below = col_cm & ((uint64_t(1) << b) - 1);
        int landing = below ? (63 - std::countl_zero(below) + 1) : 0;
        // Map to canonical rotation for group2 pieces
        int cxi = xi, cri = ri, cl = landing;
        if (g2) {
          if (ri == 2) {
            cri = 0;
            cl = landing - 1;
          } else if (ri == 3) {
            cri = 1;
            cxi = xi - 1;
          }
          if (cl < 0 || cxi < 0)
            continue;
        }
        if (move_set[cxi][cri] & (uint64_t(1) << cl))
          continue;
        move_set[cxi][cri] |= uint64_t(1) << cl;
        int y = landing - YS;
        // Spin only applies at the exact rotated position, not after drop
        SpinKind spin = SpinKind::None;
        if (landing == b && (last_rotated[xi][ri] & (uint64_t(1) << landing))) {
          if (type == PieceType::T) {
            uint64_t bm = uint64_t(1) << landing;
            if (spin_3corner[xi] & bm)
              spin =
                  (spin_front[xi][ri] & bm) ? SpinKind::TSpin : SpinKind::Mini;
          } else {
            spin = detect_allspin(cm, xi, ri, landing);
          }
        }
        auto canon_rot = static_cast<Rotation>(cri);
        int cx = cxi - XS;
        int cy = cl - YS;
        out.push(Placement{type, canon_rot, (int8_t)cx, (int8_t)cy, spin});
      }
    }

    // 3. Mark searched, save current, clear to_search
    searched[xi][ri] |= to_search[xi][ri];
    uint64_t current = to_search[xi][ri];
    to_search[xi][ri] = 0;
    remaining &= ~(uint64_t(1) << index);

    // 4. Horizontal shift
    for (int dx : {-1, 1}) {
      int nx = x + dx;
      int nxi = nx + XS;
      if (nxi < 0 || nxi >= XW)
        continue;
      uint64_t m = current & ~searched[nxi][ri] & ~cm.data[nxi][ri];
      if (m) {
        if (kTrace && nxi == kTraceXI && ri == kTraceRI &&
            (m & (uint64_t(1) << kTraceBit)))
          std::fprintf(stderr,
                       "[trace] HSHIFT from x=%d %s → x=%d, "
                       "src_bits=0x%lx\n",
                       x, dbg_rot(ri), nx, (unsigned long)current);
        to_search[nxi][ri] |= m;
        remaining |= uint64_t(1) << (nxi * 4 + ri);
      }
    }

    // 5. Rotation expansion
    if (type == PieceType::O)
      continue;

    bool is_I = (type == PieceType::I);

    auto do_rotate = [&](Rotation target_r, const auto &kick_table) {
      int target_ri = static_cast<int>(target_r);
      uint64_t src = current;

      for (auto &kick : kick_table[ri]) {
        if (src == 0)
          break;
        int nx = x + kick.x;
        int nxi = nx + XS;
        if (nxi < 0 || nxi >= XW)
          continue;

        // Shift source positions by kick.y using threshold trick.
        constexpr int threshold = 3;
        int y_shift = threshold + kick.y;

        uint64_t shifted;
        if (y_shift >= 0 && y_shift < 64)
          shifted = (src << y_shift) >> threshold;
        else
          shifted = 0;

        uint64_t m = shifted & ~cm.data[nxi][target_ri];

        if (m) {
          uint64_t consumed;
          if (y_shift >= 0 && y_shift < 64)
            consumed = (m << threshold) >> y_shift;
          else
            consumed = 0;
          src &= ~consumed;
        }

        m &= ~searched[nxi][target_ri];
        if (m) {
          if (kTrace && nxi == kTraceXI && target_ri == kTraceRI &&
              (m & (uint64_t(1) << kTraceBit)))
            std::fprintf(stderr,
                         "[trace] KICK from x=%d %s bit=%d → "
                         "x=%d %s bit=%d  kick=(%d,%d) src=0x%lx\n",
                         x, dbg_rot(ri),
                         kTraceBit - kick.y + (int)YS - (int)YS, // approx
                         nx, dbg_rot(target_ri), kTraceBit, kick.x, kick.y,
                         (unsigned long)current);
          to_search[nxi][target_ri] |= m;
          last_rotated[nxi][target_ri] |= m; // mark as rotation-reached
          remaining |= uint64_t(1) << (nxi * 4 + target_ri);
        }
      }
    };

    do_rotate(rotate_cw(rot), is_I ? kKickCW_I : kKickCW_JLSTZ);
    do_rotate(rotate_ccw(rot), is_I ? kKickCCW_I : kKickCCW_JLSTZ);
    do_rotate(rotate_180(rot), is_I ? kKick180_I : kKick180_JLSTZ);
  }
}
