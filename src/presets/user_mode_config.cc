#include "user_mode_config.h"
#include <algorithm>
#include <fstream>
#include <iostream>

static std::string trim(const std::string &s) {
  auto start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos)
    return {};
  return s.substr(start, s.find_last_not_of(" \t\r\n") - start + 1);
}

static std::string to_lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

static bool parse_bool(const std::string &val) {
  auto v = to_lower(val);
  return v == "true" || v == "1" || v == "yes";
}

static std::optional<PieceType> char_to_piece(char c) {
  switch (std::toupper(c)) {
  case 'I': return PieceType::I;
  case 'O': return PieceType::O;
  case 'T': return PieceType::T;
  case 'S': return PieceType::S;
  case 'Z': return PieceType::Z;
  case 'J': return PieceType::J;
  case 'L': return PieceType::L;
  default: return std::nullopt;
  }
}

static std::vector<PieceType> parse_piece_list(const std::string &val) {
  std::vector<PieceType> pieces;
  for (char c : val)
    if (auto pt = char_to_piece(c))
      pieces.push_back(*pt);
  return pieces;
}

// Parse board rows from subsequent lines until a section header or EOF.
// Format: 10 chars per line, X = filled, . = empty, top row first.
static std::vector<uint16_t>
parse_board_rows(std::ifstream &file, std::string &lookahead) {
  std::vector<uint16_t> rows;
  lookahead.clear();
  std::string line;
  while (std::getline(file, line)) {
    auto row = trim(line);
    if (row.empty() || row[0] == '#' || row[0] == ';')
      continue;
    if (row[0] == '[' || row.find('=') != std::string::npos) {
      lookahead = line; // belongs to the next section/key
      break;
    }
    if (row.size() < 10)
      continue;
    uint16_t mask = 0;
    for (int i = 0; i < 10; ++i) {
      if (row[i] != '.' && row[i] != ' ')
        mask |= (1 << i);
    }
    rows.push_back(mask);
  }
  // Input is top-to-bottom; store bottom-first.
  std::reverse(rows.begin(), rows.end());
  return rows;
}

UserModeConfig parse_user_mode(const std::string &path) {
  UserModeConfig cfg;

  std::ifstream file(path);
  if (!file.is_open()) {
    std::cerr << "Failed to open umode: " << path << "\n";
    return cfg;
  }

  std::string section;
  std::string line;
  std::string lookahead; // set by parse_board_rows when it overshoots

  auto next_line = [&]() -> bool {
    if (!lookahead.empty()) {
      line = std::move(lookahead);
      lookahead.clear();
      return true;
    }
    return !!std::getline(file, line);
  };

  while (next_line()) {
    line = trim(line);
    if (line.empty() || line[0] == '#' || line[0] == ';')
      continue;

    if (line[0] == '[') {
      auto end = line.find(']');
      if (end != std::string::npos)
        section = to_lower(trim(line.substr(1, end - 1)));
      continue;
    }

    auto eq = line.find('=');
    if (eq == std::string::npos)
      continue;
    auto key = to_lower(trim(line.substr(0, eq)));
    auto val = trim(line.substr(eq + 1));

    // --- [meta] ---
    if (section == "meta") {
      if (key == "name")
        cfg.name = val;
      else if (key == "description")
        cfg.description = val;
    }

    // --- [queue] ---
    else if (section == "queue") {
      if (key == "initial") {
        auto v = to_lower(val);
        if (v != "none" && !v.empty())
          cfg.initial_queue = parse_piece_list(val);
      } else if (key == "shuffle_initial")
        cfg.shuffle_initial = parse_bool(val);
      else if (key == "continuation") {
        auto v = to_lower(val);
        if (v == "7bag")
          cfg.continuation = UserModeConfig::Continuation::SevenBag;
        else if (v == "random")
          cfg.continuation = UserModeConfig::Continuation::Random;
        else if (v == "none")
          cfg.continuation = UserModeConfig::Continuation::None;
      } else if (key == "count") {
        auto v = to_lower(val);
        cfg.count = (v == "inf" || v == "0") ? 0 : std::stoi(val);
      }
    }

    // --- [board] ---
    else if (section == "board") {
      if (key == "type") {
        auto v = to_lower(val);
        if (v == "empty")
          cfg.board_type = UserModeConfig::BoardType::Empty;
        else if (v == "predetermined")
          cfg.board_type = UserModeConfig::BoardType::Predetermined;
        else if (v == "generated")
          cfg.board_type = UserModeConfig::BoardType::Generated;
      }
    }

    // --- [board.predetermined] ---
    // Rows are listed as bare lines (no key=value), top-to-bottom.
    else if (section == "board.predetermined") {
      // First line after [board.predetermined] is a row. Re-parse current
      // line as a row, then read remaining rows.
      std::vector<uint16_t> rows;
      // Parse current line as first row.
      if (line.size() >= 10 && line.find('=') == std::string::npos) {
        uint16_t mask = 0;
        for (int i = 0; i < 10; ++i)
          if (line[i] != '.' && line[i] != ' ')
            mask |= (1 << i);
        rows.push_back(mask);
      }
      // Parse remaining rows.
      auto rest = parse_board_rows(file, lookahead);
      rows.insert(rows.end(), rest.begin(), rest.end());
      // parse_board_rows reverses, but our first row was already top-most.
      // The `rest` rows are already reversed. We need to reverse the whole
      // thing together: rows[0] = top = highest row.
      // Actually, the first row we parsed is the top row, and rest is
      // already bottom-first. Let's just reverse the manually-parsed one
      // and prepend.
      // Simpler: treat all rows as top-to-bottom, reverse at the end.
      // rest is already reversed. The manually parsed row isn't.
      // Let me just reverse rest back, append our first row at front,
      // then reverse the whole thing.
      std::reverse(rest.begin(), rest.end()); // undo parse_board_rows reverse
      rest.insert(rest.begin(), rows[0]);
      std::reverse(rest.begin(), rest.end()); // final: bottom-first
      cfg.board_rows = std::move(rest);
    }

    // --- [board.generated] ---
    else if (section == "board.generated") {
      if (key == "max_height")
        cfg.gen_max_height = std::stoi(val);
      else if (key == "bumpiness")
        cfg.gen_bumpiness = std::stof(val);
      else if (key == "sparseness")
        cfg.gen_sparseness = std::stof(val);
      else if (key == "allow_holes")
        cfg.gen_allow_holes = parse_bool(val);
    }

    // --- [goal] ---
    else if (section == "goal") {
      if (key == "use_all_queue")
        cfg.use_all_queue = parse_bool(val);
      else if (key == "pc")
        cfg.pc = std::stoi(val);
      else if (key == "tsd")
        cfg.tsd = std::stoi(val);
      else if (key == "tst")
        cfg.tst = std::stoi(val);
      else if (key == "quad")
        cfg.quad = std::stoi(val);
      else if (key == "combo")
        cfg.combo = std::stoi(val);
      else if (key == "b2b")
        cfg.b2b = std::stoi(val);
      else if (key == "lines")
        cfg.lines = std::stoi(val);
      else if (key == "max_overhangs")
        cfg.max_overhangs = std::stoi(val);
      else if (key == "max_holes")
        cfg.max_holes = std::stoi(val);
    }

  }

  return cfg;
}
