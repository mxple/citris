#pragma once

#include "blitz.h"
#include "cheese.h"
#include "finesse_trainer.h"
#include "freeplay.h"
#include "settings.h"
#include "sprint.h"
#include "tspin_practice.h"
#include "versus.h"
#include <memory>
#include <vector>

inline std::vector<std::unique_ptr<GameMode>> play_modes(const Settings& settings) {
  std::vector<std::unique_ptr<GameMode>> modes;
  modes.push_back(std::make_unique<FreeplayMode>(settings));
  modes.push_back(std::make_unique<SprintMode>());
  modes.push_back(std::make_unique<BlitzMode>());
  modes.push_back(std::make_unique<CheeseMode>());
  modes.push_back(std::make_unique<VersusMode>(settings));
  return modes;
}

inline std::vector<std::unique_ptr<GameMode>> training_modes(const Settings&) {
  std::vector<std::unique_ptr<GameMode>> modes;
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
  modes.push_back(
      std::make_unique<FinesseTrainerMode>(FinesseDifficulty::Normal));
  modes.push_back(
      std::make_unique<FinesseTrainerMode>(FinesseDifficulty::Advanced));
  return modes;
}
