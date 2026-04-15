#include "ai/board_bitset.h"
#include "ai/movegen.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

static uint64_t perft(const BoardBitset &board, const PieceType *queue, int depth) {
  MoveBuffer moves;
  generate_moves(board, moves, queue[0]);
  if (depth == 1)
    return moves.count;
  uint64_t nodes = 0;
  for (int i = 0; i < moves.count; ++i) {
    BoardBitset after = board;
    auto &m = moves.moves[i];
    after.place(m.type, m.rotation, m.x, m.y);
    after.clear_lines();
    nodes += perft(after, queue + 1, depth - 1);
  }
  return nodes;
}

static uint64_t perft_divide(const BoardBitset &board, const PieceType *queue,
                             int total_depth, int split_depth) {
  MoveBuffer moves;
  generate_moves(board, moves, queue[0]);
  uint64_t total = 0;
  for (int i = 0; i < moves.count; ++i) {
    auto &m = moves.moves[i];
    BoardBitset after = board;
    after.place(m.type, m.rotation, m.x, m.y);
    after.clear_lines();
    uint64_t count;
    if (split_depth <= 1) {
      count = perft(after, queue + 1, total_depth - 1);
    } else {
      count = perft_divide(after, queue + 1, total_depth - 1, split_depth - 1);
    }
    if (split_depth <= 1) {
      const char *rot_names[] = {"N", "E", "S", "W"};
      std::printf("  %s (%d,%d) %s: %lu\n",
                  rot_names[static_cast<int>(m.rotation)],
                  m.x, m.y,
                  m.spin == SpinKind::None ? "" :
                  m.spin == SpinKind::TSpin ? "tspin" : "spin",
                  count);
    }
    total += count;
  }
  return total;
}

static PieceType parse_piece(char ch) {
  switch (ch) {
  case 'I': return PieceType::I;
  case 'O': return PieceType::O;
  case 'T': return PieceType::T;
  case 'S': return PieceType::S;
  case 'Z': return PieceType::Z;
  case 'J': return PieceType::J;
  case 'L': return PieceType::L;
  default:  return PieceType::I;
  }
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <queue> [max_depth]\n", argv[0]);
    std::fprintf(stderr, "  queue: piece letters e.g. TISZJLO\n");
    std::fprintf(stderr, "  max_depth: optional, defaults to full queue length\n");
    return 1;
  }

  const char *queue_str = argv[1];
  int queue_len = std::strlen(queue_str);
  int max_depth = queue_len;
  bool divide = false;
  int divide_depth = 0;

  for (int i = 2; i < argc; ++i) {
    if (std::strcmp(argv[i], "--divide") == 0 || std::strcmp(argv[i], "-d") == 0) {
      divide = true;
      if (i + 1 < argc)
        divide_depth = std::atoi(argv[++i]);
    } else {
      int v = std::atoi(argv[i]);
      if (v > 0)
        max_depth = std::min(max_depth, v);
    }
  }

  std::vector<PieceType> queue(queue_len);
  for (int i = 0; i < queue_len; ++i) {
    queue[i] = parse_piece(queue_str[i]);
    if (queue_str[i] != 'I' && queue_str[i] != 'O' && queue_str[i] != 'T' &&
        queue_str[i] != 'S' && queue_str[i] != 'Z' && queue_str[i] != 'J' &&
        queue_str[i] != 'L') {
      std::fprintf(stderr, "unknown piece '%c'\n", queue_str[i]);
      return 1;
    }
  }

  std::printf("queue: %s (depth %d)\n", queue_str, max_depth);

  BoardBitset board{};
  if (divide && divide_depth > 0 && divide_depth <= max_depth) {
    std::printf("depth %d divide (showing subtrees to depth %d):\n",
                divide_depth, max_depth);
    uint64_t total = perft_divide(board, queue.data(), max_depth,
                                  divide_depth);
    std::printf("  total: %lu\n", total);
  } else {
    for (int d = 1; d <= max_depth; ++d) {
      uint64_t count = perft(board, queue.data(), d);
      std::printf("depth %d: %lu\n", d, count);
    }
  }

  return 0;
}
