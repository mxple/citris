#include "game_manager.h"
#include "settings.h"
#include "ui/menu.h"

#include <SDL3/SDL.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include "FreeMono_otf.h"

static constexpr int kInitialWindowW = 1200;
static constexpr int kInitialWindowH = 900;

static void setup_imgui_style() {
  ImGuiStyle &style = ImGui::GetStyle();
  ImGui::StyleColorsDark();

  style.WindowRounding = 6.f;
  style.ChildRounding = 6.f;
  style.FrameRounding = 4.f;
  style.PopupRounding = 6.f;
  style.ScrollbarRounding = 6.f;
  style.GrabRounding = 4.f;
  style.TabRounding = 4.f;
  style.WindowBorderSize = 0.f;
  style.FrameBorderSize = 0.f;
  style.PopupBorderSize = 0.f;
  style.WindowPadding = ImVec2(12, 12);
  style.FramePadding = ImVec2(8, 5);
  style.ItemSpacing = ImVec2(8, 6);
  style.ItemInnerSpacing = ImVec2(6, 4);
  style.IndentSpacing = 20.f;
  style.ScrollbarSize = 12.f;
  style.GrabMinSize = 10.f;

  auto rgba = [](int r, int g, int b, float a = 1.f) {
    return ImVec4(r / 255.f, g / 255.f, b / 255.f, a);
  };

  const ImVec4 bg_deep = rgba(20, 16, 12);
  const ImVec4 bg = rgba(28, 22, 17);
  const ImVec4 bg_light = rgba(40, 32, 24);
  const ImVec4 bg_hi = rgba(55, 43, 30);
  const ImVec4 border = rgba(70, 55, 38);
  const ImVec4 text = rgba(245, 232, 210);
  const ImVec4 text_dim = rgba(160, 146, 120);
  const ImVec4 text_dis = rgba(95, 85, 70);
  const ImVec4 accent = rgba(255, 154, 40);    // amber
  const ImVec4 accent_hi = rgba(255, 186, 80); // lighter amber
  const ImVec4 accent_lo = rgba(200, 110, 20); // darker amber
  const ImVec4 accent_soft = rgba(255, 154, 40, 0.35f);

  ImVec4 *c = style.Colors;
  c[ImGuiCol_Text] = text;
  c[ImGuiCol_TextDisabled] = text_dis;
  c[ImGuiCol_WindowBg] = rgba(26, 20, 15, 0.98f);
  c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_PopupBg] = rgba(22, 17, 12, 0.98f);
  c[ImGuiCol_Border] = border;
  c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_FrameBg] = bg_light;
  c[ImGuiCol_FrameBgHovered] = bg_hi;
  c[ImGuiCol_FrameBgActive] = rgba(75, 55, 30);
  c[ImGuiCol_TitleBg] = bg_deep;
  c[ImGuiCol_TitleBgActive] = rgba(50, 35, 18);
  c[ImGuiCol_TitleBgCollapsed] = bg_deep;
  c[ImGuiCol_MenuBarBg] = bg;
  c[ImGuiCol_ScrollbarBg] = rgba(18, 14, 10);
  c[ImGuiCol_ScrollbarGrab] = bg_hi;
  c[ImGuiCol_ScrollbarGrabHovered] = accent_lo;
  c[ImGuiCol_ScrollbarGrabActive] = accent;
  c[ImGuiCol_CheckMark] = accent_hi;
  c[ImGuiCol_SliderGrab] = accent;
  c[ImGuiCol_SliderGrabActive] = accent_hi;
  c[ImGuiCol_Button] = rgba(50, 38, 22);
  c[ImGuiCol_ButtonHovered] = accent_lo;
  c[ImGuiCol_ButtonActive] = accent;
  c[ImGuiCol_Header] = rgba(60, 44, 22);
  c[ImGuiCol_HeaderHovered] = accent_lo;
  c[ImGuiCol_HeaderActive] = accent;
  c[ImGuiCol_Separator] = border;
  c[ImGuiCol_SeparatorHovered] = accent_lo;
  c[ImGuiCol_SeparatorActive] = accent;
  c[ImGuiCol_ResizeGrip] = rgba(70, 50, 25);
  c[ImGuiCol_ResizeGripHovered] = accent_lo;
  c[ImGuiCol_ResizeGripActive] = accent;
  c[ImGuiCol_Tab] = rgba(40, 30, 18);
  c[ImGuiCol_TabHovered] = accent_lo;
  c[ImGuiCol_TabActive] = rgba(80, 55, 25);
  c[ImGuiCol_TabUnfocused] = rgba(30, 22, 14);
  c[ImGuiCol_TabUnfocusedActive] = rgba(55, 40, 22);
  c[ImGuiCol_PlotLines] = text_dim;
  c[ImGuiCol_PlotLinesHovered] = accent_hi;
  c[ImGuiCol_PlotHistogram] = accent;
  c[ImGuiCol_PlotHistogramHovered] = accent_hi;
  c[ImGuiCol_TextSelectedBg] = accent_soft;
  c[ImGuiCol_DragDropTarget] = accent_hi;
  c[ImGuiCol_NavHighlight] = accent;
  c[ImGuiCol_NavWindowingHighlight] = ImVec4(1, 1, 1, 0.7f);
  c[ImGuiCol_NavWindowingDimBg] = ImVec4(0.1f, 0.08f, 0.05f, 0.5f);
  c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.05f, 0.03f, 0.02f, 0.6f);
}

int main(int argc, char *argv[]) {
  Settings settings(argv[0]);

  SDL_Init(SDL_INIT_VIDEO);

  SDL_Window *window = SDL_CreateWindow(
      "Citris", kInitialWindowW, kInitialWindowH,
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  SDL_Renderer *renderer = SDL_CreateRenderer(window, nullptr);
  SDL_SetRenderVSync(renderer, 1);

  // Query DPI scale from the ratio of pixel size to logical window size.
  // With HIGH_PIXEL_DENSITY, on a 2x HiDPI display this will be 2.0.
  float dpi_scale = 1.f;
  {
    int win_w, win_h, pix_w, pix_h;
    SDL_GetWindowSize(window, &win_w, &win_h);
    SDL_GetWindowSizeInPixels(window, &pix_w, &pix_h);
    if (win_w > 0)
      dpi_scale = static_cast<float>(pix_w) / static_cast<float>(win_w);
  }
  SDL_SetRenderScale(renderer, dpi_scale, dpi_scale);

  // ImGui setup
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr; // don't write imgui.ini

  // Bake the font at physical-pixel size, then downscale via FontGlobalScale
  // so text appears at logical size but rasterized at native resolution.
  ImFontConfig font_cfg;
  font_cfg.FontDataOwnedByAtlas = false;
  float base_font = 18.f * settings.scale_factor;
  io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char *>(FreeMono_otf),
                                 static_cast<int>(FreeMono_otf_len),
                                 base_font * dpi_scale, &font_cfg);
  io.FontGlobalScale = 1.f / dpi_scale;

  setup_imgui_style();
  ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer3_Init(renderer);

  while (true) {
    Menu menu(renderer, window, settings);
    auto mode = menu.run();
    if (!mode)
      break;
    GameManager gm(renderer, window, settings, std::move(mode));
    if (!gm.run())
      break;
  }

  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();

  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}
