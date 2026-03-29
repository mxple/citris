#pragma once

#include <algorithm>
#include <iterator>

enum class SpinKind { None, Mini, TSpin, AllSpin };

struct AttackState {
  int combo = 0;
  int b2b = 0;
};

// TETR.IO-style attack table.
// Returns lines sent and mutates state (combo, b2b).
inline int compute_attack_and_update_state(AttackState &state, int cleared,
                                           SpinKind spin) {
  if (cleared == 0) {
    state.combo = 0;
    return 0;
  }

  int base = 0;
  bool sustains_b2b = false;

  switch (spin) {
  case SpinKind::TSpin:
    // T-spin: doubled lines
    base = cleared * 2;
    sustains_b2b = true;
    break;
  case SpinKind::Mini:
    // T-spin mini: reduced damage
    switch (cleared) {
    case 1:
      base = 0;
      break;
    case 2:
      base = 1;
      break;
    default:
      base = cleared;
      break;
    }
    sustains_b2b = true;
    break;
  case SpinKind::AllSpin:
    // Allspin: normal line damage, maintains b2b
    switch (cleared) {
    case 1:
      base = 0;
      break;
    case 2:
      base = 1;
      break;
    case 3:
      base = 2;
      break;
    case 4:
      base = 4;
      break;
    default:
      base = cleared;
      break;
    }
    sustains_b2b = true;
    break;
  case SpinKind::None:
    switch (cleared) {
    case 1:
      base = 0;
      break;
    case 2:
      base = 1;
      break;
    case 3:
      base = 2;
      break;
    case 4:
      base = 4;
      sustains_b2b = true;
      break; // tetris
    default:
      base = cleared;
      break;
    }
    break;
  }

  // Combo bonus (TETR.IO combo table)
  int combo_bonus = 0;
  if (state.combo > 0) {
    static constexpr int kComboTable[] = {0, 1, 1, 1, 2, 2, 3, 3, 4, 4, 4, 5};
    int idx =
        std::min(state.combo, static_cast<int>(std::size(kComboTable) - 1));
    combo_bonus = kComboTable[idx];
  }
  state.combo++;

  // B2B bonus: +1 for consecutive difficult clears
  int b2b_bonus = 0;
  if (sustains_b2b) {
    if (state.b2b > 0) {
      b2b_bonus = 1;
    }
    state.b2b++;
  } else {
    state.b2b = 0;
  }

  return base + combo_bonus + b2b_bonus;
}
