#pragma once

#include "blitz.h"
#include "cheese.h"
#include "freeplay.h"
#include "sprint.h"
#include "tspin_practice.h"
#include "user_mode.h"
#include <filesystem>
#include <memory>
#include <vector>

inline std::vector<std::unique_ptr<GameMode>> all_modes() {
  std::vector<std::unique_ptr<GameMode>> modes;
  modes.push_back(std::make_unique<FreeplayMode>());
  modes.push_back(std::make_unique<SprintMode>());
  modes.push_back(std::make_unique<BlitzMode>());
  modes.push_back(std::make_unique<CheeseMode>());
  modes.push_back(
      std::make_unique<TSpinPracticeMode>(5, TSpinVariant::TSD));
  modes.push_back(
      std::make_unique<TSpinPracticeMode>(5, TSpinVariant::TSDQuad));
  return modes;
}

// Load user-defined training modes from assets/usermodes/.
inline std::vector<std::unique_ptr<GameMode>> all_user_modes() {
  std::vector<std::unique_ptr<GameMode>> modes;
  const std::string dir = "assets/usermodes";
  if (!std::filesystem::is_directory(dir))
    return modes;
  for (auto &entry : std::filesystem::directory_iterator(dir)) {
    if (entry.path().extension() == ".umode") {
      auto cfg = parse_user_mode(entry.path().string());
      modes.push_back(std::make_unique<UserMode>(std::move(cfg)));
    }
  }
  return modes;
}

