#include "ui/menu.h"
#include "presets/presets.h"
#include "ui/settings_editor.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

Menu::Menu(SDL_Renderer *renderer, SDL_Window *window, Settings &settings)
    : renderer_(renderer), window_(window), settings_(settings) {
  modes_ = all_modes();
}

std::unique_ptr<GameMode> Menu::make_selected_mode(int index) {
  auto fresh_modes = all_modes();
  if (index < 0 || index >= static_cast<int>(fresh_modes.size()))
    return nullptr;
  if (index == 0) {
    if (auto *fp = dynamic_cast<FreeplayMode *>(fresh_modes[0].get())) {
      fp->set_gravity_interval(settings_.game.gravity_interval);
      fp->set_lock_delay(settings_.game.lock_delay);
      fp->set_garbage_delay(settings_.game.garbage_delay);
      fp->set_hard_drop_delay(settings_.game.hard_drop_delay);
      fp->set_max_lock_resets(settings_.game.max_lock_resets);
      fp->set_infinite_hold(settings_.game.infinite_hold);
    }
  }
  return std::move(fresh_modes[index]);
}

std::unique_ptr<GameMode> Menu::run() {
  SettingsEditor settings_editor(renderer_, window_, settings_);

  while (true) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      ImGui_ImplSDL3_ProcessEvent(&ev);
      if (ev.type == SDL_EVENT_QUIT)
        return nullptr;
    }

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    int win_w = 0, win_h = 0;
    SDL_GetWindowSize(window_, &win_w, &win_h);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)win_w, (float)win_h));
    ImGui::Begin("Menu", nullptr,
                  ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                      ImGuiWindowFlags_NoBringToFrontOnFocus |
                      ImGuiWindowFlags_NoScrollbar |
                      ImGuiWindowFlags_NoScrollWithMouse);

    {
      const char *title = "CITRIS";
      ImGui::SetWindowFontScale(2.5f);
      ImVec2 ts = ImGui::CalcTextSize(title);
      ImGui::SetCursorPosX((win_w - ts.x) * 0.5f);
      ImGui::SetCursorPosY(win_h * 0.15f);
      ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.3f, 1.f), "%s", title);
      ImGui::SetWindowFontScale(1.0f);
    }

    ImGui::SetCursorPosY(win_h * 0.35f);

    std::unique_ptr<GameMode> selected;
    bool back_to_main = false;

    auto button = [&](const char *label) -> bool {
      float bw = 240.f;
      float bh = 48.f;
      ImGui::SetCursorPosX((win_w - bw) * 0.5f);
      return ImGui::Button(label, ImVec2(bw, bh));
    };

    if (screen_ == Screen::Main) {
      if (button("Play"))
        screen_ = Screen::PresetSelect;
      if (button("Settings"))
        screen_ = Screen::Settings;
      if (button("Quit")) {
        ImGui::End();
        ImGui::Render();
        return nullptr;
      }
    } else if (screen_ == Screen::PresetSelect) {
      for (int i = 0; i < (int)modes_.size(); ++i) {
        if (button(modes_[i]->title().c_str())) {
          selected = make_selected_mode(i);
        }
      }
      ImGui::Spacing();
      if (button("Back"))
        back_to_main = true;
    } else if (screen_ == Screen::Settings) {
      ImGui::SetCursorPosY(win_h * 0.15f);
      settings_editor.draw();
      if (settings_editor.should_close()) {
        settings_editor.reset_close();
        back_to_main = true;
      }
    }

    ImGui::End();

    SDL_SetRenderDrawColor(renderer_, 20, 20, 20, 255);
    SDL_RenderClear(renderer_);
    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
    SDL_RenderPresent(renderer_);

    if (back_to_main) {
      screen_ = Screen::Main;
    }
    if (selected)
      return selected;
  }
}
