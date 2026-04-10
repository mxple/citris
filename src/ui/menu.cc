#include "ui/menu.h"
#include "presets/presets.h"
#include "render/font.h"
#include "render/renderer.h"
#include "ui/settings_editor.h"

using L = RenderLayout;

Menu::Menu(SDL_Renderer *renderer, SDL_Window *window, Settings &settings)
    : renderer_(renderer), window_(window), settings_(settings) {
  modes_ = all_modes();
  handle_resize(renderer_, settings_.auto_scale);
  rebuild_buttons();
}

void Menu::rebuild_buttons() {
  buttons_.clear();

  std::vector<std::string> labels;
  if (state_ == State::Main) {
    labels = {"Play", "Settings"};
  } else {
    for (auto &m : modes_)
      labels.push_back(m->title());
  }

  for (auto &l : labels)
    buttons_.push_back(std::make_unique<MenuButtonWidget>(l, L::kFontXL));

  float width = L::kWindowW;
  float height = L::kWindowH;

  float total_h = 0.f;
  for (auto &b : buttons_)
    total_h += b->layout(0.f, 0.f, width);

  float y = (height - total_h) / 2.f;
  for (auto &b : buttons_) {
    float h = b->layout(0.f, y, width);
    y += h;
  }

  if (cursor_ >= static_cast<int>(buttons_.size()))
    cursor_ = 0;
  update_cursor_selection();
}

void Menu::update_cursor_selection() {
  for (int i = 0; i < static_cast<int>(buttons_.size()); ++i)
    buttons_[i]->set_selected(i == cursor_);
}

std::unique_ptr<GameMode> Menu::run() {
  while (running_) {
    if (dirty_) {
      draw();
      dirty_ = false;
    }

    SDL_Event ev;
    if (!SDL_WaitEvent(&ev))
      continue;

    do {
      switch (ev.type) {
      case SDL_EVENT_QUIT:
        running_ = false;
        return nullptr;

      case SDL_EVENT_WINDOW_EXPOSED:
      case SDL_EVENT_WINDOW_RESIZED:
        dirty_ = true;
        break;

      case SDL_EVENT_KEY_DOWN:
        if (!ev.key.repeat) {
          KeyCode k = ev.key.key;
          if (k == SDLK_UP && cursor_ > 0) {
            --cursor_;
            update_cursor_selection();
            dirty_ = true;
          } else if (k == SDLK_DOWN &&
                     cursor_ < static_cast<int>(buttons_.size()) - 1) {
            ++cursor_;
            update_cursor_selection();
            dirty_ = true;
          } else if (k == SDLK_RETURN) {
            if (auto mode = activate_item(cursor_)) {
              handle_resize(renderer_, settings_.auto_scale);
              return mode;
            }
            dirty_ = true;
          } else if (k == SDLK_BACKSPACE || k == SDLK_ESCAPE) {
            if (state_ == State::PresetSelect) {
              state_ = State::Main;
              cursor_ = 0;
              rebuild_buttons();
              dirty_ = true;
            }
          }
        }
        break;

      case SDL_EVENT_MOUSE_MOTION: {
        SDL_ConvertEventToRenderCoordinates(renderer_, &ev);
        Vec2f m{ev.motion.x, ev.motion.y};
        for (int i = 0; i < static_cast<int>(buttons_.size()); ++i) {
          if (buttons_[i]->contains(m)) {
            if (cursor_ != i) {
              cursor_ = i;
              update_cursor_selection();
              dirty_ = true;
            }
            break;
          }
        }
        break;
      }

      case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        if (ev.button.button != SDL_BUTTON_LEFT)
          break;
        SDL_ConvertEventToRenderCoordinates(renderer_, &ev);
        Vec2f m{ev.button.x, ev.button.y};
        for (int i = 0; i < static_cast<int>(buttons_.size()); ++i) {
          buttons_[i]->handle_event(ev, m);
          if (buttons_[i]->take_click()) {
            cursor_ = i;
            update_cursor_selection();
            if (auto mode = activate_item(i)) {
              handle_resize(renderer_, settings_.auto_scale);
              return mode;
            }
            dirty_ = true;
            break;
          }
        }
        break;
      }
      }
    } while (running_ && SDL_PollEvent(&ev));
  }
  return nullptr;
}

std::unique_ptr<GameMode> Menu::activate_item(int index) {
  if (state_ == State::Main) {
    if (index == 0) {
      state_ = State::PresetSelect;
      cursor_ = 0;
      rebuild_buttons();
    } else if (index == 1) {
      SettingsEditor editor(renderer_, window_, settings_);
      editor.run();
      // SettingsEditor::save_settings reapplies scale + logical
      // presentation; rebuild our widgets against the new scale.
      rebuild_buttons();
    }
  } else if (state_ == State::PresetSelect) {
    return make_selected_mode();
  }
  return nullptr;
}

std::unique_ptr<GameMode> Menu::make_selected_mode() {
  auto fresh_modes = all_modes();
  if (cursor_ == 0) {
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
  SDL_SetRenderDrawColor(renderer_, 20, 20, 20, 255);
  SDL_RenderClear(renderer_);

  // Title — centered over the first button using the actual logical width.
  if (!buttons_.empty()) {
    if (const CachedText *t =
            get_text(renderer_, "CITRIS", L::kFontXXL, Color(200, 200, 200))) {
      float cx = L::kWindowW / 2.f;
      float y = buttons_.front()->bounds().y - 100.f * L::kScale;
      SDL_FRect dst = {cx - t->w / 2.f, y, t->w, t->h};
      SDL_RenderTexture(renderer_, t->tex, nullptr, &dst);
    }
  }

  for (auto &b : buttons_)
    b->draw(renderer_, 0.f);

  SDL_RenderPresent(renderer_);
}
