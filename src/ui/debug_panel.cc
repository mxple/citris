#include "ui/debug_panel.h"
#include "ui/ai_debug_panel.h"
#include "ui/piece_ui.h"

#include "ai_state.h"
#include "cv/clipboard.h"
#include "cv/cv_image.h"
#include "debug_state.h"
#include "engine/game_state.h"
#include "engine/piece.h"
#include "log.h"
#include "ui/import_modal.h"

#include <algorithm>
#include <cstring>
#include <imgui.h>
#include <vector>

using imp::kPieceLetters;
using imp::piece_char_filter;
using imp::parse_queue;

namespace {

void draw_queue_editor(DebugState &dbg, const GameState &state) {
  static char buf[64] = "";
  constexpr ImGuiInputTextFlags kFlags = ImGuiInputTextFlags_CharsUppercase |
                                         ImGuiInputTextFlags_CallbackCharFilter |
                                         ImGuiInputTextFlags_EnterReturnsTrue;

  char hint[sizeof(buf)] = {};
  int idx = 0;
  hint[idx++] = kPieceLetters[static_cast<int>(state.current_piece.type)];
  int max_q = std::min(static_cast<int>(state.queue.size()),
                       static_cast<int>(sizeof(hint) - 1 - idx));
  for (int i = 0; i < max_q; ++i)
    hint[idx++] = kPieceLetters[static_cast<int>(state.queue[i])];

  ImGui::Text("Queue:");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 128));
  bool submit = ImGui::InputTextWithHint("##queueedit", hint, buf, sizeof(buf),
                                         kFlags, piece_char_filter);
  ImGui::PopStyleColor();
  imp::overlay_colored_queue(buf);
  if (ImGui::Button("Clear Hold##queueedit"))
    dbg.clear_hold = true;

  if (!submit)
    return;

  auto pieces = parse_queue(buf);
  if (!pieces.empty())
    dbg.pending_queue_replacement = std::move(pieces);

  buf[0] = '\0';
}

void draw_clipboard_import(DebugState &dbg, SDL_Renderer *renderer) {
  static std::string s_last_error;

  if (ImGui::Button("Clipboard Import")) {
    s_last_error.clear();
    auto img = imp::read_clipboard_image();
    if (!img) {
      s_last_error = "No image in clipboard (or unsupported format).";
    } else if (!renderer) {
      s_last_error = "No SDL renderer available for preview texture.";
    } else {
      imp::open_import_modal(renderer, std::move(*img));
    }
  }
  if (!s_last_error.empty()) {
    ImGui::TextColored(ImVec4(1.f, 0.5f, 0.5f, 1.f), "%s", s_last_error.c_str());
  }
// #endif
}

} // namespace

void draw_debug_panel(DebugState &dbg, AIState *ai, AIController *ai_ctrl,
                      const GameState &state, SDL_Renderer *renderer) {
  if (!dbg.open)
    return;

  if (ImGui::CollapsingHeader("Board Editor", ImGuiTreeNodeFlags_DefaultOpen)) {
    draw_queue_editor(dbg, state);
    draw_clipboard_import(dbg, renderer);
  }

  if (ai && ai_ctrl)
    draw_ai_controls(*ai, *ai_ctrl);

  // Modal lives on top of all sidebar content; draw it after the panel so
  // late-frame interaction wins. The function no-ops when the modal is closed.
  imp::draw_import_modal(dbg);
}

bool debug_panel_has_content(const DebugState &dbg) { return dbg.open; }
