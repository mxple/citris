#include "ui/menu.h"
#include "presets/presets.h"
#include "presets/versus.h"
#include "ui/settings_editor.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

Menu::Menu(SDL_Renderer *renderer, SDL_Window *window, Settings &settings)
    : renderer_(renderer), window_(window), settings_(settings) {
  modes_ = play_modes(settings);
  training_modes_ = training_modes(settings);
}

std::unique_ptr<GameMode> Menu::make_selected_play_mode(int index) {
  auto fresh_modes = play_modes(settings_);
  if (index < 0 || index >= static_cast<int>(fresh_modes.size()))
    return nullptr;
  return std::move(fresh_modes[index]);
}

std::unique_ptr<GameMode> Menu::make_selected_training_mode(int index) {
  auto fresh_modes = training_modes(settings_);
  if (index < 0 || index >= static_cast<int>(fresh_modes.size()))
    return nullptr;
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

    if (screen_ != Screen::Settings) {
      const char *title = "CITRIS";
      ImGui::SetWindowFontScale(2.5f);
      ImVec2 ts = ImGui::CalcTextSize(title);
      ImGui::SetCursorPosX((win_w - ts.x) * 0.5f);
      ImGui::SetCursorPosY(win_h * 0.15f);
      ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.3f, 1.f), "%s", title);
      ImGui::SetWindowFontScale(1.0f);
    }

    if (screen_ != Screen::Settings)
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
      if (button("Training"))
        screen_ = Screen::TrainModeSelect;
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
          // Versus modes need opponent configuration before starting.
          if (dynamic_cast<VersusMode *>(modes_[i].get())) {
            screen_ = Screen::VersusSetup;
          } else {
            selected = make_selected_play_mode(i);
          }
        }
      }
      ImGui::Spacing();
      if (button("Back"))
        back_to_main = true;
    } else if (screen_ == Screen::VersusSetup) {
      // Two-column P1 / P2 configuration. Each column has a Kind dropdown
      // and conditional fields (path for ExternalBot; beam/depth/think for
      // CitrisAi).
      const char *kinds[] = {"Human", "Citris AI", "External TBP bot"};

      const float col_w = 280.f;
      const float gap = 40.f;
      const float total_w = col_w * 2.f + gap;
      const float left_x = (win_w - total_w) * 0.5f;
      const float right_x = left_x + col_w + gap;

      auto col_label = [&](float x, const char *text,
                           const ImVec4 &col = ImVec4(0.9f, 0.6f, 0.1f, 1.f)) {
        ImGui::SetCursorPosX(x);
        ImGui::TextColored(col, "%s", text);
      };
      auto draw_player_config = [&](float x, const char *header, int *kind,
                                    char *path_buf, size_t path_sz,
                                    int *beam, int *depth, int *think) {
        col_label(x, header);

        ImGui::SetCursorPosX(x);
        ImGui::SetNextItemWidth(col_w);
        ImGui::PushID(kind); // disambiguate widgets between the two columns
        ImGui::Combo("##kind", kind, kinds, 3);

        if (*kind == 2) { // ExternalBot
          ImGui::SetCursorPosX(x);
          ImGui::TextDisabled("Bot path");
          ImGui::SetCursorPosX(x);
          ImGui::SetNextItemWidth(col_w);
          ImGui::InputText("##path", path_buf, path_sz);
          ImGui::SetCursorPosX(x);
        } else if (*kind == 1) { // CitrisAi
          ImGui::SetCursorPosX(x);
          ImGui::TextDisabled("Beam width");
          ImGui::SetCursorPosX(x);
          ImGui::SetNextItemWidth(col_w);
          ImGui::InputInt("##beam", beam);
          if (*beam < 1) *beam = 1;
          if (*beam > 5000) *beam = 5000;

          ImGui::SetCursorPosX(x);
          ImGui::TextDisabled("Max depth");
          ImGui::SetCursorPosX(x);
          ImGui::SetNextItemWidth(col_w);
          ImGui::InputInt("##depth", depth);
          if (*depth < 1) *depth = 1;
          if (*depth > 30) *depth = 30;
        }

        // Think time applies to any bot (both CitrisAi and ExternalBot).
        if (*kind != 0) {
          ImGui::SetCursorPosX(x);
          ImGui::TextDisabled("Think time (ms) — 0 = no cap");
          ImGui::SetCursorPosX(x);
          ImGui::SetNextItemWidth(col_w);
          ImGui::InputInt("##think", think);
          if (*think < 0) *think = 0;
        }

        ImGui::PopID();
      };

      // Remember start Y so both columns start at the same vertical line.
      float start_y = ImGui::GetCursorPosY();

      ImGui::SetCursorPosY(start_y);
      ImGui::BeginGroup();
      draw_player_config(left_x, "PLAYER 1", &p1_kind_, p1_path_,
                         sizeof(p1_path_), &p1_beam_, &p1_depth_,
                         &p1_think_ms_);
      ImGui::EndGroup();
      float left_end_y = ImGui::GetCursorPosY();

      ImGui::SetCursorPosY(start_y);
      ImGui::BeginGroup();
      draw_player_config(right_x, "PLAYER 2", &p2_kind_, p2_path_,
                         sizeof(p2_path_), &p2_beam_, &p2_depth_,
                         &p2_think_ms_);
      ImGui::EndGroup();
      float right_end_y = ImGui::GetCursorPosY();

      ImGui::SetCursorPosY(std::max(left_end_y, right_end_y));
      ImGui::Spacing();
      ImGui::Spacing();

      auto needs_path = [](int kind, const char *buf) {
        return kind == 2 && buf[0] == '\0';
      };
      bool can_start = !needs_path(p1_kind_, p1_path_) &&
                       !needs_path(p2_kind_, p2_path_);

      if (!can_start) ImGui::BeginDisabled();
      if (button("Start")) {
        auto vm = std::make_unique<VersusMode>(settings_);
        auto to_cfg = [](int kind, const char *path, int beam, int depth,
                         int think) {
          VersusMode::PlayerConfig c;
          c.kind = static_cast<VersusMode::Kind>(kind);
          c.external_path = std::string(path);
          c.beam_width = beam;
          c.max_depth = depth;
          c.think_time_ms = think;
          return c;
        };
        vm->set_player(0, to_cfg(p1_kind_, p1_path_, p1_beam_, p1_depth_,
                                  p1_think_ms_));
        vm->set_player(1, to_cfg(p2_kind_, p2_path_, p2_beam_, p2_depth_,
                                  p2_think_ms_));
        selected = std::move(vm);
      }
      if (!can_start) ImGui::EndDisabled();
      if (button("Back")) {
        screen_ = Screen::PresetSelect;
      }
    } else if (screen_ == Screen::TrainModeSelect) {
      for (int i = 0; i < (int)training_modes_.size(); ++i) {
        if (button(training_modes_[i]->title().c_str())) {
          selected = make_selected_training_mode(i);
        }
      }
      ImGui::Spacing();
      if (button("Back"))
        back_to_main = true;
    } else if (screen_ == Screen::Settings) {
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

#ifdef __EMSCRIPTEN__
    emscripten_sleep(0);
#endif

    if (back_to_main) {
      screen_ = Screen::Main;
    }
    if (selected)
      return selected;
  }
}
