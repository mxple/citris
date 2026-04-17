#include "hold_shuffle.h"
#include <algorithm>
#include <numeric>
#include <optional>

namespace {

// is_good_queue (script7.js:812): the queue can be split into at most two
// runs of distinct pieces (a new run starts the moment a duplicate appears).
// Equivalent to "no piece appears more than twice, and the two occurrences
// straddle a 7-bag boundary." For unique_pieces=1 puzzle queues every piece
// is distinct, so this trivially holds.
bool is_good_queue(const std::vector<PieceType> &q) {
  std::vector<PieceType> accum;
  int count = 1;
  for (PieceType t : q) {
    if (std::find(accum.begin(), accum.end(), t) != accum.end()) {
      accum.clear();
      ++count;
    }
    accum.push_back(t);
  }
  return count <= 2;
}

} // namespace

bool is_hold_equivalent(const std::vector<PieceType> &bag,
                        const std::vector<PieceType> &target) {
  const int n = static_cast<int>(bag.size());
  if (static_cast<int>(target.size()) != n)
    return false;
  if (n == 0)
    return true;

  std::optional<PieceType> hold;
  std::optional<PieceType> current;
  int draw_idx = 0;

  auto draw = [&]() {
    if (draw_idx < n)
      current = bag[draw_idx++];
    else
      current = std::nullopt;
  };
  draw();

  for (PieceType need : target) {
    if (current && *current == need) {
      draw();
    } else if (hold && *hold == need) {
      std::swap(hold, current);
      draw();
    } else if (!hold && current) {
      // Hold-swap with empty hold: current moves to hold, the next bag
      // piece is drawn as the new current. Player can then play that.
      hold = current;
      draw();
      if (!current || *current != need)
        return false;
      draw();
    } else {
      return false;
    }
  }
  return true;
}

std::vector<PieceType>
shuffle_hold_equivalent(const std::vector<PieceType> &queue,
                        std::mt19937 &rng) {
  const int n = static_cast<int>(queue.size());
  if (n < 2 || n > 7)
    return queue;

  // Brute-force enumerate index permutations (up to 7! = 5040). For each,
  // check whether the rearranged bag is hold-equivalent to the original
  // queue and survives is_good_queue.
  std::vector<int> perm(n);
  std::iota(perm.begin(), perm.end(), 0);
  std::vector<std::vector<PieceType>> candidates;

  do {
    std::vector<PieceType> bag(n);
    for (int i = 0; i < n; ++i)
      bag[i] = queue[perm[i]];
    if (is_hold_equivalent(bag, queue) && is_good_queue(bag))
      candidates.push_back(std::move(bag));
  } while (std::next_permutation(perm.begin(), perm.end()));

  if (candidates.empty())
    return queue;
  std::uniform_int_distribution<size_t> pick(0, candidates.size() - 1);
  return candidates[pick(rng)];
}
