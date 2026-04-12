#pragma once

#include "render/renderer.h"
#include "render/view_model.h"
#include "settings.h"
#include <SDL3/SDL.h>

// Draws the in-game ImGui UI: hold panel (top-left), stats panel
// (bottom-left), board panel (center), preview panel (right).
// The renderer is used to produce the board/mini-piece textures.
void draw_game_ui(Renderer &renderer, SDL_Window *window, const ViewModel &vm,
                  const Settings &settings);
