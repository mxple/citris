#pragma once
#include <chrono>
#include <string>

inline std::string path_join(const std::string &a, const std::string &b) {
  if (a.empty() || a == ".") return b;
  if (a.back() == '/' || a.back() == '\\') return a + b;
  return a + "/" + b;
}

struct GameTuning {
  std::chrono::milliseconds gravity_interval{10000};
  std::chrono::milliseconds lock_delay{5000};
  std::chrono::milliseconds garbage_delay{250};
  int max_lock_resets = 15;
  bool infinite_hold = false;
};

struct Settings {
  // Derives base_dir from the executable location (argv[0] on POSIX,
  // GetModuleFileNameW on Windows) and loads settings.ini from there.
  explicit Settings(const char *argv0);

  // Base directory (derived from argv[0], for resolving relative paths)
  std::string base_dir = ".";

  // Rendering (paths below are resolved to absolute form in the ctor)
  std::string skin_path = "assets/skin.png";

  // Controls
  KeyCode move_left = SDLK_LEFT;
  KeyCode move_right = SDLK_RIGHT;
  KeyCode rotate_cw = SDLK_UP;
  KeyCode rotate_ccw = SDLK_Z;
  KeyCode rotate_180 = SDLK_A;
  KeyCode hard_drop = SDLK_SPACE;
  KeyCode soft_drop = SDLK_DOWN;
  KeyCode hold = SDLK_C;
  KeyCode undo = SDLK_U;
  KeyCode reset_game = SDLK_GRAVE;
  KeyCode exit_to_menu = SDLK_BACKSPACE;
  KeyCode debug_menu = SDLK_F3;

  // Input tuning
  std::chrono::milliseconds das{110};
  std::chrono::milliseconds arr{0};
  std::chrono::milliseconds soft_drop_interval{0};
  std::chrono::milliseconds hard_drop_delay{50};
  bool das_preserve_charge = true;

  // Game tuning (from [game] INI section, applied to freeplay)
  GameTuning game;

  // Scaling
  float scale_factor = 1.f;
  bool auto_scale = true;

  // Ghost piece rendering
  bool colored_ghost = true;
  uint8_t ghost_opacity = 100;

  // Plan overlay max alpha (0–255); default matches the old hardcoded 180
  uint8_t plan_opacity = 180;

  // Linear filtering for skin/board textures (vs pixel-perfect nearest)
  bool antialiasing = false;

  // Board background
  uint8_t board_opacity = 255;

  // Gridlines
  uint8_t grid_opacity = 40;

  bool load(const std::string &path);
  bool save(const std::string &path) const;
};

std::string key_to_string(KeyCode key);
