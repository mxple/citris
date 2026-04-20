#pragma once

#include "freeplay.h"
#include "settings.h"

#include <memory>

// Versus AI: player 1 plays Freeplay rules; player 2 is driven by Citris's
// own AI (via InternalTbpBot, wired in GameManager when mode2_ is set).
// Inherits FreeplayMode tuning so gravity/lock-delay/garbage-delay match the
// player's Freeplay preferences from settings.
class VersusMode : public FreeplayMode {
public:
  using FreeplayMode::FreeplayMode;

  std::string title() const override { return "Versus AI"; }

  std::unique_ptr<GameMode>
  opponent_mode(const Settings &settings) const override {
    return std::make_unique<FreeplayMode>(settings);
  }
};
