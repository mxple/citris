#include "settings.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <unordered_map>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#elif defined(_WIN32)
#include <windows.h>
#include <filesystem>
#else
#include <filesystem>
#endif

Settings::Settings(const char *argv0) {
#ifdef __EMSCRIPTEN__
  (void)argv0;
  base_dir = ".";
  EM_ASM({
    var saved = localStorage.getItem('citris_settings');
    if (saved) FS.writeFile('/settings.ini', saved);
  });
#elif defined(_WIN32)
  (void)argv0;
  wchar_t exe_buf[MAX_PATH];
  GetModuleFileNameW(nullptr, exe_buf, MAX_PATH);
  base_dir = std::filesystem::path(exe_buf).parent_path().string();
#else
  base_dir = std::filesystem::path(argv0).parent_path().string();
#endif
  if (base_dir.empty())
    base_dir = ".";
  load(path_join(base_dir, "settings.ini"));
  skin_path = path_join(base_dir, skin_path);
}

static const std::unordered_map<std::string, KeyCode> kKeyMap = {
    {"a", SDLK_A},
    {"b", SDLK_B},
    {"c", SDLK_C},
    {"d", SDLK_D},
    {"e", SDLK_E},
    {"f", SDLK_F},
    {"g", SDLK_G},
    {"h", SDLK_H},
    {"i", SDLK_I},
    {"j", SDLK_J},
    {"k", SDLK_K},
    {"l", SDLK_L},
    {"m", SDLK_M},
    {"n", SDLK_N},
    {"o", SDLK_O},
    {"p", SDLK_P},
    {"q", SDLK_Q},
    {"r", SDLK_R},
    {"s", SDLK_S},
    {"t", SDLK_T},
    {"u", SDLK_U},
    {"v", SDLK_V},
    {"w", SDLK_W},
    {"x", SDLK_X},
    {"y", SDLK_Y},
    {"z", SDLK_Z},
    {"0", SDLK_0},
    {"1", SDLK_1},
    {"2", SDLK_2},
    {"3", SDLK_3},
    {"4", SDLK_4},
    {"5", SDLK_5},
    {"6", SDLK_6},
    {"7", SDLK_7},
    {"8", SDLK_8},
    {"9", SDLK_9},
    {"escape", SDLK_ESCAPE},
    {"lcontrol", SDLK_LCTRL},
    {"lshift", SDLK_LSHIFT},
    {"lalt", SDLK_LALT},
    {"rcontrol", SDLK_RCTRL},
    {"rshift", SDLK_RSHIFT},
    {"ralt", SDLK_RALT},
    {"lbracket", SDLK_LEFTBRACKET},
    {"rbracket", SDLK_RIGHTBRACKET},
    {"semicolon", SDLK_SEMICOLON},
    {"comma", SDLK_COMMA},
    {"period", SDLK_PERIOD},
    {"apostrophe", SDLK_APOSTROPHE},
    {"slash", SDLK_SLASH},
    {"backslash", SDLK_BACKSLASH},
    {"grave", SDLK_GRAVE},
    {"equal", SDLK_EQUALS},
    {"hyphen", SDLK_MINUS},
    {"space", SDLK_SPACE},
    {"enter", SDLK_RETURN},
    {"backspace", SDLK_BACKSPACE},
    {"tab", SDLK_TAB},
    {"left", SDLK_LEFT},
    {"right", SDLK_RIGHT},
    {"up", SDLK_UP},
    {"down", SDLK_DOWN},
    {"insert", SDLK_INSERT},
    {"delete", SDLK_DELETE},
    {"home", SDLK_HOME},
    {"end", SDLK_END},
    {"pageup", SDLK_PAGEUP},
    {"pagedown", SDLK_PAGEDOWN},
    {"f1", SDLK_F1},
    {"f2", SDLK_F2},
    {"f3", SDLK_F3},
    {"f4", SDLK_F4},
    {"f5", SDLK_F5},
    {"f6", SDLK_F6},
    {"f7", SDLK_F7},
    {"f8", SDLK_F8},
    {"f9", SDLK_F9},
    {"f10", SDLK_F10},
    {"f11", SDLK_F11},
    {"f12", SDLK_F12},
};

static const std::unordered_map<KeyCode, std::string> kReverseKeyMap = []() {
  std::unordered_map<KeyCode, std::string> m;
  for (auto &[name, key] : kKeyMap)
    m[key] = name;
  return m;
}();

std::string key_to_string(KeyCode key) {
  auto it = kReverseKeyMap.find(key);
  return it != kReverseKeyMap.end() ? it->second : "unknown";
}

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

static bool parse_key(const std::string &val, KeyCode &out) {
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

    bool ok = true;
    if (section == "rendering") {
      if (key == "skin")
        skin_path = val;
      else if (key == "colored_ghost")
        ok = parse_bool(val, colored_ghost);
      else if (key == "plan_opacity") {
        int pct;
        ok = parse_int(val, pct);
        if (ok)
          plan_opacity =
              static_cast<uint8_t>(std::clamp(pct, 0, 100) * 255 / 100);
      } else if (key == "ghost_opacity") {
        int pct;
        ok = parse_int(val, pct);
        if (ok)
          ghost_opacity =
              static_cast<uint8_t>(std::clamp(pct, 0, 100) * 255 / 100);
      } else if (key == "board_opacity") {
        int pct;
        ok = parse_int(val, pct);
        if (ok)
          board_opacity =
              static_cast<uint8_t>(std::clamp(pct, 0, 100) * 255 / 100);
      } else if (key == "grid_opacity") {
        int pct;
        ok = parse_int(val, pct);
        if (ok)
          grid_opacity =
              static_cast<uint8_t>(std::clamp(pct, 0, 100) * 255 / 100);
      } else if (key == "scale_factor") {
        try {
          float v = std::stof(trim(val));
          scale_factor = std::clamp(v, 0.5f, 4.f);
          ok = true;
        } catch (...) {
          ok = false;
        }
      } else if (key == "auto_scale")
        ok = parse_bool(val, auto_scale);
      else if (key == "antialiasing")
        ok = parse_bool(val, antialiasing);
      else
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
      else if (key == "reset_game")
        ok = parse_key(val, reset_game);
      else if (key == "exit_to_menu")
        ok = parse_key(val, exit_to_menu);
      else if (key == "debug_menu")
        ok = parse_key(val, debug_menu);
      else
        ok = false;
    } else if (section == "tuning") {
      if (key == "das")
        ok = parse_ms(val, das);
      else if (key == "arr")
        ok = parse_ms(val, arr);
      else if (key == "soft_drop_interval")
        ok = parse_ms(val, soft_drop_interval);
      else if (key == "hard_drop_delay")
        ok = parse_ms(val, hard_drop_delay);
      else if (key == "das_preserve_charge")
        ok = parse_bool(val, das_preserve_charge);
      else
        ok = false;
    } else if (section == "game") {
      if (key == "gravity_interval")
        ok = parse_ms(val, game.gravity_interval);
      else if (key == "lock_delay")
        ok = parse_ms(val, game.lock_delay);
      else if (key == "garbage_delay")
        ok = parse_ms(val, game.garbage_delay);
      else if (key == "max_lock_resets")
        ok = parse_int(val, game.max_lock_resets);
      else if (key == "infinite_hold")
        ok = parse_bool(val, game.infinite_hold);
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

bool Settings::save(const std::string &path) const {
  std::ofstream file(path);
  if (!file.is_open())
    return false;

  auto ks = [](KeyCode k) { return key_to_string(k); };
  auto opacity_pct = [](uint8_t v) { return v * 100 / 255; };

  file << "[rendering]\n";
  // Write skin_path relative to base_dir.
  std::string rel_skin = skin_path;
  if (auto pos = rel_skin.find(base_dir); pos == 0 && base_dir != ".") {
    rel_skin = rel_skin.substr(base_dir.size());
    if (!rel_skin.empty() && (rel_skin[0] == '/' || rel_skin[0] == '\\'))
      rel_skin = rel_skin.substr(1);
  }
  file << "skin = " << rel_skin << "\n";
  file << "colored_ghost = " << (colored_ghost ? "true" : "false") << "\n";
  file << "ghost_opacity = " << opacity_pct(ghost_opacity) << "\n";
  file << "plan_opacity = " << opacity_pct(plan_opacity) << "\n";
  file << "board_opacity = " << opacity_pct(board_opacity) << "\n";
  file << "grid_opacity = " << opacity_pct(grid_opacity) << "\n";
  file << "scale_factor = " << scale_factor << "\n";
  file << "auto_scale = " << (auto_scale ? "true" : "false") << "\n";
  file << "antialiasing = " << (antialiasing ? "true" : "false") << "\n";
  file << "\n";

  file << "[controls]\n";
  file << "move_left = " << ks(move_left) << "\n";
  file << "move_right = " << ks(move_right) << "\n";
  file << "rotate_cw = " << ks(rotate_cw) << "\n";
  file << "rotate_ccw = " << ks(rotate_ccw) << "\n";
  file << "rotate_180 = " << ks(rotate_180) << "\n";
  file << "hard_drop = " << ks(hard_drop) << "\n";
  file << "soft_drop = " << ks(soft_drop) << "\n";
  file << "hold = " << ks(hold) << "\n";
  file << "undo = " << ks(undo) << "\n";
  file << "reset_game = " << ks(reset_game) << "\n";
  file << "exit_to_menu = " << ks(exit_to_menu) << "\n";
  file << "debug_menu = " << ks(debug_menu) << "\n";
  file << "\n";

  file << "[tuning]\n";
  file << "das = " << das.count() << "\n";
  file << "arr = " << arr.count() << "\n";
  file << "soft_drop_interval = " << soft_drop_interval.count() << "\n";
  file << "hard_drop_delay = " << hard_drop_delay.count() << "\n";
  file << "das_preserve_charge = " << (das_preserve_charge ? "true" : "false")
       << "\n";
  file << "\n";

  file << "[game]\n";
  file << "gravity_interval = " << game.gravity_interval.count() << "\n";
  file << "lock_delay = " << game.lock_delay.count() << "\n";
  file << "garbage_delay = " << game.garbage_delay.count() << "\n";
  file << "max_lock_resets = " << game.max_lock_resets << "\n";
  file << "infinite_hold = " << (game.infinite_hold ? "true" : "false")
       << "\n";

  if (!file.good())
    return false;

#ifdef __EMSCRIPTEN__
  file.close();
  EM_ASM({
    var content = FS.readFile(UTF8ToString($0), {encoding: 'utf8'});
    localStorage.setItem('citris_settings', content);
  }, path.c_str());
#endif

  return true;
}
