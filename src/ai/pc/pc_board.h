#pragma once

// Port of MinusKelvin's pcf BitBoard.

#include <array>
#include <bit>
#include <cstdint>

// Packed 60-bit board for perfect clear solving (<=6 rows x 10 columns).
// Bit n = cell at column (n % 10), row (n / 10). Row 0 is bottom.
struct PcBoard {
  uint64_t bits = 0;

  static PcBoard filled(int height) {
    return {(1ULL << (height * 10)) - 1};
  }

  PcBoard combine(PcBoard o) const { return {bits | o.bits}; }
  PcBoard remove(PcBoard o) const { return {bits & ~o.bits}; }
  bool overlaps(PcBoard o) const { return (bits & o.bits) != 0; }

  bool operator==(PcBoard o) const { return bits == o.bits; }
  bool operator!=(PcBoard o) const { return bits != o.bits; }

  bool cell_filled(int x, int y) const {
    return y < 6 && (bits & (1ULL << (x + y * 10))) != 0;
  }

  bool line_filled(int y) const {
    return ((bits >> (10 * y)) & 0x3FF) == 0x3FF;
  }

  PcBoard lines_cleared() const {
    uint64_t b = 0;
    int dst = 0;
    for (int y = 0; y < 6; ++y) {
      if (!line_filled(y)) {
        b |= ((bits >> (10 * y)) & 0x3FF) << (10 * dst);
        ++dst;
      }
    }
    return {b};
  }

  // Leftmost column with any empty cell in rows 0..height-1.
  int leftmost_empty_column(int height) const {
    uint64_t collapsed = 0x3FF;
    for (int y = 0; y < height; ++y)
      collapsed &= bits >> (y * 10);
    return std::countr_one(static_cast<uint32_t>(collapsed & 0x3FF));
  }

  int popcount() const { return std::popcount(bits); }
};

// Precomputed masks: for a 6-bit hurdle bitmask, produce a u64 with full
// rows (all 10 bits set) at the indicated row positions.
inline constexpr std::array<uint64_t, 64> kHurdleMasks = [] {
  std::array<uint64_t, 64> m{};
  for (int h = 0; h < 64; ++h)
    for (int y = 0; y < 6; ++y)
      if (h & (1 << y))
        m[h] |= 0x3FFULL << (y * 10);
  return m;
}();
