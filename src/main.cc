#include "game_manager.h"
#include "menu.h"
#include "render/renderer.h"
#include "settings.h"

#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#endif

using L = RenderLayout;

int main(int argc, char *argv[]) {
  Settings settings;
#ifdef _WIN32
  // WIN32 subsystem + SFML::Main may pass an unreliable argv[0];
  // use the Windows API to get the executable path instead.
  wchar_t exe_buf[MAX_PATH];
  GetModuleFileNameW(nullptr, exe_buf, MAX_PATH);
  settings.base_dir =
      std::filesystem::path(exe_buf).parent_path();
#else
  settings.base_dir =
      std::filesystem::path(argv[0]).parent_path();
#endif
  if (settings.base_dir.empty())
    settings.base_dir = ".";
  settings.load(settings.resolve("settings.ini"));

  auto window =
      sf::RenderWindow(sf::VideoMode({L::kWindowW, L::kWindowH}), "Citris");

  window.setKeyRepeatEnabled(false);

  while (true) {
    Menu menu(window, settings);
    auto mode = menu.run();
    if (!mode)
      break;
    GameManager gm(window, settings, std::move(mode));
    gm.run();
  }

  return 0;
}
