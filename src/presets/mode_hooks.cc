#include "mode_hooks.h"
#include "game_mode.h"

std::unique_ptr<GameMode> ModeHooks::opponent_mode(const Settings &) const {
  return nullptr;
}
