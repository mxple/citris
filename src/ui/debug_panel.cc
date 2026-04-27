#include "ui/debug_panel.h"

#include "ai_state.h"
#include "debug_state.h"
#include "engine/game_state.h"
#include "engine/piece.h"

#include <algorithm>
#include <cstring>
#include <imgui.h>
#include <vector>

namespace {

// PieceType enum order: I, O, T, S, Z, J, L — index into this string to
// render a queue as letters.
constexpr const char *kPieceLetters = "IOTSZJL";

void draw_queue_editor(DebugState &dbg, const GameState &state) {
  if (!ImGui::CollapsingHeader("Queue Editor", ImGuiTreeNodeFlags_DefaultOpen))
    return;

  static char buf[64] = "";
  constexpr ImGuiInputTextFlags kFlags = ImGuiInputTextFlags_CharsUppercase |
                                         ImGuiInputTextFlags_CallbackCharFilter |
                                         ImGuiInputTextFlags_EnterReturnsTrue;
  auto filter = [](ImGuiInputTextCallbackData *data) -> int {
    ImWchar c = data->EventChar;
    if (c >= 'a' && c <= 'z')
      c = c - 'a' + 'A';
    switch (c) {
    case 'J': case 'L': case 'S': case 'Z':
    case 'O': case 'T': case 'I':
      data->EventChar = c;
      return 0;
    default:
      return 1;
    }
  };

  // Hint = current piece + preview, matching the user's mental model of the
  // sequence (and matching the apply semantics — input[0] becomes current
  // piece, rest replaces queue prefix). Sized to fit buf so a long queue
  // doesn't push past the input width.
  char hint[sizeof(buf)] = {};
  int idx = 0;
  hint[idx++] = kPieceLetters[static_cast<int>(state.current_piece.type)];
  int max_q = std::min(static_cast<int>(state.queue.size()),
                       static_cast<int>(sizeof(hint) - 1 - idx));
  for (int i = 0; i < max_q; ++i)
    hint[idx++] = kPieceLetters[static_cast<int>(state.queue[i])];

  ImGui::SetNextItemWidth(-FLT_MIN);
  bool submit = ImGui::InputTextWithHint("##queueedit", hint, buf, sizeof(buf),
                                         kFlags, filter);
  submit |= ImGui::Button("Apply##queueedit");
  ImGui::SameLine();
  if (ImGui::Button("Clear Hold##queueedit"))
    dbg.clear_hold = true;

  if (!submit)
    return;

  std::vector<PieceType> pieces;
  pieces.reserve(std::strlen(buf));
  for (const char *p = buf; *p; ++p) {
    switch (*p) {
    case 'J': pieces.push_back(PieceType::J); break;
    case 'L': pieces.push_back(PieceType::L); break;
    case 'S': pieces.push_back(PieceType::S); break;
    case 'Z': pieces.push_back(PieceType::Z); break;
    case 'O': pieces.push_back(PieceType::O); break;
    case 'T': pieces.push_back(PieceType::T); break;
    case 'I': pieces.push_back(PieceType::I); break;
    default: break;
    }
  }
  if (!pieces.empty())
    dbg.pending_queue_replacement = std::move(pieces);

  // Clear the buffer so the hint reflects the post-apply queue on the next
  // frame. The user committed; further edits should start from a blank slate.
  buf[0] = '\0';
}

} // namespace

void draw_debug_panel(DebugState &dbg, AIState *ai, AIController *ai_ctrl,
                      const GameState &state) {
  if (!dbg.open)
    return;

  draw_queue_editor(dbg, state);

  if (ai && ai_ctrl)
    ai->draw_ai_controls(*ai_ctrl);
}

bool debug_panel_has_content(const DebugState &dbg) { return dbg.open; }
