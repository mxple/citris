#pragma once

#include "render/renderer.h"
#include "render/view_model.h"
#include "settings.h"
#include <SDL3/SDL.h>
#include <span>

class GameMode;
class IController;
class AIState;
class AIController;

// Draws the in-game ImGui UI: sidebar panel (left), hold panel, stats panel,
// board panel (center), preview panel (right).
// The renderer is used to produce the board/mini-piece textures.
void draw_game_ui(Renderer &renderer, SDL_Window *window, const ViewModel &vm,
                  const Settings &settings, GameMode *mode,
                  std::span<IController *> ctrls, AIState *ai,
                  AIController *ai_ctrl);
