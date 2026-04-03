#include "settings.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <unordered_map>

static const std::unordered_map<std::string, sf::Keyboard::Key> kKeyMap = {
    {"a", sf::Keyboard::Key::A},
    {"b", sf::Keyboard::Key::B},
    {"c", sf::Keyboard::Key::C},
    {"d", sf::Keyboard::Key::D},
    {"e", sf::Keyboard::Key::E},
    {"f", sf::Keyboard::Key::F},
    {"g", sf::Keyboard::Key::G},
    {"h", sf::Keyboard::Key::H},
    {"i", sf::Keyboard::Key::I},
    {"j", sf::Keyboard::Key::J},
    {"k", sf::Keyboard::Key::K},
    {"l", sf::Keyboard::Key::L},
    {"m", sf::Keyboard::Key::M},
    {"n", sf::Keyboard::Key::N},
    {"o", sf::Keyboard::Key::O},
    {"p", sf::Keyboard::Key::P},
    {"q", sf::Keyboard::Key::Q},
    {"r", sf::Keyboard::Key::R},
    {"s", sf::Keyboard::Key::S},
    {"t", sf::Keyboard::Key::T},
    {"u", sf::Keyboard::Key::U},
    {"v", sf::Keyboard::Key::V},
    {"w", sf::Keyboard::Key::W},
    {"x", sf::Keyboard::Key::X},
    {"y", sf::Keyboard::Key::Y},
    {"z", sf::Keyboard::Key::Z},
    {"0", sf::Keyboard::Key::Num0},
    {"1", sf::Keyboard::Key::Num1},
    {"2", sf::Keyboard::Key::Num2},
    {"3", sf::Keyboard::Key::Num3},
    {"4", sf::Keyboard::Key::Num4},
    {"5", sf::Keyboard::Key::Num5},
    {"6", sf::Keyboard::Key::Num6},
    {"7", sf::Keyboard::Key::Num7},
    {"8", sf::Keyboard::Key::Num8},
    {"9", sf::Keyboard::Key::Num9},
    {"escape", sf::Keyboard::Key::Escape},
    {"lcontrol", sf::Keyboard::Key::LControl},
    {"lshift", sf::Keyboard::Key::LShift},
    {"lalt", sf::Keyboard::Key::LAlt},
    {"rcontrol", sf::Keyboard::Key::RControl},
    {"rshift", sf::Keyboard::Key::RShift},
    {"ralt", sf::Keyboard::Key::RAlt},
    {"lbracket", sf::Keyboard::Key::LBracket},
    {"rbracket", sf::Keyboard::Key::RBracket},
    {"semicolon", sf::Keyboard::Key::Semicolon},
    {"comma", sf::Keyboard::Key::Comma},
    {"period", sf::Keyboard::Key::Period},
    {"apostrophe", sf::Keyboard::Key::Apostrophe},
    {"slash", sf::Keyboard::Key::Slash},
    {"backslash", sf::Keyboard::Key::Backslash},
    {"grave", sf::Keyboard::Key::Grave},
    {"equal", sf::Keyboard::Key::Equal},
    {"hyphen", sf::Keyboard::Key::Hyphen},
    {"space", sf::Keyboard::Key::Space},
    {"enter", sf::Keyboard::Key::Enter},
    {"backspace", sf::Keyboard::Key::Backspace},
    {"tab", sf::Keyboard::Key::Tab},
    {"left", sf::Keyboard::Key::Left},
    {"right", sf::Keyboard::Key::Right},
    {"up", sf::Keyboard::Key::Up},
    {"down", sf::Keyboard::Key::Down},
    {"insert", sf::Keyboard::Key::Insert},
    {"delete", sf::Keyboard::Key::Delete},
    {"home", sf::Keyboard::Key::Home},
    {"end", sf::Keyboard::Key::End},
    {"pageup", sf::Keyboard::Key::PageUp},
    {"pagedown", sf::Keyboard::Key::PageDown},
    {"f1", sf::Keyboard::Key::F1},
    {"f2", sf::Keyboard::Key::F2},
    {"f3", sf::Keyboard::Key::F3},
    {"f4", sf::Keyboard::Key::F4},
    {"f5", sf::Keyboard::Key::F5},
    {"f6", sf::Keyboard::Key::F6},
    {"f7", sf::Keyboard::Key::F7},
    {"f8", sf::Keyboard::Key::F8},
    {"f9", sf::Keyboard::Key::F9},
    {"f10", sf::Keyboard::Key::F10},
    {"f11", sf::Keyboard::Key::F11},
    {"f12", sf::Keyboard::Key::F12},
};

static std::string trim(const std::string &s) {
  auto start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos)
    return "";
  auto end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

static std::string to_lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

static bool parse_key(const std::string &val, sf::Keyboard::Key &out) {
  auto it = kKeyMap.find(to_lower(trim(val)));
  if (it != kKeyMap.end()) {
    out = it->second;
    return true;
  }
  return false;
}

static bool parse_int(const std::string &val, int &out) {
  try {
    out = std::stoi(trim(val));
    return true;
  } catch (...) {
    return false;
  }
}

static bool parse_bool(const std::string &val, bool &out) {
  auto v = to_lower(trim(val));
  if (v == "true" || v == "1" || v == "yes") {
    out = true;
    return true;
  }
  if (v == "false" || v == "0" || v == "no") {
    out = false;
    return true;
  }
  return false;
}

static bool parse_ms(const std::string &val, std::chrono::milliseconds &out) {
  int ms;
  if (parse_int(val, ms)) {
    out = std::chrono::milliseconds(ms);
    return true;
  }
  return false;
}

bool Settings::load(const std::string &path) {
  std::ifstream file(path);
  if (!file.is_open())
    return false;

  std::string section;
  std::string line;
  int line_num = 0;

  while (std::getline(file, line)) {
    ++line_num;
    line = trim(line);

    // Skip empty lines and comments
    if (line.empty() || line[0] == '#' || line[0] == ';')
      continue;

    // Section header
    if (line[0] == '[') {
      auto end = line.find(']');
      if (end != std::string::npos)
        section = to_lower(trim(line.substr(1, end - 1)));
      continue;
    }

    // Key = value
    auto eq = line.find('=');
    if (eq == std::string::npos)
      continue;
    auto key = to_lower(trim(line.substr(0, eq)));
    auto val = trim(line.substr(eq + 1));

    bool ok = true;
    if (section == "rendering") {
      if (key == "skin")
        skin_path = val;
      else if (key == "colored_ghost")
        ok = parse_bool(val, colored_ghost);
      else if (key == "ghost_opacity") {
        int pct;
        ok = parse_int(val, pct);
        if (ok)
          ghost_opacity =
              static_cast<uint8_t>(std::clamp(pct, 0, 100) * 255 / 100);
      } else if (key == "grid_opacity") {
        int pct;
        ok = parse_int(val, pct);
        if (ok)
          grid_opacity =
              static_cast<uint8_t>(std::clamp(pct, 0, 100) * 255 / 100);
      } else
        ok = false;
    } else if (section == "controls") {
      if (key == "move_left")
        ok = parse_key(val, move_left);
      else if (key == "move_right")
        ok = parse_key(val, move_right);
      else if (key == "rotate_cw")
        ok = parse_key(val, rotate_cw);
      else if (key == "rotate_ccw")
        ok = parse_key(val, rotate_ccw);
      else if (key == "rotate_180")
        ok = parse_key(val, rotate_180);
      else if (key == "hard_drop")
        ok = parse_key(val, hard_drop);
      else if (key == "soft_drop")
        ok = parse_key(val, soft_drop);
      else if (key == "hold")
        ok = parse_key(val, hold);
      else if (key == "undo")
        ok = parse_key(val, undo);
      else
        ok = false;
    } else if (section == "tuning") {
      if (key == "das")
        ok = parse_ms(val, das);
      else if (key == "arr")
        ok = parse_ms(val, arr);
      else if (key == "soft_drop_interval")
        ok = parse_ms(val, soft_drop_interval);
      else if (key == "das_preserve_charge")
        ok = parse_bool(val, das_preserve_charge);
      else
        ok = false;
    } else if (section == "game") {
      if (key == "gravity_interval")
        ok = parse_ms(val, gravity_interval);
      else if (key == "lock_delay")
        ok = parse_ms(val, lock_delay);
      else if (key == "garbage_delay")
        ok = parse_ms(val, garbage_delay);
      else if (key == "max_lock_resets")
        ok = parse_int(val, max_lock_resets);
      else if (key == "infinite_hold")
        ok = parse_bool(val, infinite_hold);
      else if (key == "hard_drop_delay")
        ok = parse_ms(val, hard_drop_delay);
      else
        ok = false;
    } else {
      ok = false;
    }

    if (!ok)
      std::cerr << path << ":" << line_num << ": unknown key '" << key
                << "' in [" << section << "]\n";
  }
  return true;
}
