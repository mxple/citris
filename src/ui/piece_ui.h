#pragma once

#include "engine/piece.h"
#include "render/colors.h"

#include <imgui.h>
#include <optional>
#include <vector>

namespace imp {

inline constexpr const char* kPieceLetters = "IOTSZJL";

inline char piece_char(PieceType t) {
  int i = static_cast<int>(t);
  return (i >= 0 && i <= 6) ? kPieceLetters[i] : '?';
}

inline std::optional<PieceType> letter_to_piece(char ch) {
  switch (ch) {
    case 'I': case 'i': return PieceType::I;
    case 'O': case 'o': return PieceType::O;
    case 'T': case 't': return PieceType::T;
    case 'S': case 's': return PieceType::S;
    case 'Z': case 'z': return PieceType::Z;
    case 'J': case 'j': return PieceType::J;
    case 'L': case 'l': return PieceType::L;
    default: return std::nullopt;
  }
}

inline std::vector<PieceType> parse_queue(const char* buf) {
  std::vector<PieceType> out;
  for (const char* p = buf; *p; ++p) {
    if (auto pt = letter_to_piece(*p)) out.push_back(*pt);
  }
  return out;
}

inline int piece_char_filter(ImGuiInputTextCallbackData* data) {
  ImWchar c = data->EventChar;
  if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
  switch (c) {
    case 'I': case 'O': case 'T': case 'S':
    case 'Z': case 'J': case 'L':
      data->EventChar = c;
      return 0;
    default:
      return 1;
  }
}

inline ImU32 piece_im_color(PieceType t) {
  Color c = cell_ref_color(static_cast<CellColor>(static_cast<int>(t) + 1));
  return IM_COL32(c.r, c.g, c.b, 255);
}

// Draw colored piece letters over the last InputText widget.
// Call immediately after ImGui::InputText().
inline void overlay_colored_queue(const char* buf) {
  ImVec2 rect_min = ImGui::GetItemRectMin();
  ImVec2 rect_max = ImGui::GetItemRectMax();
  const ImVec2& pad = ImGui::GetStyle().FramePadding;
  auto* dl = ImGui::GetWindowDrawList();

  dl->PushClipRect(rect_min, rect_max, true);
  float base_x = rect_min.x + pad.x;
  float y = rect_min.y + pad.y;
  for (int i = 0; buf[i]; ++i) {
    auto pt = letter_to_piece(buf[i]);
    if (!pt) continue;
    float x = base_x + ImGui::CalcTextSize(buf, buf + i).x;
    char tmp[2] = {piece_char(*pt), 0};
    dl->AddText(ImVec2(x, y), piece_im_color(*pt), tmp);
  }
  dl->PopClipRect();
}

}  // namespace imp
