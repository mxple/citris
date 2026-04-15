#include "ai/board_bitset.h"
#include "ai/movegen.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Reads a board + piece type from stdin and prints all legal placements.
//
// Input format (bottom row last, top row first):
//   |......#...|
//   |....#.#...|
//   |...##.#...|
//   |...#..#...|
//   J
//
// '#' = filled, '.' = empty. The pipe/bar borders are optional.
// Last non-empty line is the piece letter: I O T S Z J L
// Optionally append " sonic" to restrict to sonic-only (hard-drop) moves.

static const char *piece_name(PieceType t) {
  constexpr const char *names[] = {"I", "O", "T", "S", "Z", "J", "L"};
  return names[static_cast<int>(t)];
}

static const char *rot_name(Rotation r) {
  constexpr const char *names[] = {"N", "E", "S", "W"};
  return names[static_cast<int>(r)];
}

int main() {
  std::vector<std::string> lines;
  char buf[256];
  while (std::fgets(buf, sizeof(buf), stdin)) {
    // strip trailing newline
    int len = std::strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
      buf[--len] = '\0';
    if (len > 0)
      lines.push_back(buf);
  }

  if (lines.empty()) {
    std::fprintf(stderr, "No input\n");
    return 1;
  }

  // Last line: piece type (+ optional " sonic")
  std::string piece_line = lines.back();
  lines.pop_back();

  bool sonic = false;
  if (piece_line.find("sonic") != std::string::npos) {
    sonic = true;
    // strip " sonic"
    piece_line = piece_line.substr(0, piece_line.find_first_of(" \t"));
  }

  PieceType piece;
  char ch = piece_line[0];
  switch (ch) {
  case 'I': piece = PieceType::I; break;
  case 'O': piece = PieceType::O; break;
  case 'T': piece = PieceType::T; break;
  case 'S': piece = PieceType::S; break;
  case 'Z': piece = PieceType::Z; break;
  case 'J': piece = PieceType::J; break;
  case 'L': piece = PieceType::L; break;
  default:
    std::fprintf(stderr, "Unknown piece: %c\n", ch);
    return 1;
  }

  // Parse board rows (input is top-to-bottom, board is y-up)
  BoardBitset bb;
  int num_rows = static_cast<int>(lines.size());
  for (int i = 0; i < num_rows; ++i) {
    int board_y = num_rows - 1 - i; // top line = highest row
    const std::string &row = lines[i];
    int col = 0;
    for (char c : row) {
      if (c == '|')
        continue;
      if (col >= 10)
        break;
      if (c == '#') {
        bb.rows[board_y] |= uint16_t(1) << col;
        bb.cols[col] |= uint64_t(1) << board_y;
      }
      ++col;
    }
  }

  // Print parsed board
  std::printf("Board (%d rows):\n", num_rows);
  for (int y = num_rows - 1; y >= 0; --y) {
    std::printf("  %2d |", y);
    for (int x = 0; x < 10; ++x)
      std::printf("%c", (bb.rows[y] >> x) & 1 ? '#' : '.');
    std::printf("|\n");
  }
  std::printf("Piece: %s  sonic: %s\n\n", piece_name(piece),
              sonic ? "yes" : "no");

  // Generate moves
  MoveBuffer moves;
  generate_moves(bb, moves, piece, sonic);

  // Sort by rotation, then x, then y
  std::sort(moves.begin(), moves.end(),
            [](const Placement &a, const Placement &b) {
              if (a.rotation != b.rotation)
                return static_cast<int>(a.rotation) < static_cast<int>(b.rotation);
              if (a.x != b.x)
                return a.x < b.x;
              return a.y < b.y;
            });

  std::printf("%d placements:\n", moves.count);
  for (int i = 0; i < moves.count; ++i) {
    auto &m = moves.moves[i];
    auto cells = m.cells();

    std::printf("  %s rot=%s pos=(%d,%d)  cells=", piece_name(m.type),
                rot_name(m.rotation), m.x, m.y);
    for (int c = 0; c < 4; ++c)
      std::printf("(%d,%d)%s", cells[c].x, cells[c].y, c < 3 ? " " : "");

    // Show board with this placement
    std::printf("\n");
    BoardBitset after = bb;
    after.place(m.type, m.rotation, m.x, m.y);
    int top = 0;
    for (int y = BoardBitset::kHeight - 1; y >= 0; --y)
      if (after.rows[y] & 0x3FF) { top = y; break; }
    for (int y = top; y >= 0; --y) {
      std::printf("       %2d |", y);
      for (int x = 0; x < 10; ++x) {
        bool was = (bb.rows[y] >> x) & 1;
        bool now = (after.rows[y] >> x) & 1;
        std::printf("%c", now ? (was ? '#' : '*') : '.');
      }
      std::printf("|\n");
    }
    std::printf("\n");
  }

  return 0;
}
