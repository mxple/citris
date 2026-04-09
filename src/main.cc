#include "game_manager.h"
#include "menu.h"
#include "render/renderer.h"
#include "settings.h"

#include <filesystem>

using L = RenderLayout;

int main(int, char *argv[]) {
  Settings settings;
  settings.base_dir =
      std::filesystem::path(argv[0]).parent_path();
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
