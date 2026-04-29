#include "ui/import_modal.h"
#include "ui/piece_ui.h"

#include "cv/color.h"
#include "cv/cv_image.h"
#include "cv/detect.h"
#include "debug_state.h"
#include "render/colors.h"

#include <algorithm>
#include <imgui.h>
#include <optional>
#include <string>

namespace imp {

namespace {

struct ModalState {
  bool open = false;
  CvImage img;
  SDL_Texture* tex = nullptr;
  std::optional<Detection> det;
  // Params persist across opens — last successful tuning seeds the next
  // session, so a one-time skin calibration carries forward.
  DetectParams params;
  // Manual override for hold/current piece type. 0 = None, 1-7 = I/O/T/S/Z/J/L.
  // Populated from CV on each detection run; user can override via dropdown.
  int hold_sel = 0;
  int current_sel = 0;
  int hue_preset = 0;
  char queue_buf[32] = {};
};

ModalState g;

void destroy_texture() {
  if (g.tex) {
    SDL_DestroyTexture(g.tex);
    g.tex = nullptr;
  }
}

void close_modal() {
  destroy_texture();
  g.open = false;
  g.img = CvImage{};
  g.det.reset();
  g.hold_sel = 0;
  g.current_sel = 0;
  g.queue_buf[0] = '\0';
}

// Convert a detection grid (r=0 top of visible playfield) to engine
// BoardCells (r=0 at bottom, rows 20-39 are spawn buffer, kept Empty).
BoardCells grid_to_engine(const Detection::Grid& grid) {
  BoardCells out{};
  for (int r = 0; r < Board::kVisibleHeight; ++r) {
    int engine_row = Board::kVisibleHeight - 1 - r;
    for (int c = 0; c < Board::kWidth; ++c) out[engine_row][c] = grid[r][c];
  }
  return out;
}

void commit(DebugState& dbg) {
  if (!g.det) return;
  Detection& det = *g.det;

  if (g.current_sel > 0) {
    auto pt = static_cast<PieceType>(g.current_sel - 1);
    if (det.current_piece && *det.current_piece == pt) {
      for (auto [r, c] : det.current_cells)
        if (r >= 0 && r < Board::kVisibleHeight)
          det.grid[r][c] = CellColor::Empty;
    }
    dbg.pending_current_import = pt;
  }
  dbg.pending_board_import = grid_to_engine(det.grid);
  if (g.hold_sel > 0)
    dbg.pending_hold_import = static_cast<PieceType>(g.hold_sel - 1);
  auto queue = parse_queue(g.queue_buf);
  if (!queue.empty())
    dbg.pending_queue_import = std::move(queue);
}

void run_detection() {
  g.det = detect(g.img, g.params);
  if (g.det) {
    g.hold_sel = g.det->hold_piece ? (int(*g.det->hold_piece) + 1) : 0;
    g.current_sel = g.det->current_piece ? (int(*g.det->current_piece) + 1) : 0;
    char* p = g.queue_buf;
    for (auto pt : g.det->queue_pieces) {
      if (p - g.queue_buf >= (int)sizeof(g.queue_buf) - 1) break;
      *p++ = piece_char(pt);
    }
    *p = '\0';
  } else {
    g.hold_sel = 0;
    g.current_sel = 0;
    g.queue_buf[0] = '\0';
  }
}

// Returns true if any control mutated. Caller re-runs detection on change.
bool draw_param_controls() {
  bool changed = false;
  float w = ImGui::GetContentRegionAvail().x * 0.55f;

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Cell thresh");
  ImGui::SameLine(ImGui::GetContentRegionAvail().x - w + ImGui::GetCursorPosX());
  ImGui::SetNextItemWidth(w);
  changed |= ImGui::SliderFloat("##cellthresh", &g.params.cell_thresh,
                                 0.f, 1.f, "%.2f");

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Sat weight");
  ImGui::SameLine(ImGui::GetContentRegionAvail().x - w + ImGui::GetCursorPosX());
  ImGui::SetNextItemWidth(w);
  changed |= ImGui::SliderFloat("##satweight", &g.params.cell_sat_weight,
                                 0.f, 1.f, "%.2f");

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Edge percentile");
  ImGui::SameLine(ImGui::GetContentRegionAvail().x - w + ImGui::GetCursorPosX());
  ImGui::SetNextItemWidth(w);
  changed |= ImGui::SliderInt("##edgepct", &g.params.edge_percentile, 50, 99);

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Hue preset");
  ImGui::SameLine(ImGui::GetContentRegionAvail().x - w + ImGui::GetCursorPosX());
  ImGui::SetNextItemWidth(w);
  int preset = g.hue_preset;
  if (ImGui::Combo("##huepreset", &preset,
                   "TETR.IO\0Jstris\0Puyo Puyo Tetris\0Tetris 99\0")) {
    g.hue_preset = preset;
    g.params.piece_hues = kHuePresets[preset];
    changed = true;
  }

  return changed;
}

ImVec2 image_to_screen(ImVec2 origin, ImVec2 size, int x, int y) {
  float sx = (g.img.w > 0) ? size.x / float(g.img.w) : 0.f;
  float sy = (g.img.h > 0) ? size.y / float(g.img.h) : 0.f;
  return {origin.x + x * sx, origin.y + y * sy};
}

void draw_rect_overlay(ImDrawList* dl, ImVec2 origin, ImVec2 size,
                       const CvRect& r, ImU32 color, const char* label) {
  ImVec2 p0 = image_to_screen(origin, size, r.x, r.y);
  ImVec2 p1 = image_to_screen(origin, size, r.x + r.w, r.y + r.h);
  dl->AddRect(p0, p1, color, 0.f, 0, 2.f);
  if (label && *label) {
    ImVec2 lp = ImVec2(p0.x + 3.f, p0.y + 1.f);
    dl->AddRectFilled(ImVec2(lp.x - 2.f, lp.y),
                      ImVec2(lp.x + ImGui::CalcTextSize(label).x + 2.f,
                             lp.y + ImGui::GetTextLineHeight()),
                      IM_COL32(0, 0, 0, 160));
    dl->AddText(lp, color, label);
  }
}

}  // namespace

void open_import_modal(SDL_Renderer* renderer, CvImage image) {
  close_modal();
  g.img = std::move(image);
  if (renderer && g.img.w > 0 && g.img.h > 0) {
    g.tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                              SDL_TEXTUREACCESS_STATIC, g.img.w, g.img.h);
    if (g.tex) {
      SDL_UpdateTexture(g.tex, nullptr, g.img.rgba.data(), g.img.w * 4);
      SDL_SetTextureScaleMode(g.tex, SDL_SCALEMODE_LINEAR);
    }
  }
  g.open = true;
  run_detection();
}

bool import_modal_is_open() { return g.open; }

void draw_import_modal(DebugState& dbg) {
  if (!g.open) return;

  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos, ImGuiCond_Always);
  ImGui::SetNextWindowSize(vp->WorkSize, ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.08f, 1.f));

  constexpr ImGuiWindowFlags kFlags =
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoCollapse;

  bool keep_open = true;
  bool window_visible =
      ImGui::Begin("Import from Clipboard##importmodal", &keep_open, kFlags);
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
  if (!keep_open) {
    ImGui::End();
    close_modal();
    return;
  }
  if (!window_visible) {
    ImGui::End();
    return;
  }

  // ----- Detection summary -----
  if (!g.det) {
    ImGui::TextColored(ImVec4(1.f, 0.5f, 0.5f, 1.f),
                        "Board not detected. Try a different screenshot.");
  } else {
    const Detection& d = *g.det;
    float w = ImGui::GetContentRegionAvail().x * 0.55f;

    ImGui::Text("Board: %dx%d px @ pitch %.1f", d.board.w, d.board.h, d.pitch);

    static const char* kPieceOpts[] = {
        "None", "I", "O", "T", "S", "Z", "J", "L",
    };
    auto piece_combo = [&](const char* id, int* sel) {
      const char* preview = kPieceOpts[*sel];
      if (*sel > 0) ImGui::PushStyleColor(ImGuiCol_Text,
          piece_im_color(static_cast<PieceType>(*sel - 1)));
      bool open = ImGui::BeginCombo(id, preview);
      if (*sel > 0) ImGui::PopStyleColor();
      if (open) {
        for (int i = 0; i < 8; ++i) {
          if (i > 0) ImGui::PushStyleColor(ImGuiCol_Text,
              piece_im_color(static_cast<PieceType>(i - 1)));
          if (ImGui::Selectable(kPieceOpts[i], *sel == i)) *sel = i;
          if (i > 0) ImGui::PopStyleColor();
        }
        ImGui::EndCombo();
      }
    };

    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImVec4(1.f, 0.7f, 0.2f, 1.f), "Hold");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - w + ImGui::GetCursorPosX());
    ImGui::SetNextItemWidth(w);
    piece_combo("##holdsel", &g.hold_sel);

    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImVec4(1.f, 1.f, 0.3f, 1.f), "Current");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - w + ImGui::GetCursorPosX());
    ImGui::SetNextItemWidth(w);
    piece_combo("##currentsel", &g.current_sel);

    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.f, 1.f), "Queue");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - w + ImGui::GetCursorPosX());
    ImGui::SetNextItemWidth(w);
    constexpr ImGuiInputTextFlags kQFlags =
        ImGuiInputTextFlags_CharsUppercase |
        ImGuiInputTextFlags_CallbackCharFilter;
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 128));
    ImGui::InputText("##queue", g.queue_buf, sizeof(g.queue_buf),
                     kQFlags, piece_char_filter);
    ImGui::PopStyleColor();
    overlay_colored_queue(g.queue_buf);
  }

  // ----- Live-tunable detection params -----
  if (draw_param_controls()) run_detection();

  // ----- Image area -----
  ImVec2 avail = ImGui::GetContentRegionAvail();
  float button_row_h = ImGui::GetFrameHeightWithSpacing() * 1.2f;
  avail.y = std::max(100.f, avail.y - button_row_h);
  float img_aspect = (g.img.h > 0) ? float(g.img.w) / float(g.img.h) : 1.f;
  float disp_w = avail.x;
  float disp_h = disp_w / img_aspect;
  if (disp_h > avail.y) {
    disp_h = avail.y;
    disp_w = disp_h * img_aspect;
  }
  ImVec2 origin = ImGui::GetCursorScreenPos();
  ImVec2 size = ImVec2(disp_w, disp_h);
  // InvisibleButton + manual draw avoids the window-drag conflict that hit
  // the previous interactive flow.
  ImGui::InvisibleButton("##importimg", size);

  // Erase mispredicted cells by clicking/drawing over them.
  if (g.det && (ImGui::IsItemActive() || ImGui::IsItemClicked(0))) {
    ImVec2 mouse = ImGui::GetMousePos();
    const Detection& d = *g.det;
    float sx = (g.img.w > 0) ? float(g.img.w) / size.x : 0.f;
    float sy = (g.img.h > 0) ? float(g.img.h) / size.y : 0.f;
    float img_x = (mouse.x - origin.x) * sx;
    float img_y = (mouse.y - origin.y) * sy;
    float cw = d.board.w / float(Board::kWidth);
    float ch = d.board.h / float(Board::kVisibleHeight);
    int col = int((img_x - d.board.x) / cw);
    int row = int((img_y - d.board.y) / ch);
    if (row >= 0 && row < Board::kVisibleHeight &&
        col >= 0 && col < Board::kWidth) {
      g.det->grid[row][col] = CellColor::Empty;
    }
  }

  auto* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                    IM_COL32(0, 0, 0, 255));
  if (g.tex) {
    dl->AddImage((ImTextureID)(intptr_t)g.tex, origin,
                 ImVec2(origin.x + size.x, origin.y + size.y),
                 ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 128));
  }

  if (g.det) {
    const Detection& d = *g.det;
    draw_rect_overlay(dl, origin, size, d.board, IM_COL32(60, 240, 80, 255),
                      "board");
    if (d.hold && g.hold_sel > 0) {
      draw_rect_overlay(dl, origin, size, *d.hold,
                        IM_COL32(255, 180, 50, 255), "hold");
    }
    for (size_t i = 0; i < d.queue.size(); ++i) {
      if (d.queue[i].w <= 0 || d.queue[i].h <= 0) continue;
      char buf[8];
      std::snprintf(buf, sizeof(buf), "q%zu", i + 1);
      draw_rect_overlay(dl, origin, size, d.queue[i],
                        IM_COL32(80, 200, 255, 255), buf);
    }

    float cw = d.board.w / float(Board::kWidth);
    float ch = d.board.h / float(Board::kVisibleHeight);

    // 10x20 gridlines inside the board rect.
    ImU32 grid_col = IM_COL32(255, 255, 255, 77);
    for (int c = 1; c < Board::kWidth; ++c) {
      int px = int(d.board.x + c * cw);
      ImVec2 top = image_to_screen(origin, size, px, d.board.y);
      ImVec2 bot = image_to_screen(origin, size, px, d.board.y + d.board.h);
      dl->AddLine(top, bot, grid_col, 1.f);
    }
    for (int r = 1; r < Board::kVisibleHeight; ++r) {
      int py = int(d.board.y + r * ch);
      ImVec2 lft = image_to_screen(origin, size, d.board.x, py);
      ImVec2 rgt = image_to_screen(origin, size, d.board.x + d.board.w, py);
      dl->AddLine(lft, rgt, grid_col, 1.f);
    }

    // Filled colored square (30% of cell size) centered in each non-empty cell.
    for (int r = 0; r < Board::kVisibleHeight; ++r) {
      for (int c = 0; c < Board::kWidth; ++c) {
        CellColor cc = d.grid[r][c];
        if (cc == CellColor::Empty) continue;
        ImVec2 p0 = image_to_screen(origin, size,
                                     d.board.x + int(c * cw),
                                     d.board.y + int(r * ch));
        ImVec2 p1 = image_to_screen(origin, size,
                                     d.board.x + int((c + 1) * cw),
                                     d.board.y + int((r + 1) * ch));
        float qx = (p1.x - p0.x) * 0.35f;
        float qy = (p1.y - p0.y) * 0.35f;
        Color ref = cell_ref_color(cc);
        dl->AddRectFilled(ImVec2(p0.x + qx, p0.y + qy),
                          ImVec2(p1.x - qx, p1.y - qy),
                          IM_COL32(ref.r, ref.g, ref.b, 255));
      }
    }

    // Current piece — outline the 4 cells in yellow on top of letters.
    if (d.current_piece && g.current_sel > 0) {
      for (auto [r, c] : d.current_cells) {
        CvRect cell{
            d.board.x + int(c * cw), d.board.y + int(r * ch),
            std::max(1, int(cw)), std::max(1, int(ch)),
        };
        ImVec2 p0 = image_to_screen(origin, size, cell.x, cell.y);
        ImVec2 p1 = image_to_screen(origin, size, cell.x + cell.w,
                                     cell.y + cell.h);
        dl->AddRect(p0, p1, IM_COL32(255, 240, 60, 255), 0.f, 0, 2.f);
      }
    }

  }

  // ----- Bottom controls -----
  ImGui::Separator();
  // Stage close requests instead of returning early — taking an early `return`
  // out of an unmatched `BeginDisabled` (or any push-pop scope) corrupts the
  // global ImGui state stack and breaks every subsequent window, including
  // the debug panel that hosts this modal.
  bool want_commit = false;
  bool want_close = false;

  ImGui::BeginDisabled(!g.det.has_value());
  if (ImGui::Button("Commit")) {
    want_commit = true;
    want_close = true;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Re-detect")) run_detection();
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) want_close = true;

  ImGui::End();

  if (want_commit) commit(dbg);
  if (want_close) close_modal();
}

}  // namespace imp
