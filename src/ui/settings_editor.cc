#include "ui/settings_editor.h"

#include <imgui.h>
#include <SDL3_image/SDL_image.h>
#include <filesystem>
#include <algorithm>
#include <cstdio>

SettingsEditor::SettingsEditor(SDL_Renderer *renderer, SDL_Window *window,
                                Settings &settings)
    : renderer_(renderer), window_(window), settings_(settings) {
  sync_from_settings();
  discover_skins();
}

SettingsEditor::~SettingsEditor() {
  if (preview_tex_)
    SDL_DestroyTexture(preview_tex_);
}

void SettingsEditor::reload_skin_preview() {
  const std::string &path =
      (skin_index_ >= 0 && skin_index_ < (int)skin_paths_.size())
          ? skin_paths_[skin_index_]
          : std::string();
  if (path == preview_loaded_path_ && preview_tex_)
    return;
  if (preview_tex_) {
    SDL_DestroyTexture(preview_tex_);
    preview_tex_ = nullptr;
  }
  preview_loaded_path_ = path;
  if (path.empty())
    return;
  preview_tex_ = IMG_LoadTexture(renderer_, path.c_str());
  if (preview_tex_) {
    SDL_SetTextureScaleMode(preview_tex_, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(preview_tex_, SDL_BLENDMODE_BLEND);
  }
}

void SettingsEditor::sync_from_settings() {
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

void SettingsEditor::discover_skins() {
  skin_paths_.clear();
  try {
    for (auto &entry :
         std::filesystem::directory_iterator(settings_.base_dir / "assets")) {
      auto name = entry.path().filename().string();
      if (name.starts_with("skin") && name.ends_with(".png"))
        skin_paths_.push_back(entry.path().string());
    }
  } catch (...) {
  }
  std::sort(skin_paths_.begin(), skin_paths_.end());
  skin_index_ = 0;
  for (int i = 0; i < (int)skin_paths_.size(); ++i)
    if (skin_paths_[i] == settings_.skin_path)
      skin_index_ = i;
}

namespace {
struct Binding {
  const char *label;
  KeyCode *target;
};
}

void SettingsEditor::draw() {
  Binding bindings[] = {
      {"Move Left", &settings_.move_left},
      {"Move Right", &settings_.move_right},
      {"Rotate CW", &settings_.rotate_cw},
      {"Rotate CCW", &settings_.rotate_ccw},
      {"Rotate 180", &settings_.rotate_180},
      {"Hard Drop", &settings_.hard_drop},
      {"Soft Drop", &settings_.soft_drop},
      {"Hold", &settings_.hold},
      {"Undo", &settings_.undo},
  };
  constexpr int kBindingCount = sizeof(bindings) / sizeof(bindings[0]);

  // --- Keybind capture ---
  // If capturing, grab the next pressed non-modifier key from ImGui's input
  // queue and assign it, then stop capturing.
  if (capturing_ >= 0 && capturing_ < kBindingCount) {
    ImGuiIO &io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
      capturing_ = -1;
    } else {
      for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
        if (k == ImGuiKey_Escape)
          continue;
        if (ImGui::IsKeyPressed((ImGuiKey)k, false)) {
          // Map ImGuiKey back to SDL_Keycode via the key name.
          const char *name = ImGui::GetKeyName((ImGuiKey)k);
          if (name && name[0]) {
            SDL_Keycode kc = SDL_GetKeyFromName(name);
            if (kc != SDLK_UNKNOWN)
              *bindings[capturing_].target = kc;
          }
          capturing_ = -1;
          break;
        }
      }
    }
  }

  ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.3f, 1.f), "SETTINGS");
  ImGui::Separator();

  ImGui::BeginChild("settings_scroll", ImVec2(0, -40), false,
                     ImGuiWindowFlags_NoScrollbar);

  // Consistent two-column layout: label on the left at col 0, widget on the
  // right at kLabelCol, sized to kWidgetW. All widgets use "##label" so
  // ImGui's built-in right-side label is suppressed.
  constexpr float kLabelCol = 240.f;
  constexpr float kWidgetW = 220.f;

  auto row_begin = [&](const char *label) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(kLabelCol);
    ImGui::SetNextItemWidth(kWidgetW);
  };

  auto int_row = [&](const char *label, int *v) {
    row_begin(label);
    ImGui::PushID(label);
    ImGui::InputInt("##v", v);
    ImGui::PopID();
  };

  auto slider_row = [&](const char *label, int *v, int lo, int hi) {
    row_begin(label);
    ImGui::PushID(label);
    ImGui::SliderInt("##v", v, lo, hi);
    ImGui::PopID();
  };

  auto bool_row = [&](const char *label, bool *v) {
    row_begin(label);
    ImGui::PushID(label);
    ImGui::Checkbox("##v", v);
    ImGui::PopID();
  };

  ImGui::SeparatorText("Controls");
  for (int i = 0; i < kBindingCount; ++i) {
    row_begin(bindings[i].label);
    ImGui::PushID(i);
    const char *btn_label =
        capturing_ == i ? "[press a key]" : key_to_string(*bindings[i].target).c_str();
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s##kb", btn_label);
    if (ImGui::Button(buf, ImVec2(kWidgetW, 0)))
      capturing_ = i;
    ImGui::PopID();
  }

  ImGui::SeparatorText("Input Tuning");
  int_row("DAS (ms)", &das_ms_);
  int_row("ARR (ms)", &arr_ms_);
  int_row("Soft Drop Interval (ms)", &soft_drop_ms_);
  bool_row("DAS Preserve Charge", &settings_.das_preserve_charge);

  ImGui::SeparatorText("Rendering");
  bool_row("Auto Scale", &settings_.auto_scale);
  slider_row("Scale %", &scale_pct_, 50, 400);

  if (!skin_paths_.empty()) {
    std::vector<const char *> skin_items;
    for (auto &s : skin_paths_)
      skin_items.push_back(s.c_str());

    // Preview of the currently selected skin, rendered directly above the
    // Skin combo. The skin is a horizontal strip of kSkinTiles tiles
    // (pitch kSkinPitch, tile kSkinTile). Scale to fit kWidgetW.
    reload_skin_preview();
    if (preview_tex_) {
      const float tex_w = (float)preview_tex_->w;
      const float tex_h = (float)preview_tex_->h;
      if (tex_w > 0 && tex_h > 0) {
        const float disp_w = kWidgetW;
        const float disp_h = tex_h * (disp_w / tex_w);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Skin Preview");
        ImGui::SameLine(kLabelCol);
        ImGui::Image((ImTextureID)(intptr_t)preview_tex_,
                     ImVec2(disp_w, disp_h));
      }
    }

    row_begin("Skin");
    if (ImGui::Combo("##skin", &skin_index_, skin_items.data(),
                      (int)skin_items.size())) {
      settings_.skin_path = skin_paths_[skin_index_];
    }
  }
  bool_row("Antialiasing", &settings_.antialiasing);
  bool_row("Colored Ghost", &settings_.colored_ghost);
  slider_row("Ghost Opacity %", &ghost_opacity_pct_, 0, 100);
  slider_row("Grid Opacity %", &grid_opacity_pct_, 0, 100);

  ImGui::SeparatorText("Freeplay Settings");
  int_row("Gravity Interval (ms)", &gravity_ms_);
  int_row("Lock Delay (ms)", &lock_delay_ms_);
  int_row("Garbage Delay (ms)", &garbage_delay_ms_);
  int_row("Hard Drop Delay (ms)", &hard_drop_delay_ms_);
  int_row("Max Lock Resets", &settings_.game.max_lock_resets);
  bool_row("Infinite Hold", &settings_.game.infinite_hold);

  ImGui::EndChild();

  ImGui::Separator();
  if (ImGui::Button("Save & Back", ImVec2(140, 0))) {
    save_settings();
    close_requested_ = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(100, 0))) {
    sync_from_settings();
    close_requested_ = true;
  }
}

void SettingsEditor::save_settings() {
  settings_.das = std::chrono::milliseconds(std::max(0, das_ms_));
  settings_.arr = std::chrono::milliseconds(std::max(0, arr_ms_));
  settings_.soft_drop_interval =
      std::chrono::milliseconds(std::max(0, soft_drop_ms_));
  settings_.game.gravity_interval = std::chrono::milliseconds(gravity_ms_);
  settings_.game.lock_delay = std::chrono::milliseconds(lock_delay_ms_);
  settings_.game.garbage_delay =
      std::chrono::milliseconds(std::max(0, garbage_delay_ms_));
  settings_.game.hard_drop_delay =
      std::chrono::milliseconds(std::max(0, hard_drop_delay_ms_));
  settings_.ghost_opacity =
      static_cast<uint8_t>(std::clamp(ghost_opacity_pct_, 0, 100) * 255 / 100);
  settings_.grid_opacity =
      static_cast<uint8_t>(std::clamp(grid_opacity_pct_, 0, 100) * 255 / 100);
  settings_.scale_factor =
      std::clamp(static_cast<float>(scale_pct_) / 100.f, 0.5f, 4.f);

  settings_.save("settings.ini");
}
