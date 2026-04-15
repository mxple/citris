#pragma once

#include "blitz.h"
#include "cheese.h"
#include "freeplay.h"
#include "opener.h"
#include "sprint.h"
#include "ai/opener_db.h"
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

// Return available openers for the opener selection sub-screen.
inline std::vector<Opener> all_openers() {
  auto openers = builtin_openers();
  auto from_dir = load_openers_from_dir("assets/openers");
  for (auto &op : from_dir)
    openers.push_back(std::move(op));
  return openers;
}
