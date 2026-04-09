#include "menu.h"
#include "presets/presets.h"
#include "render/font.h"
#include "render/renderer.h"
#include "ui/settings_editor.h"
#include <unistd.h>

using L = RenderLayout;

static float kLineH = 60.f * L::kScale;

Menu::Menu(sf::RenderWindow &window, Settings &settings)
    : window_(window), settings_(settings), font_(get_font()) {
  modes_ = all_modes();
}

float Menu::logical_height() const { return L::kWindowH; }

std::vector<std::string> Menu::current_items() const {
  if (state_ == State::Main)
    return {"Play", "Settings"};
  std::vector<std::string> items;
  for (auto &m : modes_)
    items.push_back(m->title());
  return items;
}

std::unique_ptr<GameMode> Menu::run() {
  while (window_.isOpen()) {
    draw();

    if (auto sfml_ev = window_.waitEvent()) {
      if (sfml_ev->is<sf::Event::Closed>()) {
        window_.close();
      } else if (auto *r = sfml_ev->getIf<sf::Event::Resized>()) {
        // handle_resize(window_, r->size.x, r->size.y, settings_.auto_scale);
      } else if (auto *kp = sfml_ev->getIf<sf::Event::KeyPressed>()) {
        if (auto mode = handle_key(kp->code)) {
          restore_game_view();
          return mode;
        }
      } else if (auto *mb = sfml_ev->getIf<sf::Event::MouseButtonPressed>()) {
        if (mb->button == sf::Mouse::Button::Left) {
          auto mouse =
              window_.mapPixelToCoords(sf::Mouse::getPosition(window_));
          if (auto mode = handle_click(mouse)) {
            restore_game_view();
            return mode;
          }
        }
      } else if (auto *mm = sfml_ev->getIf<sf::Event::MouseMoved>()) {
        auto mouse =
            window_.mapPixelToCoords({mm->position.x, mm->position.y});
        update_hover(mouse);
      }
    }
  }
  return nullptr;
}

void Menu::update_hover(sf::Vector2f mouse) {
  auto items = current_items();
  float total_h = static_cast<float>(items.size()) * kLineH;
  float start_y = (logical_height() - total_h) / 2.f;

  for (int i = 0; i < static_cast<int>(items.size()); ++i) {
    float y = start_y + i * kLineH;
    if (mouse.y >= y && mouse.y < y + kLineH) {
      cursor_ = i;
      return;
    }
  }
}

std::unique_ptr<GameMode> Menu::handle_click(sf::Vector2f mouse) {
  auto items = current_items();
  float total_h = static_cast<float>(items.size()) * kLineH;
  float start_y = (logical_height() - total_h) / 2.f;

  for (int i = 0; i < static_cast<int>(items.size()); ++i) {
    float y = start_y + i * kLineH;
    if (mouse.y >= y && mouse.y < y + kLineH) {
      cursor_ = i;
      return activate_item(i);
    }
  }
  return nullptr;
}

std::unique_ptr<GameMode> Menu::activate_item(int index) {
  if (state_ == State::Main) {
    if (index == 0) {
      state_ = State::PresetSelect;
      cursor_ = 0;
    } else if (index == 1) {
      SettingsEditor editor(window_, settings_);
      editor.run();
      L::init(settings_.scale_factor);
      kLineH = 60.f * L::kScale;
      if (!settings_.auto_scale) {
        unsigned tw = static_cast<unsigned>(L::kWindowW);
        unsigned th = static_cast<unsigned>(L::kWindowH);
// #ifdef _WIN32
        window_.setSize({tw, th});
// #else
//         window_.create(sf::VideoMode({tw, th}), "Citris");
//         window_.setKeyRepeatEnabled(false);
// #endif
      }
      auto cur = window_.getSize();
      handle_resize(window_, cur.x, cur.y, settings_.auto_scale);
    }
  } else if (state_ == State::PresetSelect) {
    return make_selected_mode();
  }
  return nullptr;
}

void Menu::restore_game_view() {
  auto sz = window_.getSize();
  handle_resize(window_, sz.x, sz.y, settings_.auto_scale);
}

std::unique_ptr<GameMode> Menu::handle_key(sf::Keyboard::Key key) {
  using K = sf::Keyboard::Key;

  if (key == K::Up) {
    if (cursor_ > 0)
      --cursor_;
  } else if (key == K::Down) {
    int max_idx = static_cast<int>(current_items().size()) - 1;
    if (cursor_ < max_idx)
      ++cursor_;
  } else if (key == K::Enter) {
    return activate_item(cursor_);
  } else if (key == K::Backspace || key == K::Escape) {
    if (state_ == State::PresetSelect) {
      state_ = State::Main;
      cursor_ = 0;
    }
  }

  return nullptr;
}

std::unique_ptr<GameMode> Menu::make_selected_mode() {
  // Re-create the selected mode fresh (modes_ holds templates for display)
  // We need to rebuild so each game session gets fresh state
  auto fresh_modes = all_modes();
  if (cursor_ == 0) {
    // Apply INI tuning to freeplay
    if (auto *fp = dynamic_cast<FreeplayMode *>(fresh_modes[0].get())) {
      fp->set_gravity_interval(settings_.game.gravity_interval);
      fp->set_lock_delay(settings_.game.lock_delay);
      fp->set_garbage_delay(settings_.game.garbage_delay);
      fp->set_hard_drop_delay(settings_.game.hard_drop_delay);
      fp->set_max_lock_resets(settings_.game.max_lock_resets);
      fp->set_infinite_hold(settings_.game.infinite_hold);
    }
  }
  return std::move(fresh_modes[cursor_]);
}

void Menu::draw() {
  window_.clear(sf::Color(20, 20, 20));
  draw_items(current_items());
  window_.display();
}

void Menu::draw_items(const std::vector<std::string> &items) {
  float total_h = static_cast<float>(items.size()) * kLineH;
  float start_y = (logical_height() - total_h) / 2.f;
  float center_x = L::kWindowW / 2.f;

  sf::Text title(font_, "CITRIS", L::kFontXXL);
  title.setFillColor(sf::Color(200, 200, 200));
  auto title_bounds = title.getLocalBounds();
  title.setPosition({center_x - title_bounds.size.x / 2.f, start_y - 100.f * L::kScale});
  window_.draw(title);

  for (int i = 0; i < static_cast<int>(items.size()); ++i) {
    sf::Text text(font_, items[i], L::kFontXL);
    bool selected = (i == cursor_);
    text.setFillColor(selected ? sf::Color(255, 255, 100)
                               : sf::Color(180, 180, 180));

    auto bounds = text.getLocalBounds();
    float x = center_x - bounds.size.x / 2.f;
    float y = start_y + i * kLineH;
    text.setPosition({x, y});

    if (selected) {
      sf::Text arrow(font_, "> ", L::kFontXL);
      arrow.setFillColor(sf::Color(255, 255, 100));
      arrow.setPosition({x - 30.f * L::kScale, y});
      window_.draw(arrow);
    }

    window_.draw(text);
  }
}
