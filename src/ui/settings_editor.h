#pragma once

#include "settings.h"
#include <SDL3/SDL.h>
#include <string>
#include <vector>

class SettingsEditor {
public:
  SettingsEditor(SDL_Renderer *renderer, SDL_Window *window,
                 Settings &settings);
  ~SettingsEditor();

  // Draws the settings editor UI into the current ImGui frame. Assumes the
  // caller has already called ImGui::NewFrame() and owns an enclosing
  // Begin/End (or panel). Does not call Begin/End itself.
  void draw();

  // Returns true if the user clicked Save or Back in the last draw() call.
  bool should_close() const { return close_requested_; }
  void reset_close() { close_requested_ = false; }

private:
  void save_settings();
  void discover_skins();
  void sync_from_settings();
  void reload_skin_preview();
  int keycode_to_display(const char *buf, size_t n, unsigned key);

  SDL_Renderer *renderer_;
  SDL_Window *window_;
  Settings &settings_;

  // Adapter ints for chrono::ms fields
  int das_ms_, arr_ms_, soft_drop_ms_;
  int gravity_ms_, lock_delay_ms_, garbage_delay_ms_, hard_drop_delay_ms_;

  // Adapter ints for opacity (0-100 percentage)
  int ghost_opacity_pct_, grid_opacity_pct_;

  // Scale factor as percent
  int scale_pct_;

  std::vector<std::string> skin_paths_;
  int skin_index_ = 0;

  // Cached texture for previewing the currently selected skin.
  SDL_Texture *preview_tex_ = nullptr;
  std::string preview_loaded_path_;

  // Keybind capture state: index of currently-capturing key field, -1 if
  // none. Indices map to keybind_targets_ below.
  int capturing_ = -1;

  bool close_requested_ = false;
};
