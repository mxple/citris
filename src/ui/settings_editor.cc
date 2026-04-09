#include "ui/settings_editor.h"
#include "render/font.h"
#include "render/renderer.h"
#include <filesystem>

using L = RenderLayout;

SettingsEditor::SettingsEditor(sf::RenderWindow &window, Settings &settings)
    : window_(window), settings_(settings), font_(get_font()) {
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
      "Skin", &settings_.skin_path, skin_paths, settings_.base_dir.string()));
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

  // Layout all widgets
  float y = 60.f;
  float x = 40.f;
  float w = window_.getView().getSize().x - 80.f;
  for (auto &widget : widgets_) {
    y += widget->layout(x, y, w);
    y += 4.f; // spacing
  }
  content_height_ = y + 40.f;
}

float SettingsEditor::logical_height() const {
  return window_.getView().getSize().y;
}

void SettingsEditor::run() {
  auto sz = window_.getSize();
  handle_resize_fill_width(window_, sz.x, sz.y, settings_.scale_factor);
  build_widgets();

  while (window_.isOpen()) {
    draw();

    if (auto sfml_ev = window_.waitEvent()) {
      if (sfml_ev->is<sf::Event::Closed>()) {
        window_.close();
        return;
      }

      if (auto *r = sfml_ev->getIf<sf::Event::Resized>()) {
        handle_resize_fill_width(window_, r->size.x, r->size.y,
                                 settings_.scale_factor);
        continue;
      }

      if (auto *kp = sfml_ev->getIf<sf::Event::KeyPressed>()) {
        if (kp->code == sf::Keyboard::Key::Escape) {
          // Check no widget is listening/editing — if so, let it handle first
          bool consumed = false;
          auto mouse =
              window_.mapPixelToCoords(sf::Mouse::getPosition(window_));
          mouse.y -= scroll_y_;
          for (auto &w : widgets_) {
            if (w->handle_event(*sfml_ev, mouse)) {
              consumed = true;
              break;
            }
          }
          if (!consumed) {
            save_settings();
            return;
          }
          continue;
        }
      }

      // Mouse scroll
      if (auto *scroll = sfml_ev->getIf<sf::Event::MouseWheelScrolled>()) {
        scroll_y_ += scroll->delta * 30.f;
        float max_scroll = 0.f;
        float min_scroll = -(content_height_ - logical_height());
        if (min_scroll > 0.f)
          min_scroll = 0.f;
        scroll_y_ = std::clamp(scroll_y_, min_scroll, max_scroll);
        continue;
      }

      // Update hover on mouse move
      if (sfml_ev->getIf<sf::Event::MouseMoved>()) {
        auto mouse =
            window_.mapPixelToCoords(sf::Mouse::getPosition(window_));
        mouse.y -= scroll_y_;
        for (auto &w : widgets_) {
          w->set_hovered(w->bounds().contains(mouse));
        }
        continue;
      }

      // Deactivate all widgets before forwarding mouse clicks
      if (sfml_ev->getIf<sf::Event::MouseButtonPressed>()) {
        for (auto &w : widgets_)
          w->deactivate();
      }

      // Forward to widgets
      auto mouse =
          window_.mapPixelToCoords(sf::Mouse::getPosition(window_));
      mouse.y -= scroll_y_;
      for (auto &w : widgets_) {
        if (w->handle_event(*sfml_ev, mouse))
          break;
      }
    }
  }
}

void SettingsEditor::draw() {
  window_.clear(sf::Color(20, 20, 20));

  // Use the current (fill) view as the base, apply scroll offset
  sf::View base_view = window_.getView();
  sf::View scrolled = base_view;
  scrolled.move({0.f, -scroll_y_});
  window_.setView(scrolled);

  // Title
  sf::Text title(font_, "SETTINGS", L::kBaseFontXL);
  title.setFillColor(sf::Color(200, 200, 200));
  title.setPosition({40.f, 10.f});
  window_.draw(title);

  // Draw widgets
  for (auto &w : widgets_)
    w->draw(window_);

  // Hint at bottom (fixed position, not scrolled)
  window_.setView(base_view);
  sf::Text hint(font_, "ESC to save & return", L::kBaseFontS);
  hint.setFillColor(sf::Color(120, 120, 120));
  hint.setPosition({40.f, logical_height() - 25.f});
  window_.draw(hint);

  window_.display();
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
}
