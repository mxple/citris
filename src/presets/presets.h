#pragma once

#include "blitz.h"
#include "cheese.h"
#include "freeplay.h"
#include "sprint.h"
#include "tspin_practice.h"
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
  modes.push_back(
      std::make_unique<TSpinPracticeMode>(5, TSpinVariant::DTCannon));
  modes.push_back(
      std::make_unique<TSpinPracticeMode>(5, TSpinVariant::Cspin));
  modes.push_back(
      std::make_unique<TSpinPracticeMode>(5, TSpinVariant::Fractal));
  return modes;
}
