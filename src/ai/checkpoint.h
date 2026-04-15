#pragma once

#include "board_bitset.h"
#include "placement.h"
#include <optional>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Match mode — how strictly a checkpoint must be matched.
// ---------------------------------------------------------------------------

enum class MatchMode : uint8_t {
  Strict, // Board must exactly match checkpoint (no extra cells)
  Soft    // All checkpoint cells must be filled; extra cells elsewhere OK
};

// ---------------------------------------------------------------------------
// Piece constraint — restricts which pieces may be used for a checkpoint.
// ---------------------------------------------------------------------------

enum class ConstraintType : uint8_t {
  None,      // no restriction
  Allowed,   // only these pieces may be placed
  AtLeast,   // path must include at least these pieces
  Blacklist  // these pieces may not be placed
};

struct PieceConstraint {
  ConstraintType type = ConstraintType::None;
  uint8_t pieces = 0;              // bitmask for Allowed/Blacklist
  PieceCounts min_counts{};        // per-piece minimums for AtLeast (e.g., T T → min_counts[T]=2)

  // Can this piece type be placed under the constraint?
  bool piece_allowed(PieceType p) const {
    uint8_t bit = uint8_t(1) << static_cast<int>(p);
    switch (type) {
    case ConstraintType::None:
      return true;
    case ConstraintType::Allowed:
      return (pieces & bit) != 0;
    case ConstraintType::Blacklist:
      return (pieces & bit) == 0;
    case ConstraintType::AtLeast:
      return true;
    }
    return true;
  }

  // Check full constraint satisfaction for a completed path.
  bool path_valid(const PieceCounts &used) const {
    switch (type) {
    case ConstraintType::None:
      return true;
    case ConstraintType::Allowed: {
      for (int i = 0; i < 7; ++i)
        if (used[i] && !((pieces >> i) & 1))
          return false;
      return true;
    }
    case ConstraintType::Blacklist: {
      for (int i = 0; i < 7; ++i)
        if (used[i] && ((pieces >> i) & 1))
          return false;
      return true;
    }
    case ConstraintType::AtLeast:
      for (int i = 0; i < 7; ++i)
        if (used[i] < min_counts[i])
          return false;
      return true;
    }
    return true;
  }
};

// ---------------------------------------------------------------------------
// MatchScore — detailed comparison of a board against a checkpoint.
// ---------------------------------------------------------------------------

struct MatchScore {
  int matched = 0; // required-filled cell ∩ board cell
  int missing = 0; // required-filled cell \ board cell
  int extra = 0;   // board cell in non-wildcard empty position
};

// ---------------------------------------------------------------------------
// Checkpoint — a target board silhouette for one step of an opener.
//
// Grid cell types (in .opener files):
//   '.' = must be empty
//   'X' = wildcard (don't care — filled or empty OK)
//   '*' = must be filled (any piece type)
//   'I'/'O'/'T'/'S'/'Z'/'J'/'L' = must be filled by that specific piece
//
// The rows bitmask stores which cells must be filled ('*' and piece letters).
// The wildcards bitmask stores which cells are don't-care.
// Piece annotations store the required piece type for annotated cells.
// ---------------------------------------------------------------------------

struct Checkpoint {
  std::string name;
  std::vector<uint16_t> rows;             // must-be-filled bitmask per row
  std::vector<uint16_t> wildcards;        // wildcard (don't care) bitmask per row
  std::vector<int8_t> piece_types;        // [y * 10 + x], -1 = no annotation
  MatchMode match_mode = MatchMode::Strict;
  PieceConstraint constraint;
  int max_pieces = -1;                    // max pieces to reach this checkpoint (-1 = unlimited)

  int height() const { return static_cast<int>(rows.size()); }

  bool cell_filled(int col, int row) const {
    if (row < 0 || row >= height())
      return false;
    return (rows[row] >> col) & 1;
  }

  bool cell_wildcard(int col, int row) const {
    if (row < 0 || row >= height() || row >= (int)wildcards.size())
      return false;
    return (wildcards[row] >> col) & 1;
  }

  // Per-cell piece type annotation (-1 = none).
  int8_t cell_piece(int col, int row) const {
    if (piece_types.empty() || row < 0 || row >= height())
      return -1;
    return piece_types[row * 10 + col];
  }

  bool has_piece_annotations() const {
    for (auto p : piece_types)
      if (p >= 0)
        return true;
    return false;
  }

  void set_cell(int col, int row, bool filled) {
    if (row < 0 || row >= static_cast<int>(rows.size()))
      return;
    if (filled)
      rows[row] |= uint16_t(1) << col;
    else
      rows[row] &= ~(uint16_t(1) << col);
  }

  // Check if board occupancy matches this checkpoint (respects match_mode
  // and wildcards). Does NOT validate piece-type annotations — that requires
  // the engine Board with CellColor.
  bool matches(const BoardBitset &board) const {
    for (int y = 0; y < height(); ++y) {
      uint16_t target = rows[y] & 0x3FF;
      uint16_t wild =
          (y < (int)wildcards.size()) ? (wildcards[y] & 0x3FF) : uint16_t(0);
      uint16_t actual = board.rows[y] & 0x3FF;
      uint16_t care = ~wild & 0x3FF; // cells we check

      if (match_mode == MatchMode::Strict) {
        // Care cells: actual must match target exactly
        if ((actual & care) != (target & care))
          return false;
      } else {
        // Soft: all target (filled) cells must be filled
        if ((actual & target) != target)
          return false;
      }
    }
    if (match_mode == MatchMode::Strict) {
      // Rows above checkpoint must be empty
      for (int y = height(); y < BoardBitset::kHeight; ++y) {
        if (board.rows[y] & 0x3FF)
          return false;
      }
    }
    return true;
  }

  // Detailed score for evaluation. Wildcard cells are excluded from scoring.
  MatchScore match_score(const BoardBitset &board) const {
    MatchScore ms;
    for (int y = 0; y < height(); ++y) {
      uint16_t target = rows[y] & 0x3FF;
      uint16_t wild =
          (y < (int)wildcards.size()) ? (wildcards[y] & 0x3FF) : uint16_t(0);
      uint16_t actual = board.rows[y] & 0x3FF;
      uint16_t care = ~wild & 0x3FF;

      ms.matched +=
          std::popcount(static_cast<unsigned>(target & actual & care));
      ms.missing +=
          std::popcount(static_cast<unsigned>(target & ~actual & care));
      ms.extra +=
          std::popcount(static_cast<unsigned>(~target & actual & care));
    }
    return ms;
  }
};

// ---------------------------------------------------------------------------
// CheckpointNode — a checkpoint with links to next-step alternatives.
// ---------------------------------------------------------------------------

struct CheckpointNode {
  Checkpoint checkpoint;
  std::vector<int> children; // indices into Opener::nodes for next steps
};

// ---------------------------------------------------------------------------
// Opener — a named tree of checkpoints.
// ---------------------------------------------------------------------------

struct Opener {
  std::string name;
  std::string description;
  std::vector<CheckpointNode> nodes;
  std::vector<int> roots;

  bool empty() const { return nodes.empty(); }

  int max_depth() const {
    int max_d = 0;
    for (int ri : roots)
      max_d = std::max(max_d, depth_of(ri));
    return max_d;
  }

private:
  int depth_of(int ni) const {
    int max_child = 0;
    for (int ci : nodes[ni].children)
      max_child = std::max(max_child, depth_of(ci));
    return 1 + max_child;
  }
};

// ---------------------------------------------------------------------------
// Plan — AI-computed placement sequence to reach checkpoints.
// ---------------------------------------------------------------------------

struct Plan {
  struct Step {
    Placement placement;
    bool uses_hold = false;
    int node_idx = -1;
    int lines_cleared = 0;
    BoardBitset board_after;
  };

  std::vector<Step> steps;
  int current_step = 0;

  bool complete() const {
    return current_step >= static_cast<int>(steps.size());
  }

  const Step *current() const {
    if (complete())
      return nullptr;
    return &steps[current_step];
  }

  void advance() {
    if (!complete())
      ++current_step;
  }
};
