#pragma once

#include "render/font.h"
#include "render/renderer.h"
#include "sdl_types.h"
#include "settings.h"
#include "vec2.h"
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

struct WidgetRect {
  float x = 0, y = 0, w = 0, h = 0;
  bool contains(Vec2f p) const {
    return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h;
  }
};

// All widgets live in a pixel-native logical space (DISABLED logical
// presentation). They multiply their visual constants by RenderLayout::kScale
// so that a single UI scales consistently with the user's scale factor.

// ---------------------------------------------------------------------------
// Widget base
// ---------------------------------------------------------------------------
class Widget {
public:
  virtual ~Widget() = default;

  // Returns true if event was consumed
  virtual bool handle_event(const SDL_Event &event, Vec2f mouse) = 0;
  virtual void draw(SDL_Renderer *renderer, float scroll_y) = 0;

  // Lay out at (x, y) with given width; returns height consumed
  virtual float layout(float x, float y, float width) = 0;

  // Cancel any active editing/listening state
  virtual void deactivate() {}

  WidgetRect bounds() const { return bounds_; }
  bool contains(Vec2f p) const { return bounds_.contains(p); }
  void set_hovered(bool h) { hovered_ = h; }

protected:
  static float s() { return RenderLayout::kScale; }

  void draw_hover_bg(SDL_Renderer *renderer, float scroll_y) {
    if (!hovered_)
      return;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 15);
    SDL_FRect r = {bounds_.x, bounds_.y + scroll_y, bounds_.w, bounds_.h};
    SDL_RenderFillRect(renderer, &r);
  }

  static void draw_text_at(SDL_Renderer *renderer, const char *str,
                            unsigned size, float x, float y, Color color) {
    const CachedText *t = get_text(renderer, str, size, color);
    if (!t)
      return;
    SDL_FRect dst = {x, y, t->w, t->h};
    SDL_RenderTexture(renderer, t->tex, nullptr, &dst);
  }

  WidgetRect bounds_;
  std::string label_;
  bool hovered_ = false;
};

// ---------------------------------------------------------------------------
// SectionHeader — non-interactive divider
// ---------------------------------------------------------------------------
class SectionHeader : public Widget {
public:
  explicit SectionHeader(const std::string &label) { label_ = label; }

  bool handle_event(const SDL_Event &, Vec2f) override { return false; }

  float layout(float x, float y, float width) override {
    float h = 36.f * s();
    bounds_ = {x, y, width, h};
    return h;
  }

  void draw(SDL_Renderer *renderer, float scroll_y) override {
    draw_text_at(renderer, label_.c_str(), RenderLayout::kFontL, bounds_.x,
                 bounds_.y + 6.f * s() + scroll_y, Color(180, 180, 80));
  }
};

// ---------------------------------------------------------------------------
// KeyBindWidget — click to listen, press key to bind
// ---------------------------------------------------------------------------
class KeyBindWidget : public Widget {
public:
  KeyBindWidget(const std::string &label, KeyCode *target) : target_(target) {
    label_ = label;
  }

  void deactivate() override { listening_ = false; }

  bool handle_event(const SDL_Event &event, Vec2f mouse) override {
    if (listening_) {
      if (event.type == SDL_EVENT_KEY_DOWN) {
        if (event.key.key == SDLK_ESCAPE) {
          listening_ = false;
          return true;
        }
        *target_ = event.key.key;
        listening_ = false;
        return true;
      }
      if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        listening_ = false;
        return true;
      }
      return true; // Consume everything while listening
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
      if (event.button.button == SDL_BUTTON_LEFT &&
          bounds_.contains(mouse)) {
        listening_ = true;
        return true;
      }
    }
    return false;
  }

  float layout(float x, float y, float width) override {
    float h = 30.f * s();
    bounds_ = {x, y, width, h};
    return h;
  }

  void draw(SDL_Renderer *renderer, float scroll_y) override {
    draw_hover_bg(renderer, scroll_y);

    float dy = bounds_.y + 4.f * s() + scroll_y;
    draw_text_at(renderer, label_.c_str(), RenderLayout::kFontM, bounds_.x,
                 dy, Color(200, 200, 200));

    std::string val_str =
        listening_ ? "[ press a key ]" : key_to_string(*target_);
    Color val_color = listening_ ? Color(255, 255, 100) : Color(255, 255, 255);
    draw_text_at(renderer, val_str.c_str(), RenderLayout::kFontM,
                 bounds_.x + 260.f * s(), dy, val_color);
  }

private:
  KeyCode *target_;
  bool listening_ = false;
};

// ---------------------------------------------------------------------------
// NumericInputWidget — click to edit, type digits, Enter commits
// ---------------------------------------------------------------------------
class NumericInputWidget : public Widget {
public:
  NumericInputWidget(const std::string &label, int *target, int min_val,
                     int max_val)
      : target_(target), min_(min_val), max_(max_val) {
    label_ = label;
  }

  void deactivate() override {
    if (editing_)
      commit();
  }

  bool handle_event(const SDL_Event &event, Vec2f mouse) override {
    if (editing_) {
      if (event.type == SDL_EVENT_KEY_DOWN) {
        if (event.key.key == SDLK_RETURN) {
          commit();
          return true;
        }
        if (event.key.key == SDLK_ESCAPE) {
          editing_ = false;
          return true;
        }
        if (event.key.key == SDLK_BACKSPACE && !buf_.empty()) {
          buf_.pop_back();
          return true;
        }
      }
      if (event.type == SDL_EVENT_TEXT_INPUT) {
        const char *text = event.text.text;
        if (text && text[0]) {
          char c = text[0];
          if (c >= '0' && c <= '9') {
            buf_ += c;
            return true;
          }
          if (c == '-' && buf_.empty()) {
            buf_ += c;
            return true;
          }
        }
      }
      if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (!bounds_.contains(mouse)) {
          commit();
          return false;
        }
      }
      return true;
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
      if (event.button.button == SDL_BUTTON_LEFT &&
          bounds_.contains(mouse)) {
        editing_ = true;
        buf_ = std::to_string(*target_);
        return true;
      }
    }
    return false;
  }

  float layout(float x, float y, float width) override {
    float h = 30.f * s();
    bounds_ = {x, y, width, h};
    return h;
  }

  void draw(SDL_Renderer *renderer, float scroll_y) override {
    draw_hover_bg(renderer, scroll_y);

    float dy = bounds_.y + 4.f * s() + scroll_y;
    draw_text_at(renderer, label_.c_str(), RenderLayout::kFontM, bounds_.x,
                 dy, Color(200, 200, 200));

    std::string val_str = editing_ ? buf_ + "_" : std::to_string(*target_);
    Color val_color = editing_ ? Color(255, 255, 100) : Color(255, 255, 255);
    draw_text_at(renderer, val_str.c_str(), RenderLayout::kFontM,
                 bounds_.x + 260.f * s(), dy, val_color);
  }

private:
  void commit() {
    editing_ = false;
    try {
      int v = std::stoi(buf_);
      *target_ = std::clamp(v, min_, max_);
    } catch (...) {
    }
  }

  int *target_;
  int min_, max_;
  bool editing_ = false;
  std::string buf_;
};

// ---------------------------------------------------------------------------
// BoolToggleWidget — click to toggle
// ---------------------------------------------------------------------------
class BoolToggleWidget : public Widget {
public:
  BoolToggleWidget(const std::string &label, bool *target) : target_(target) {
    label_ = label;
  }

  bool handle_event(const SDL_Event &event, Vec2f mouse) override {
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
      if (event.button.button == SDL_BUTTON_LEFT &&
          bounds_.contains(mouse)) {
        *target_ = !*target_;
        return true;
      }
    }
    return false;
  }

  float layout(float x, float y, float width) override {
    float h = 30.f * s();
    bounds_ = {x, y, width, h};
    return h;
  }

  void draw(SDL_Renderer *renderer, float scroll_y) override {
    draw_hover_bg(renderer, scroll_y);

    float dy = bounds_.y + 4.f * s() + scroll_y;
    draw_text_at(renderer, label_.c_str(), RenderLayout::kFontM, bounds_.x,
                 dy, Color(200, 200, 200));

    const char *val_str = *target_ ? "ON" : "OFF";
    Color val_color = *target_ ? Color(100, 255, 100) : Color(255, 100, 100);
    draw_text_at(renderer, val_str, RenderLayout::kFontM,
                 bounds_.x + 260.f * s(), dy, val_color);
  }

private:
  bool *target_;
};

// ---------------------------------------------------------------------------
// SkinPickerWidget — cycle through skins with preview
// ---------------------------------------------------------------------------
class SkinPickerWidget : public Widget {
public:
  SkinPickerWidget(const std::string &label, std::string *skin_path,
                   std::vector<std::string> skin_paths,
                   const std::string &base_dir, SDL_Renderer *renderer)
      : skin_path_(skin_path), skin_paths_(std::move(skin_paths)),
        base_dir_(base_dir), renderer_(renderer) {
    label_ = label;
    for (int i = 0; i < static_cast<int>(skin_paths_.size()); ++i) {
      if (skin_paths_[i] == *skin_path_) {
        index_ = i;
        break;
      }
    }
    load_preview();
  }

  ~SkinPickerWidget() override {
    if (preview_tex_)
      SDL_DestroyTexture(preview_tex_);
  }

  bool handle_event(const SDL_Event &event, Vec2f mouse) override {
    if (skin_paths_.empty())
      return false;
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
      if (event.button.button == SDL_BUTTON_LEFT &&
          bounds_.contains(mouse)) {
        index_ = (index_ + 1) % static_cast<int>(skin_paths_.size());
        *skin_path_ = skin_paths_[index_];
        load_preview();
        return true;
      }
      if (event.button.button == SDL_BUTTON_RIGHT &&
          bounds_.contains(mouse)) {
        index_ = (index_ - 1 + static_cast<int>(skin_paths_.size())) %
                 static_cast<int>(skin_paths_.size());
        *skin_path_ = skin_paths_[index_];
        load_preview();
        return true;
      }
    }
    return false;
  }

  float layout(float x, float y, float width) override {
    float h = 70.f * s();
    bounds_ = {x, y, width, h};
    return h;
  }

  void draw(SDL_Renderer *renderer, float scroll_y) override {
    using L = RenderLayout;
    draw_hover_bg(renderer, scroll_y);

    float dy = bounds_.y + scroll_y;
    draw_text_at(renderer, label_.c_str(), L::kFontM, bounds_.x,
                 dy + 4.f * s(), Color(200, 200, 200));

    draw_text_at(renderer, skin_path_->c_str(), L::kFontS,
                 bounds_.x + 260.f * s(), dy + 4.f * s(),
                 Color(180, 180, 180));

    if (preview_ok_ && preview_tex_) {
      float px = bounds_.x + 260.f * s();
      float py = dy + 26.f * s();
      float tile_draw_size = 28.f * s();

      for (int i = 0; i < 7; ++i) {
        SDL_FRect src = {static_cast<float>(i * L::kSkinPitch), 0.f,
                         static_cast<float>(L::kSkinTile),
                         static_cast<float>(L::kSkinTile)};
        SDL_FRect dst = {px + i * (tile_draw_size + 2.f * s()), py,
                         tile_draw_size, tile_draw_size};
        SDL_RenderTexture(renderer, preview_tex_, &src, &dst);
      }
    }

    draw_text_at(renderer, "L/R click to cycle", L::kFontS,
                 bounds_.x + 260.f * s(), dy + 56.f * s(),
                 Color(120, 120, 120));
  }

private:
  void load_preview() {
    if (preview_tex_) {
      SDL_DestroyTexture(preview_tex_);
      preview_tex_ = nullptr;
      preview_ok_ = false;
    }
    if (index_ >= 0 && index_ < static_cast<int>(skin_paths_.size())) {
      std::string full =
          (std::filesystem::path(base_dir_) / skin_paths_[index_]).string();
      preview_tex_ = IMG_LoadTexture(renderer_, full.c_str());
      preview_ok_ = (preview_tex_ != nullptr);
      if (preview_ok_)
        SDL_SetTextureScaleMode(preview_tex_, SDL_SCALEMODE_NEAREST);
    }
  }

  std::string *skin_path_;
  std::vector<std::string> skin_paths_;
  std::string base_dir_;
  SDL_Renderer *renderer_;
  int index_ = 0;
  SDL_Texture *preview_tex_ = nullptr;
  bool preview_ok_ = false;
};

// ---------------------------------------------------------------------------
// MenuButtonWidget — centered clickable text row, emits click events
// ---------------------------------------------------------------------------
class MenuButtonWidget : public Widget {
public:
  MenuButtonWidget(std::string label, unsigned font_size)
      : font_size_(font_size) {
    label_ = std::move(label);
  }

  void set_selected(bool s) { selected_ = s; }
  bool take_click() {
    bool c = clicked_;
    clicked_ = false;
    return c;
  }

  bool handle_event(const SDL_Event &event, Vec2f mouse) override {
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event.button.button == SDL_BUTTON_LEFT &&
        bounds_.contains(mouse)) {
      clicked_ = true;
      return true;
    }
    return false;
  }

  float layout(float x, float y, float width) override {
    float h = static_cast<float>(font_size_) * 2.2f;
    bounds_ = {x, y, width, h};
    return h;
  }

  void draw(SDL_Renderer *renderer, float scroll_y) override {
    Color color = selected_ ? Color(255, 255, 100) : Color(180, 180, 180);
    const CachedText *t =
        get_text(renderer, label_.c_str(), font_size_, color);
    if (!t)
      return;
    float cx = bounds_.x + bounds_.w / 2.f;
    float y = bounds_.y + (bounds_.h - t->h) / 2.f + scroll_y;
    SDL_FRect dst = {cx - t->w / 2.f, y, t->w, t->h};
    SDL_RenderTexture(renderer, t->tex, nullptr, &dst);

    if (selected_) {
      if (const CachedText *a = get_text(renderer, "> ", font_size_,
                                         Color(255, 255, 100))) {
        SDL_FRect adst = {dst.x - a->w, y, a->w, a->h};
        SDL_RenderTexture(renderer, a->tex, nullptr, &adst);
      }
    }
  }

private:
  unsigned font_size_;
  bool selected_ = false;
  bool clicked_ = false;
};
