#include "game_manager.h"
#include "settings.h"

int main() {
  Settings settings;
  GameManager manager(GameMode::Solo, settings);
  manager.run();
  return 0;
}
