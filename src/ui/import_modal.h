#pragma once

#include <SDL3/SDL.h>

struct DebugState;
namespace imp { struct CvImage; }

// Multi-step modal for selecting playfield / hold / current / queue rects on
// a clipboard-decoded image, classifying each, and writing the results into
// `DebugState`'s pending_*_import side channels.
//
// Lifecycle:
//   - open_import_modal(renderer, image) — caller provides the decoded image
//     and the SDL renderer (modal owns the resulting SDL_Texture).
//   - draw_import_modal(dbg) is called every frame while open. It returns
//     immediately when not open.
//   - On commit, the modal writes pending_* fields into `dbg`. ToolController
//     drains them into engine commands on the next tick.
//
// Only one modal can be open at a time; opening a second one closes the first.
namespace imp {

class CvImage;  // forward only — the actual definition is in cv_image.h

void open_import_modal(SDL_Renderer* renderer, ::imp::CvImage image);
void draw_import_modal(DebugState& dbg);
bool import_modal_is_open();

}  // namespace imp
