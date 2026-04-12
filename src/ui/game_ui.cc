#include "ui/game_ui.h"

#include <algorithm>
#include <cstdio>
#include <imgui.h>

static void fmt_time(char *buf, size_t n, float secs) {
  int total_ms = static_cast<int>(secs * 1000);
  int mins = total_ms / 60000;
  int s = (total_ms % 60000) / 1000;
  int hundredths = (total_ms % 1000) / 10;
  std::snprintf(buf, n, "%d:%02d.%02d", mins, s, hundredths);
}

static void fmt_rate(char *buf, size_t n, int count, float secs) {
  if (secs < 0.001f) {
    std::snprintf(buf, n, "0.00");
    return;
  }
  std::snprintf(buf, n, "%.2f", static_cast<float>(count) / secs);
}

static void stat_row(const char *label, const char *value) {
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.f), "%s", label);
  ImGui::TableSetColumnIndex(1);
  ImGui::Text("%s", value);
}

// --- Cell-aligned layout ---------------------------------------------------

namespace {

using L = RenderLayout;

struct CellLayout {
  float cell_px;
  float origin_x, origin_y;

  // Return the top-left pixel position of the top-left corner of an
  // h-cell-tall element whose bottom row sits at y_up.
  ImVec2 pos(int col, int y_up, int h_cells = 1) const {
    int row_from_top = (L::kSceneRows - 1) - y_up - (h_cells - 1);
    return ImVec2(origin_x + col * cell_px, origin_y + row_from_top * cell_px);
  }
  ImVec2 size(int w_cells, int h_cells) const {
    return ImVec2(w_cells * cell_px, h_cells * cell_px);
  }
};

CellLayout compute_cell_layout(SDL_Window *window, const Settings &settings) {
  int win_w = 0, win_h = 0;
  SDL_GetWindowSize(window, &win_w, &win_h);
  float W = static_cast<float>(win_w);
  float H = static_cast<float>(win_h);
  CellLayout layout{};
  if (settings.auto_scale)
    layout.cell_px = std::min(W / float(L::kSceneCols), H / float(L::kSceneRows));
  else
    layout.cell_px = static_cast<float>(L::kTileSize) * settings.scale_factor;
  float layout_w = layout.cell_px * L::kSceneCols;
  float layout_h = layout.cell_px * L::kSceneRows;
  layout.origin_x = (W - layout_w) * 0.5f;
  layout.origin_y = (H - layout_h) * 0.5f;
  return layout;
}

} // namespace

void draw_game_ui(Renderer &renderer, SDL_Window *window, const ViewModel &vm,
                   const Settings &settings) {
  CellLayout layout = compute_cell_layout(window, settings);
  if (layout.cell_px <= 0.f)
    return;

  const ImU32 kLabelColor = IM_COL32(178, 178, 76, 255);

  // 1) Render the full 20x24 scene into an internal fixed-size texture and
  //    blit it behind everything via the background draw list, scaled to the
  //    layout. This guarantees no ImGui window can ever clip or z-order over
  //    the game board.
  float scene_w = layout.cell_px * L::kSceneCols;
  float scene_h = layout.cell_px * L::kSceneRows;
  SDL_Texture *scene = renderer.draw_scene_to_texture(vm.state);
  if (scene) {
    ImVec2 p_min(layout.origin_x, layout.origin_y);
    ImVec2 p_max(p_min.x + scene_w, p_min.y + scene_h);
    ImGui::GetBackgroundDrawList()->AddImage((ImTextureID)(intptr_t)scene,
                                             p_min, p_max);
  }

  // 2) A single transparent full-screen overlay window hosts all text and
  //    labels. Use absolute positioning via SetCursorScreenPos so nothing
  //    fights with per-panel window padding, focus, or scroll state.
  int win_w = 0, win_h = 0;
  SDL_GetWindowSize(window, &win_w, &win_h);
  constexpr ImGuiWindowFlags kOverlayFlags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoInputs |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
      ImGuiWindowFlags_NoScrollWithMouse;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImVec2(float(win_w), float(win_h)));
  if (ImGui::Begin("GameOverlay", nullptr, kOverlayFlags)) {
    const float base_font = ImGui::GetFontSize();
    const float label_scale =
        std::clamp((layout.cell_px * 0.9f) / base_font, 0.5f, 8.f);
    const float stats_scale =
        std::clamp((layout.cell_px * 0.6f) / base_font, 0.5f, 4.f);

    auto draw_centered_label = [&](const char *text, int col, int y_up,
                                   int w_cells, int h_cells) {
      ImGui::SetWindowFontScale(label_scale);
      ImVec2 p = layout.pos(col, y_up, h_cells);
      ImVec2 box = layout.size(w_cells, h_cells);
      ImVec2 ts = ImGui::CalcTextSize(text);
      ImGui::SetCursorScreenPos(
          ImVec2(p.x + (box.x - ts.x) * 0.5f, p.y + (box.y - ts.y) * 0.5f));
      ImGui::PushStyleColor(ImGuiCol_Text, kLabelColor);
      ImGui::TextUnformatted(text);
      ImGui::PopStyleColor();
      ImGui::SetWindowFontScale(1.f);
    };

    // HOLD label — 4x3 area at (kHoldColX, y_up 20..22).
    draw_centered_label("HOLD", L::kHoldColX, 20, L::kSideCols,
                        L::kPadRowsNorth);
    // NEXT label — 4x3 area at (kNextColX, y_up 20..22).
    draw_centered_label("NEXT", L::kNextColX, 20, L::kSideCols,
                        L::kPadRowsNorth);

    // Stats panel — 4x17 area at the bottom of the left column.
    {
      ImGui::SetWindowFontScale(stats_scale);
      constexpr int kStatsRows = 14;
      ImVec2 stats_pos =
          layout.pos(L::kHoldColX, L::kPadRowsSouth + 2, kStatsRows);
      ImVec2 stats_size = layout.size(L::kSideCols, kStatsRows);
      ImGui::SetCursorScreenPos(stats_pos);
      ImGui::BeginGroup();

      const auto &s = vm.stats;
      char time_buf[16], pps_buf[16], apm_buf[16], kps_buf[16];
      fmt_time(time_buf, sizeof(time_buf), s.elapsed_s);
      fmt_rate(pps_buf, sizeof(pps_buf), s.pieces, s.elapsed_s);
      fmt_rate(kps_buf, sizeof(kps_buf), s.inputs, s.elapsed_s);
      // APM = attack per minute
      if (s.elapsed_s < 0.001f)
        std::snprintf(apm_buf, sizeof(apm_buf), "0.00");
      else
        std::snprintf(apm_buf, sizeof(apm_buf), "%.2f",
                      static_cast<float>(s.attack) / s.elapsed_s * 60.f);

      if (ImGui::BeginTable("stats", 2, ImGuiTableFlags_SizingStretchProp,
                            stats_size)) {
        // Time & rates
        stat_row("TIME", time_buf);
        stat_row("PPS", pps_buf);
        stat_row("APM", apm_buf);
        stat_row("KPS", kps_buf);
        // Counters
        stat_row("LNS", std::to_string(s.lines).c_str());
        stat_row("ATK", std::to_string(s.attack).c_str());
        stat_row("NUM", std::to_string(s.pieces).c_str());
        stat_row("TSN", std::to_string(s.tspins).c_str());
        stat_row("PC", std::to_string(s.pcs).c_str());
        // Attack state
        stat_row("CMB", std::to_string(s.combo).c_str());
        stat_row("B2B", std::to_string(s.b2b).c_str());
        ImGui::EndTable();
      }

      ImGui::EndGroup();
      float stats_bottom = ImGui::GetItemRectMax().y;

      if (vm.state.last_clear.lines > 0 ||
          vm.state.last_clear.spin != SpinKind::None) {
        std::string label;
        switch (vm.state.last_clear.spin) {
        case SpinKind::TSpin:
          label = "tspin ";
          break;
        case SpinKind::Mini:
          label = "tspin mini ";
          break;
        case SpinKind::AllSpin:
          label = "allspin ";
          break;
        case SpinKind::None:
          break;
        }
        static const char *kLineNames[] = {"", "single", "double", "triple",
                                           "quad"};
        int idx = std::clamp(vm.state.last_clear.lines, 0, 4);
        label += kLineNames[idx];

        float action_y = stats_bottom + layout.cell_px * 0.3f;
        ImGui::SetCursorScreenPos(ImVec2(stats_pos.x, action_y));
        ImGui::TextColored(ImVec4(1.f, 1.f, 0.4f, 1.f), "%s", label.c_str());
        if (vm.state.last_clear.perfect_clear) {
          ImGui::SetCursorScreenPos(
              ImVec2(stats_pos.x, ImGui::GetCursorScreenPos().y));
          ImGui::TextColored(ImVec4(1.f, 0.8f, 0.2f, 1.f), "perfect clear");
        }
      }
      ImGui::SetWindowFontScale(1.f);
    }

    // HUD overlay
    if (vm.hud) {
      const auto &hud = *vm.hud;
      ImVec2 play_pos =
          layout.pos(L::kBoardColX, L::kPadRowsSouth + L::kPlayRows - 1);
      ImVec2 play_size = layout.size(L::kBoardCols, L::kPlayRows);

      auto draw_play_centered = [&](const std::string &text, Color color,
                                     float y) {
        if (text.empty())
          return;
        ImVec2 ts = ImGui::CalcTextSize(text.c_str());
        ImGui::SetCursorScreenPos(
            ImVec2(play_pos.x + (play_size.x - ts.x) * 0.5f, y));
        ImGui::TextColored(
            ImVec4(color.r / 255.f, color.g / 255.f, color.b / 255.f, 1.f),
            "%s", text.c_str());
      };

      // Game-over overlay — centered in playfield.
      if (vm.state.game_over) {
        ImGui::SetWindowFontScale(label_scale);
        float cursor_y = play_pos.y + play_size.y * 0.08f;
        draw_play_centered(hud.game_over_label, hud.game_over_label_color,
                           cursor_y);
        if (!hud.game_over_label.empty())
          cursor_y += ImGui::CalcTextSize(hud.game_over_label.c_str()).y +
                      layout.cell_px * 0.2f;
        draw_play_centered(hud.game_over_detail, hud.game_over_detail_color,
                           cursor_y);
        ImGui::SetWindowFontScale(1.f);
      }

      // Center text (e.g. lines remaining) — below the board.
      if (!hud.center_text.empty()) {
        ImGui::SetWindowFontScale(label_scale);
        ImVec2 below_pos = layout.pos(L::kBoardColX, 0, 1);
        ImVec2 below_size = layout.size(L::kBoardCols, 1);
        ImVec2 ts = ImGui::CalcTextSize(hud.center_text.c_str());
        ImGui::SetCursorScreenPos(
            ImVec2(below_pos.x + (below_size.x - ts.x) * 0.5f,
                   below_pos.y + (below_size.y - ts.y) * 0.5f));
        ImGui::TextColored(
            ImVec4(hud.center_color.r / 255.f, hud.center_color.g / 255.f,
                   hud.center_color.b / 255.f, 1.f),
            "%s", hud.center_text.c_str());
        ImGui::SetWindowFontScale(1.f);
      }
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
}
