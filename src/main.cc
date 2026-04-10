#include "game_manager.h"
#include "ui/menu.h"
#include "render/font.h"
#include "render/renderer.h"
#include "settings.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#endif

using L = RenderLayout;

int main(int argc, char *argv[]) {
  Settings settings;
#ifdef _WIN32
  wchar_t exe_buf[MAX_PATH];
  GetModuleFileNameW(nullptr, exe_buf, MAX_PATH);
  settings.base_dir = std::filesystem::path(exe_buf).parent_path();
#else
  settings.base_dir = std::filesystem::path(argv[0]).parent_path();
#endif
  if (settings.base_dir.empty())
    settings.base_dir = ".";
  settings.load(settings.resolve("settings.ini"));

  SDL_Init(SDL_INIT_VIDEO);
  TTF_Init();

  RenderLayout::init(settings.scale_factor);

  SDL_Window *window = SDL_CreateWindow(
      "Citris", static_cast<int>(L::kWindowW), static_cast<int>(L::kWindowH),
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  SDL_Renderer *renderer = SDL_CreateRenderer(window, nullptr);
  SDL_SetRenderVSync(renderer, 1);

  handle_resize(renderer, settings.auto_scale);

  while (true) {
    Menu menu(renderer, window, settings);
    auto mode = menu.run();
    if (!mode)
      break;
    GameManager gm(renderer, window, settings, std::move(mode));
    if (gm.run()) break;
  }

  shutdown_fonts();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  TTF_Quit();
  SDL_Quit();

  return 0;
}
