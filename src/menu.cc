#include "menu.h"
#include "presets/presets.h"
#include "render/font.h"
#include "render/renderer.h"

using L = RenderLayout;

Menu::Menu(sf::RenderWindow &window, Settings &settings)
    : window_(window), settings_(settings), font_(get_font()) {
  modes_ = all_modes();
}

std::unique_ptr<GameMode> Menu::run() {
  while (window_.isOpen()) {
    draw();

    if (auto sfml_ev = window_.waitEvent()) {
      if (sfml_ev->is<sf::Event::Closed>()) {
        window_.close();
      } else if (auto *r = sfml_ev->getIf<sf::Event::Resized>()) {
        handle_resize(window_, r->size.x, r->size.y);
      } else if (auto *kp = sfml_ev->getIf<sf::Event::KeyPressed>()) {
        if (auto mode = handle_key(kp->code)) {
          return mode;
        }
      }
    }
  }
  return nullptr;
}

std::unique_ptr<GameMode> Menu::handle_key(sf::Keyboard::Key key) {
  using K = sf::Keyboard::Key;

  if (key == K::Up) {
    if (cursor_ > 0)
      --cursor_;
  } else if (key == K::Down) {
    int max_idx = (state_ == State::Main)
                      ? 1
                      : static_cast<int>(modes_.size()) - 1;
    if (cursor_ < max_idx)
      ++cursor_;
  } else if (key == K::Enter) {
    if (state_ == State::Main) {
      if (cursor_ == 0) {
        state_ = State::PresetSelect;
        cursor_ = 0;
      }
    } else if (state_ == State::PresetSelect) {
      return make_selected_mode();
    }
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

  if (state_ == State::Main) {
    draw_items({"Play", "Settings"});
  } else {
    std::vector<std::string> items;
    for (auto &m : modes_)
      items.push_back(m->title());
    draw_items(items);
  }

  window_.display();
}

void Menu::draw_items(const std::vector<std::string> &items) {
  constexpr unsigned kFontSize = 28;
  constexpr float kLineH = 40.f;

  float total_h = static_cast<float>(items.size()) * kLineH;
  float start_y = (static_cast<float>(L::kWindowH) - total_h) / 2.f;
  float center_x = static_cast<float>(L::kWindowW) / 2.f;

  sf::Text title(font_, "CITRIS", 48);
  title.setFillColor(sf::Color(200, 200, 200));
  auto title_bounds = title.getLocalBounds();
  title.setPosition({center_x - title_bounds.size.x / 2.f, start_y - 100.f});
  window_.draw(title);

  for (int i = 0; i < static_cast<int>(items.size()); ++i) {
    sf::Text text(font_, items[i], kFontSize);
    bool selected = (i == cursor_);
    text.setFillColor(selected ? sf::Color(255, 255, 100)
                               : sf::Color(180, 180, 180));

    auto bounds = text.getLocalBounds();
    float x = center_x - bounds.size.x / 2.f;
    float y = start_y + i * kLineH;
    text.setPosition({x, y});

    if (selected) {
      sf::Text arrow(font_, "> ", kFontSize);
      arrow.setFillColor(sf::Color(255, 255, 100));
      arrow.setPosition({x - 30.f, y});
      window_.draw(arrow);
    }

    window_.draw(text);
  }
}
