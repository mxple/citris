#include "game_manager.h"
#include "settings.h"

int main() {
  Settings settings;
  settings.load("settings.ini");
  GameManager manager(GameMode::Solo, settings);
  manager.run();
  return 0;
}
