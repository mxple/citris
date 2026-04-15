#include "ai/opener_db.h"
#include <fstream>
#include <sstream>
#include <unordered_map>

// ---------------------------------------------------------------------------
// .opener file parser
// ---------------------------------------------------------------------------

static std::string trim(const std::string &s) {
  auto start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos)
    return "";
  auto end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

static const std::unordered_map<std::string, PieceType> kPieceMap = {
    {"I", PieceType::I}, {"O", PieceType::O}, {"T", PieceType::T},
    {"S", PieceType::S}, {"Z", PieceType::Z}, {"J", PieceType::J},
    {"L", PieceType::L},
};

// Parse a space/comma-separated piece list into a bitmask (for allow/exclude).
static uint8_t parse_piece_list(const std::string &s) {
  uint8_t mask = 0;
  std::istringstream ss(s);
  std::string token;
  while (ss >> token) {
    while (!token.empty() && token.back() == ',')
      token.pop_back();
    auto it = kPieceMap.find(token);
    if (it != kPieceMap.end())
      mask |= uint8_t(1) << static_cast<int>(it->second);
  }
  return mask;
}

// Parse a piece list with repeated entries into per-piece counts (for atleast).
// e.g., "T T" → min_counts[T] = 2
static PieceCounts parse_piece_counts(const std::string &s) {
  PieceCounts counts{};
  std::istringstream ss(s);
  std::string token;
  while (ss >> token) {
    while (!token.empty() && token.back() == ',')
      token.pop_back();
    auto it = kPieceMap.find(token);
    if (it != kPieceMap.end())
      counts[static_cast<int>(it->second)]++;
  }
  return counts;
}

// Intermediate representation for a parsed section before tree resolution.
struct ParsedSection {
  std::string id;
  std::string after;
  MatchMode match_mode = MatchMode::Strict;
  PieceConstraint constraint;
  int max_pieces = -1;
  std::vector<std::string> grid_rows;
};

std::optional<Opener> parse_opener_file(const std::string &contents) {
  Opener op;
  std::istringstream stream(contents);
  std::string line;

  // Phase 1: Split into header + raw sections
  bool in_header = true;
  std::vector<std::string> section_lines;
  std::vector<std::vector<std::string>> raw_sections;

  while (std::getline(stream, line)) {
    std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == '#')
      continue;

    if (trimmed.starts_with("---")) {
      if (!section_lines.empty()) {
        raw_sections.push_back(std::move(section_lines));
        section_lines.clear();
      }
      in_header = false;
      continue;
    }

    if (in_header) {
      if (trimmed.starts_with("name:"))
        op.name = trim(trimmed.substr(5));
      else if (trimmed.starts_with("description:"))
        op.description = trim(trimmed.substr(12));
    } else {
      section_lines.push_back(trimmed);
    }
  }
  if (!section_lines.empty())
    raw_sections.push_back(std::move(section_lines));

  if (op.name.empty() || raw_sections.empty())
    return std::nullopt;

  // Phase 2: Parse each section into ParsedSection
  bool any_id_or_after = false;
  std::vector<ParsedSection> parsed;

  for (auto &sec : raw_sections) {
    ParsedSection ps;

    for (auto &row : sec) {
      if (row.starts_with("id:")) {
        ps.id = trim(row.substr(3));
        any_id_or_after = true;
      } else if (row.starts_with("after:")) {
        ps.after = trim(row.substr(6));
        any_id_or_after = true;
      } else if (row == "[strict]") {
        ps.match_mode = MatchMode::Strict;
      } else if (row == "[soft]") {
        ps.match_mode = MatchMode::Soft;
      } else if (row.starts_with("allow:") || row.starts_with("include:")) {
        auto colon = row.find(':');
        ps.constraint.type = ConstraintType::Allowed;
        ps.constraint.pieces = parse_piece_list(trim(row.substr(colon + 1)));
      } else if (row.starts_with("exclude:")) {
        ps.constraint.type = ConstraintType::Blacklist;
        ps.constraint.pieces = parse_piece_list(trim(row.substr(8)));
      } else if (row.starts_with("pieces:")) {
        ps.max_pieces = std::stoi(trim(row.substr(7)));
      } else if (row.starts_with("atleast:")) {
        ps.constraint.type = ConstraintType::AtLeast;
        ps.constraint.min_counts = parse_piece_counts(trim(row.substr(8)));
        // Derive bitmask from counts for convenience
        for (int i = 0; i < 7; ++i)
          if (ps.constraint.min_counts[i])
            ps.constraint.pieces |= uint8_t(1) << i;
      } else if (row.size() >= 10) {
        // Grid characters: . (empty), X (wildcard), * (filled any),
        // I/O/T/S/Z/J/L (filled by specific piece)
        bool is_grid = true;
        for (int i = 0; i < 10; ++i) {
          char c = row[i];
          if (c != '.' && c != 'X' && c != '*' && c != 'I' && c != 'O' &&
              c != 'T' && c != 'S' && c != 'Z' && c != 'J' && c != 'L') {
            is_grid = false;
            break;
          }
        }
        if (is_grid)
          ps.grid_rows.push_back(row);
      }
    }

    if (!ps.grid_rows.empty())
      parsed.push_back(std::move(ps));
  }

  if (parsed.empty())
    return std::nullopt;

  // Phase 3: Build CheckpointNodes
  std::unordered_map<std::string, int> id_to_index;

  for (int i = 0; i < (int)parsed.size(); ++i) {
    auto &ps = parsed[i];

    Checkpoint cp;
    cp.name = ps.id;
    cp.match_mode = ps.match_mode;
    cp.constraint = ps.constraint;
    cp.max_pieces = ps.max_pieces;

    int nrows = (int)ps.grid_rows.size();
    cp.rows.resize(nrows, 0);
    cp.wildcards.resize(nrows, 0);
    cp.piece_types.resize(nrows * 10, -1);

    // Piece letter → PieceType index mapping
    static const std::unordered_map<char, int> letter_to_type = {
        {'I', 0}, {'O', 1}, {'T', 2}, {'S', 3},
        {'Z', 4}, {'J', 5}, {'L', 6},
    };

    // Reverse: text is top-to-bottom, board is bottom-to-top
    for (int r = 0; r < nrows; ++r) {
      int board_row = nrows - 1 - r;
      uint16_t filled_mask = 0;
      uint16_t wild_mask = 0;
      for (int x = 0; x < 10; ++x) {
        char c = ps.grid_rows[r][x];
        if (c == 'X') {
          // Wildcard — don't care
          wild_mask |= uint16_t(1) << x;
        } else if (c == '*') {
          // Must be filled, any piece
          filled_mask |= uint16_t(1) << x;
        } else if (c != '.') {
          // Piece letter — must be filled by specific piece
          filled_mask |= uint16_t(1) << x;
          auto it = letter_to_type.find(c);
          if (it != letter_to_type.end())
            cp.piece_types[board_row * 10 + x] = (int8_t)it->second;
        }
        // '.' = must be empty (neither filled nor wildcard)
      }
      cp.rows[board_row] = filled_mask;
      cp.wildcards[board_row] = wild_mask;
    }

    CheckpointNode node;
    node.checkpoint = std::move(cp);
    int idx = (int)op.nodes.size();
    op.nodes.push_back(std::move(node));

    if (!ps.id.empty())
      id_to_index[ps.id] = idx;
  }

  // Phase 4: Resolve tree structure
  if (any_id_or_after) {
    // Tree mode: use id/after to build parent-child links
    for (int i = 0; i < (int)parsed.size(); ++i) {
      auto &ps = parsed[i];
      if (ps.after.empty()) {
        op.roots.push_back(i);
      } else {
        auto it = id_to_index.find(ps.after);
        if (it != id_to_index.end())
          op.nodes[it->second].children.push_back(i);
        else
          op.roots.push_back(i); // unresolved → treat as root
      }
    }
  } else {
    // Linear mode (backward compat): sections form a chain
    op.roots.push_back(0);
    for (int i = 0; i + 1 < (int)op.nodes.size(); ++i)
      op.nodes[i].children.push_back(i + 1);
  }

  if (op.roots.empty())
    return std::nullopt;

  return op;
}

std::optional<Opener> load_opener(const std::filesystem::path &path) {
  std::ifstream f(path);
  if (!f)
    return std::nullopt;
  std::ostringstream ss;
  ss << f.rdbuf();
  return parse_opener_file(ss.str());
}

std::vector<Opener> load_openers_from_dir(const std::filesystem::path &dir) {
  std::vector<Opener> openers;
  if (!std::filesystem::is_directory(dir))
    return openers;
  for (auto &entry : std::filesystem::directory_iterator(dir)) {
    if (entry.path().extension() == ".opener") {
      if (auto op = load_opener(entry.path()))
        openers.push_back(std::move(*op));
    }
  }
  return openers;
}

// ---------------------------------------------------------------------------
// Built-in openers — empty, loaded from assets/openers/ instead
// ---------------------------------------------------------------------------

std::vector<Opener> builtin_openers() { return {}; }
