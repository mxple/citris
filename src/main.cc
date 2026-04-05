#include "game_manager.h"
#include "menu.h"
#include "render/renderer.h"
#include "settings.h"

using L = RenderLayout;

int main() {
  Settings settings;
  settings.load("settings.ini");

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
