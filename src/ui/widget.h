#pragma once

#include "render/font.h"
#include "render/renderer.h"
#include "settings.h"
#include <SFML/Graphics.hpp>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Widget base
// ---------------------------------------------------------------------------
class Widget {
public:
  virtual ~Widget() = default;

  // Returns true if event was consumed
  virtual bool handle_event(const sf::Event &event, sf::Vector2f mouse) = 0;
  virtual void draw(sf::RenderWindow &window) = 0;

  // Lay out at (x, y) with given width; returns height consumed
  virtual float layout(float x, float y, float width) = 0;

  // Cancel any active editing/listening state
  virtual void deactivate() {}

  sf::FloatRect bounds() const { return bounds_; }
  void set_hovered(bool h) { hovered_ = h; }

protected:
  void draw_hover_bg(sf::RenderWindow &window) {
    if (!hovered_)
      return;
    sf::RectangleShape bg(bounds_.size);
    bg.setPosition(bounds_.position);
    bg.setFillColor(sf::Color(255, 255, 255, 15));
    window.draw(bg);
  }

  sf::FloatRect bounds_;
  std::string label_;
  bool hovered_ = false;
};

// ---------------------------------------------------------------------------
// SectionHeader — non-interactive divider
// ---------------------------------------------------------------------------
class SectionHeader : public Widget {
public:
  explicit SectionHeader(const std::string &label) { label_ = label; }

  bool handle_event(const sf::Event &, sf::Vector2f) override { return false; }

  float layout(float x, float y, float width) override {
    constexpr float kHeight = 36.f;
    bounds_ = sf::FloatRect({x, y}, {width, kHeight});
    return kHeight;
  }

  void draw(sf::RenderWindow &window) override {
    sf::Text text(get_font(), label_, RenderLayout::kBaseFontL);
    text.setFillColor(sf::Color(180, 180, 80));
    text.setPosition({bounds_.position.x, bounds_.position.y + 6.f});
    window.draw(text);
  }
};

// ---------------------------------------------------------------------------
// KeyBindWidget — click to listen, press key to bind
// ---------------------------------------------------------------------------
class KeyBindWidget : public Widget {
public:
  KeyBindWidget(const std::string &label, sf::Keyboard::Key *target)
      : target_(target) {
    label_ = label;
  }

  void deactivate() override { listening_ = false; }

  bool handle_event(const sf::Event &event, sf::Vector2f mouse) override {
    if (listening_) {
      if (auto *kp = event.getIf<sf::Event::KeyPressed>()) {
        if (kp->code == sf::Keyboard::Key::Escape) {
          listening_ = false;
          return true;
        }
        *target_ = kp->code;
        listening_ = false;
        return true;
      }
      if (event.getIf<sf::Event::MouseButtonPressed>()) {
        listening_ = false;
        return true;
      }
      return true; // Consume everything while listening
    }

    if (auto *mb = event.getIf<sf::Event::MouseButtonPressed>()) {
      if (mb->button == sf::Mouse::Button::Left &&
          bounds_.contains(mouse)) {
        listening_ = true;
        return true;
      }
    }
    return false;
  }

  float layout(float x, float y, float width) override {
    constexpr float kHeight = 30.f;
    bounds_ = sf::FloatRect({x, y}, {width, kHeight});
    return kHeight;
  }

  void draw(sf::RenderWindow &window) override {
    draw_hover_bg(window);

    sf::Text lbl(get_font(), label_, RenderLayout::kBaseFontM);
    lbl.setFillColor(sf::Color(200, 200, 200));
    lbl.setPosition({bounds_.position.x, bounds_.position.y + 4.f});
    window.draw(lbl);

    std::string val_str =
        listening_ ? "[ press a key ]" : key_to_string(*target_);
    sf::Text val(get_font(), val_str, RenderLayout::kBaseFontM);
    val.setFillColor(listening_ ? sf::Color(255, 255, 100)
                                : sf::Color(255, 255, 255));
    val.setPosition({bounds_.position.x + 260.f, bounds_.position.y + 4.f});
    window.draw(val);
  }

private:
  sf::Keyboard::Key *target_;
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

  bool handle_event(const sf::Event &event, sf::Vector2f mouse) override {
    if (editing_) {
      if (auto *kp = event.getIf<sf::Event::KeyPressed>()) {
        if (kp->code == sf::Keyboard::Key::Enter) {
          commit();
          return true;
        }
        if (kp->code == sf::Keyboard::Key::Escape) {
          editing_ = false;
          return true;
        }
        if (kp->code == sf::Keyboard::Key::Backspace && !buf_.empty()) {
          buf_.pop_back();
          return true;
        }
      }
      if (auto *te = event.getIf<sf::Event::TextEntered>()) {
        char c = static_cast<char>(te->unicode);
        if (c >= '0' && c <= '9') {
          buf_ += c;
          return true;
        }
        if (c == '-' && buf_.empty()) {
          buf_ += c;
          return true;
        }
      }
      if (auto *mb = event.getIf<sf::Event::MouseButtonPressed>()) {
        if (!bounds_.contains(mouse)) {
          commit();
          return false;
        }
      }
      return true;
    }

    if (auto *mb = event.getIf<sf::Event::MouseButtonPressed>()) {
      if (mb->button == sf::Mouse::Button::Left &&
          bounds_.contains(mouse)) {
        editing_ = true;
        buf_ = std::to_string(*target_);
        return true;
      }
    }
    return false;
  }

  float layout(float x, float y, float width) override {
    constexpr float kHeight = 30.f;
    bounds_ = sf::FloatRect({x, y}, {width, kHeight});
    return kHeight;
  }

  void draw(sf::RenderWindow &window) override {
    draw_hover_bg(window);

    sf::Text lbl(get_font(), label_, RenderLayout::kBaseFontM);
    lbl.setFillColor(sf::Color(200, 200, 200));
    lbl.setPosition({bounds_.position.x, bounds_.position.y + 4.f});
    window.draw(lbl);

    std::string val_str = editing_ ? buf_ + "_" : std::to_string(*target_);
    sf::Text val(get_font(), val_str, RenderLayout::kBaseFontM);
    val.setFillColor(editing_ ? sf::Color(255, 255, 100)
                               : sf::Color(255, 255, 255));
    val.setPosition({bounds_.position.x + 260.f, bounds_.position.y + 4.f});
    window.draw(val);
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

  bool handle_event(const sf::Event &event, sf::Vector2f mouse) override {
    if (auto *mb = event.getIf<sf::Event::MouseButtonPressed>()) {
      if (mb->button == sf::Mouse::Button::Left &&
          bounds_.contains(mouse)) {
        *target_ = !*target_;
        return true;
      }
    }
    return false;
  }

  float layout(float x, float y, float width) override {
    constexpr float kHeight = 30.f;
    bounds_ = sf::FloatRect({x, y}, {width, kHeight});
    return kHeight;
  }

  void draw(sf::RenderWindow &window) override {
    draw_hover_bg(window);

    sf::Text lbl(get_font(), label_, RenderLayout::kBaseFontM);
    lbl.setFillColor(sf::Color(200, 200, 200));
    lbl.setPosition({bounds_.position.x, bounds_.position.y + 4.f});
    window.draw(lbl);

    sf::Text val(get_font(), *target_ ? "ON" : "OFF", RenderLayout::kBaseFontM);
    val.setFillColor(*target_ ? sf::Color(100, 255, 100)
                               : sf::Color(255, 100, 100));
    val.setPosition({bounds_.position.x + 260.f, bounds_.position.y + 4.f});
    window.draw(val);
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
                   const std::string &base_dir)
      : skin_path_(skin_path), skin_paths_(std::move(skin_paths)),
        base_dir_(base_dir) {
    label_ = label;
    // Find current index
    for (int i = 0; i < static_cast<int>(skin_paths_.size()); ++i) {
      if (skin_paths_[i] == *skin_path_) {
        index_ = i;
        break;
      }
    }
    load_preview();
  }

  bool handle_event(const sf::Event &event, sf::Vector2f mouse) override {
    if (skin_paths_.empty())
      return false;
    if (auto *mb = event.getIf<sf::Event::MouseButtonPressed>()) {
      if (mb->button == sf::Mouse::Button::Left &&
          bounds_.contains(mouse)) {
        index_ = (index_ + 1) % static_cast<int>(skin_paths_.size());
        *skin_path_ = skin_paths_[index_];
        load_preview();
        return true;
      }
      if (mb->button == sf::Mouse::Button::Right &&
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
    constexpr float kHeight = 70.f;
    bounds_ = sf::FloatRect({x, y}, {width, kHeight});
    return kHeight;
  }

  void draw(sf::RenderWindow &window) override {
    using L = RenderLayout;
    draw_hover_bg(window);

    sf::Text lbl(get_font(), label_, RenderLayout::kBaseFontM);
    lbl.setFillColor(sf::Color(200, 200, 200));
    lbl.setPosition({bounds_.position.x, bounds_.position.y + 4.f});
    window.draw(lbl);

    // Skin name
    sf::Text val(get_font(), *skin_path_, RenderLayout::kBaseFontS);
    val.setFillColor(sf::Color(180, 180, 180));
    val.setPosition({bounds_.position.x + 260.f, bounds_.position.y + 4.f});
    window.draw(val);

    // Preview strip: 7 tiles (one per piece type)
    if (preview_ok_) {
      float px = bounds_.position.x + 260.f;
      float py = bounds_.position.y + 26.f;
      float tile_draw_size = 28.f;

      for (int i = 0; i < 7; ++i) {
        sf::Sprite sprite(preview_tex_);
        sprite.setTextureRect(sf::IntRect(
            {i * L::kSkinPitch, 0}, {L::kSkinTile, L::kSkinTile}));
        float scale = tile_draw_size / L::kSkinTile;
        sprite.setScale({scale, scale});
        sprite.setPosition({px + i * (tile_draw_size + 2.f), py});
        window.draw(sprite);
      }
    }

    // Hint — below the preview strip
    sf::Text hint(get_font(), "L/R click to cycle", RenderLayout::kBaseFontS);
    hint.setFillColor(sf::Color(120, 120, 120));
    hint.setPosition({bounds_.position.x + 260.f, bounds_.position.y + 56.f});
    window.draw(hint);
  }

private:
  void load_preview() {
    if (index_ >= 0 && index_ < static_cast<int>(skin_paths_.size())) {
      std::string full =
          (std::filesystem::path(base_dir_) / skin_paths_[index_]).string();
      preview_ok_ = preview_tex_.loadFromFile(full);
    }
  }

  std::string *skin_path_;
  std::vector<std::string> skin_paths_;
  std::string base_dir_;
  int index_ = 0;
  sf::Texture preview_tex_;
  bool preview_ok_ = false;
};
