#pragma once

struct DebugState;
struct GameState;
class AIState;
class AIController;

// Render the F3 debug panel into the current ImGui window. Caller is the
// sidebar layout in game_ui.cc; it has already opened a window/region.
//
// `ai` and `ai_ctrl` are optional — when null, the AI section is skipped
// and only AI-independent sections (queue editor, etc.) render. Useful
// for modes that don't wire an AIController but still want debug tools.
//
// `state` is the live engine snapshot — sections that need to display or
// inspect current game state read from it (queue editor uses state.queue
// for the input hint; future sections can read whatever they need).
void draw_debug_panel(DebugState &dbg, AIState *ai, AIController *ai_ctrl,
                      const GameState &state);

// Sidebar visibility query — used by the panel layout to decide whether to
// allocate sidebar real estate before any drawing happens.
bool debug_panel_has_content(const DebugState &dbg);
