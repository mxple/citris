#include "ui/settings_editor.h"
#include "render/font.h"
#include "render/renderer.h"
#include <filesystem>

using L = RenderLayout;

SettingsEditor::SettingsEditor(SDL_Renderer *renderer, SDL_Window *window,
                               Settings &settings)
    : renderer_(renderer), window_(window), settings_(settings) {
  // Init adapter ints from settings
  das_ms_ = static_cast<int>(settings_.das.count());
  arr_ms_ = static_cast<int>(settings_.arr.count());
  soft_drop_ms_ = static_cast<int>(settings_.soft_drop_interval.count());
  gravity_ms_ = static_cast<int>(settings_.game.gravity_interval.count());
  lock_delay_ms_ = static_cast<int>(settings_.game.lock_delay.count());
  garbage_delay_ms_ = static_cast<int>(settings_.game.garbage_delay.count());
  hard_drop_delay_ms_ =
      static_cast<int>(settings_.game.hard_drop_delay.count());
  ghost_opacity_pct_ = settings_.ghost_opacity * 100 / 255;
  grid_opacity_pct_ = settings_.grid_opacity * 100 / 255;
  scale_pct_ = static_cast<int>(settings_.scale_factor * 100.f + 0.5f);
}

void SettingsEditor::build_widgets() {
  // Discover available skins
  std::vector<std::string> skin_paths;
  for (auto &entry :
       std::filesystem::directory_iterator(settings_.base_dir / "assets")) {
    auto name = entry.path().filename().string();
    if (name.starts_with("skin") && name.ends_with(".png"))
      skin_paths.push_back("assets/" + name);
  }
  std::sort(skin_paths.begin(), skin_paths.end());

  // --- Controls ---
  widgets_.push_back(std::make_unique<SectionHeader>("CONTROLS"));
  widgets_.push_back(
      std::make_unique<KeyBindWidget>("Move Left", &settings_.move_left));
  widgets_.push_back(
      std::make_unique<KeyBindWidget>("Move Right", &settings_.move_right));
  widgets_.push_back(
      std::make_unique<KeyBindWidget>("Rotate CW", &settings_.rotate_cw));
  widgets_.push_back(
      std::make_unique<KeyBindWidget>("Rotate CCW", &settings_.rotate_ccw));
  widgets_.push_back(
      std::make_unique<KeyBindWidget>("Rotate 180", &settings_.rotate_180));
  widgets_.push_back(
      std::make_unique<KeyBindWidget>("Hard Drop", &settings_.hard_drop));
  widgets_.push_back(
      std::make_unique<KeyBindWidget>("Soft Drop", &settings_.soft_drop));
  widgets_.push_back(
      std::make_unique<KeyBindWidget>("Hold", &settings_.hold));
  widgets_.push_back(
      std::make_unique<KeyBindWidget>("Undo", &settings_.undo));

  // --- Input Tuning ---
  widgets_.push_back(std::make_unique<SectionHeader>("INPUT TUNING"));
  widgets_.push_back(
      std::make_unique<NumericInputWidget>("DAS (ms)", &das_ms_, 0, 500));
  widgets_.push_back(
      std::make_unique<NumericInputWidget>("ARR (ms)", &arr_ms_, 0, 200));
  widgets_.push_back(std::make_unique<NumericInputWidget>(
      "Soft Drop Interval (ms)", &soft_drop_ms_, 0, 500));
  widgets_.push_back(std::make_unique<BoolToggleWidget>(
      "DAS Preserve Charge", &settings_.das_preserve_charge));

  // --- Rendering ---
  widgets_.push_back(std::make_unique<SectionHeader>("RENDERING"));
  widgets_.push_back(std::make_unique<BoolToggleWidget>(
      "Auto Scale", &settings_.auto_scale));
  widgets_.push_back(std::make_unique<NumericInputWidget>(
      "Scale %", &scale_pct_, 50, 400));
  widgets_.push_back(std::make_unique<SkinPickerWidget>(
      "Skin", &settings_.skin_path, skin_paths,
      settings_.base_dir.string(), renderer_));
  widgets_.push_back(std::make_unique<BoolToggleWidget>(
      "Colored Ghost", &settings_.colored_ghost));
  widgets_.push_back(std::make_unique<NumericInputWidget>(
      "Ghost Opacity %", &ghost_opacity_pct_, 0, 100));
  widgets_.push_back(std::make_unique<NumericInputWidget>(
      "Grid Opacity %", &grid_opacity_pct_, 0, 100));

  // --- Game Tuning ---
  widgets_.push_back(std::make_unique<SectionHeader>("GAME TUNING"));
  widgets_.push_back(std::make_unique<NumericInputWidget>(
      "Gravity Interval (ms)", &gravity_ms_, -1, 60000));
  widgets_.push_back(std::make_unique<NumericInputWidget>(
      "Lock Delay (ms)", &lock_delay_ms_, -1, 60000));
  widgets_.push_back(std::make_unique<NumericInputWidget>(
      "Garbage Delay (ms)", &garbage_delay_ms_, 0, 10000));
  widgets_.push_back(std::make_unique<NumericInputWidget>(
      "Hard Drop Delay (ms)", &hard_drop_delay_ms_, 0, 500));
  widgets_.push_back(std::make_unique<NumericInputWidget>(
      "Max Lock Resets", &settings_.game.max_lock_resets, 0, 100));
  widgets_.push_back(std::make_unique<BoolToggleWidget>(
      "Infinite Hold", &settings_.game.infinite_hold));

  // Layout all widgets — pixel-native logical presentation. Multiply
  // layout constants by kScale so the UI scales with the user setting.
  const float S = L::kScale;
  float y = 60.f * S;
  float x = 40.f * S;
  float w = L::kWindowW - 80.f * S;

  for (auto &widget : widgets_) {
    y += widget->layout(x, y, w);
    y += 4.f * S;
  }
  content_height_ = y + 40.f * S;
}

float SettingsEditor::logical_height() const { return L::kWindowH; }

void SettingsEditor::run() {
  handle_resize(renderer_, settings_.auto_scale);
  build_widgets();

  // Enable text input so NumericInputWidget can receive TEXT_INPUT events
  SDL_StartTextInput(window_);

  auto mouse_logical = [&]() -> Vec2f {
    float wx, wy;
    SDL_GetMouseState(&wx, &wy);
    float lx, ly;
    SDL_RenderCoordinatesFromWindow(renderer_, wx, wy, &lx, &ly);
    return {lx, ly - scroll_y_};
  };

  bool running = true;
  while (running) {
    if (dirty_) {
      draw();
      dirty_ = false;
    }

    SDL_Event ev;
    if (!SDL_WaitEvent(&ev))
      continue;

    // Drain all queued events before the next redraw so hover/mouse
    // state doesn't lag behind a backlog of motion events.
    do {
    switch (ev.type) {
    case SDL_EVENT_QUIT:
      SDL_StopTextInput(window_);
      running = false;
      return;

    case SDL_EVENT_WINDOW_RESIZED:
      handle_resize(renderer_, settings_.auto_scale);
      dirty_ = true;
      continue;

    case SDL_EVENT_KEY_DOWN: {
      Vec2f m = mouse_logical();
      if (ev.key.key == SDLK_ESCAPE) {
        // If a widget is listening/editing, let it handle first
        bool consumed = false;
        for (auto &w : widgets_) {
          if (w->handle_event(ev, m)) {
            consumed = true;
            break;
          }
        }
        if (!consumed) {
          save_settings();
          SDL_StopTextInput(window_);
          return;
        }
        dirty_ = true;
        continue;
      }
      for (auto &w : widgets_) {
        if (w->handle_event(ev, m))
          break;
      }
      dirty_ = true;
      break;
    }

    case SDL_EVENT_TEXT_INPUT: {
      Vec2f m = mouse_logical();
      for (auto &w : widgets_) {
        if (w->handle_event(ev, m))
          break;
      }
      dirty_ = true;
      break;
    }

    case SDL_EVENT_MOUSE_WHEEL: {
      scroll_y_ += ev.wheel.y * 30.f;
      float max_scroll = 0.f;
      float min_scroll = -(content_height_ - logical_height());
      if (min_scroll > 0.f)
        min_scroll = 0.f;
      scroll_y_ = std::clamp(scroll_y_, min_scroll, max_scroll);
      dirty_ = true;
      continue;
    }

    case SDL_EVENT_MOUSE_MOTION: {
      Vec2f m = mouse_logical();
      int new_hover = -1;
      for (int i = 0; i < static_cast<int>(widgets_.size()); ++i) {
        if (widgets_[i]->contains(m)) {
          new_hover = i;
          break;
        }
      }
      if (new_hover != hovered_idx_) {
        hovered_idx_ = new_hover;
        for (int i = 0; i < static_cast<int>(widgets_.size()); ++i)
          widgets_[i]->set_hovered(i == hovered_idx_);
        dirty_ = true;
      }
      continue;
    }

    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
      // Deactivate all widgets before forwarding clicks
      for (auto &w : widgets_)
        w->deactivate();

      Vec2f m = mouse_logical();
      for (auto &w : widgets_) {
        if (w->handle_event(ev, m))
          break;
      }
      dirty_ = true;
      break;
    }
    }
    } while (running && SDL_PollEvent(&ev));
  }

  SDL_StopTextInput(window_);
}

void SettingsEditor::draw() {
  SDL_SetRenderDrawColor(renderer_, 20, 20, 20, 255);
  SDL_RenderClear(renderer_);

  const float S = L::kScale;

  // Title (fixed, not scrolled)
  if (const CachedText *t =
          get_text(renderer_, "SETTINGS", L::kFontXL, Color(200, 200, 200))) {
    SDL_FRect dst = {40.f * S, 10.f * S + scroll_y_, t->w, t->h};
    SDL_RenderTexture(renderer_, t->tex, nullptr, &dst);
  }

  // Draw widgets (with scroll offset)
  for (auto &w : widgets_)
    w->draw(renderer_, scroll_y_);

  // Hint at bottom (fixed position)
  if (const CachedText *t = get_text(renderer_, "ESC to save & return",
                                     L::kFontS, Color(120, 120, 120))) {
    SDL_FRect dst = {40.f * S, logical_height() - 25.f * S, t->w, t->h};
    SDL_RenderTexture(renderer_, t->tex, nullptr, &dst);
  }

  SDL_RenderPresent(renderer_);
}

void SettingsEditor::save_settings() {
  // Sync adapter ints back to settings
  settings_.das = std::chrono::milliseconds(das_ms_);
  settings_.arr = std::chrono::milliseconds(arr_ms_);
  settings_.soft_drop_interval = std::chrono::milliseconds(soft_drop_ms_);
  settings_.game.gravity_interval = std::chrono::milliseconds(gravity_ms_);
  settings_.game.lock_delay = std::chrono::milliseconds(lock_delay_ms_);
  settings_.game.garbage_delay = std::chrono::milliseconds(garbage_delay_ms_);
  settings_.game.hard_drop_delay =
      std::chrono::milliseconds(hard_drop_delay_ms_);
  settings_.ghost_opacity =
      static_cast<uint8_t>(std::clamp(ghost_opacity_pct_, 0, 100) * 255 / 100);
  settings_.grid_opacity =
      static_cast<uint8_t>(std::clamp(grid_opacity_pct_, 0, 100) * 255 / 100);
  settings_.scale_factor =
      std::clamp(static_cast<float>(scale_pct_) / 100.f, 0.5f, 4.f);

  settings_.save("settings.ini");

  // Apply scale changes immediately so the caller sees an updated window.
  L::init(settings_.scale_factor);
  if (!settings_.auto_scale) {
    SDL_SetWindowSize(window_, static_cast<int>(L::kWindowW),
                      static_cast<int>(L::kWindowH));
    // Wayland applies size changes asynchronously; block until the
    // compositor confirms so the caller's subsequent logical
    // presentation matches the real window size.
    SDL_SyncWindow(window_);
  }
  handle_resize(renderer_, settings_.auto_scale);
}
