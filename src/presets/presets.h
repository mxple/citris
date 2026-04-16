#pragma once

#include "blitz.h"
#include "cheese.h"
#include "freeplay.h"
#include "opener.h"
#include "sprint.h"
#include "user_mode.h"
#include "ai/opener_db.h"
#include <filesystem>
#include <memory>
#include <vector>

inline std::vector<std::unique_ptr<GameMode>> all_modes() {
  std::vector<std::unique_ptr<GameMode>> modes;
  modes.push_back(std::make_unique<FreeplayMode>());
  modes.push_back(std::make_unique<SprintMode>());
  modes.push_back(std::make_unique<BlitzMode>());
  modes.push_back(std::make_unique<CheeseMode>());
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

// Return available openers for the opener selection sub-screen.
inline std::vector<Opener> all_openers() {
  auto openers = builtin_openers();
  auto from_dir = load_openers_from_dir("assets/openers");
  for (auto &op : from_dir)
    openers.push_back(std::move(op));
  return openers;
}
