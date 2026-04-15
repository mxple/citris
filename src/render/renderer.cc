#include "renderer.h"
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <iostream>

using L = RenderLayout;

static constexpr Color kFallbackColors[] = {
    Color::Transparent(), // Empty
    Color(135, 206, 250), // I
    Color(255, 255, 0),   // O
    Color(186, 85, 211),  // T
    Color(50, 205, 50),   // S
    Color(255, 105, 180), // Z
    Color(30, 144, 255),  // J
    Color(255, 165, 0),   // L
    Color(128, 128, 128), // Garbage
};

// Convert a scene-local y_up (0 = bottom of the scene, kSceneRows-1 = top) to
// the pixel Y of the top edge of that cell inside the scene texture.
static constexpr float row_px(float y_up) {
  return (L::kSceneRows - 1 - L::kPadRowsSouth - y_up) * L::kTileSize;
}

struct LineSeg {
  float x1, y1, x2, y2;
};

int Renderer::cell_to_skin(CellColor cc) {
  switch (cc) {
  case CellColor::Z:
    return 0;
  case CellColor::L:
    return 1;
  case CellColor::O:
    return 2;
  case CellColor::S:
    return 3;
  case CellColor::I:
    return 4;
  case CellColor::J:
    return 5;
  case CellColor::T:
    return 6;
  case CellColor::Garbage:
    return L::kSkinGarbage;
  case CellColor::Empty:
    return 0;
  }
  return 0;
}

int Renderer::piece_to_skin(PieceType type) {
  return cell_to_skin(piece_to_cell_color(type));
}

Renderer::Renderer(SDL_Renderer *renderer, const Settings &settings)
    : renderer_(renderer), settings_(settings) {
  load_skin();
}

Renderer::~Renderer() {
  if (scene_tex_)
    SDL_DestroyTexture(scene_tex_);
  if (skin_)
    SDL_DestroyTexture(skin_);
}

void Renderer::load_skin() {
  if (skin_) {
    SDL_DestroyTexture(skin_);
    skin_ = nullptr;
    skin_ok_ = false;
  }
  if (settings_.skin_path.empty())
    return;

  skin_ = IMG_LoadTexture(renderer_, settings_.skin_path.c_str());
  if (!skin_) {
    std::cerr << "Failed to load skin: " << settings_.skin_path << " ("
              << SDL_GetError() << ")\n";
    return;
  }
  skin_ok_ = true;
  // Skin tiles are 30px and cells render at exactly 30px - no filtering.
  SDL_SetTextureScaleMode(skin_, SDL_SCALEMODE_NEAREST);
  SDL_SetTextureBlendMode(skin_, SDL_BLENDMODE_BLEND);

  for (int t = 0; t < L::kSkinTiles; ++t) {
    skin_src_[t] = {static_cast<float>(t * L::kSkinPitch), 0.f,
                    static_cast<float>(L::kTileSize),
                    static_cast<float>(L::kTileSize)};
  }
}

SDL_Texture *Renderer::draw_scene_to_texture(const ViewModel &vm) {
  const auto &state = vm.state;
  constexpr int kTile = L::kTileSize; // 30
  constexpr int kTargetW = L::kSceneCols * kTile;
  constexpr int kTargetH = L::kSceneRows * kTile;

  if (!scene_tex_) {
    scene_tex_ =
        SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                          SDL_TEXTUREACCESS_TARGET, kTargetW, kTargetH);
    if (!scene_tex_) {
      std::cerr << "Failed to create scene render texture: " << SDL_GetError()
                << "\n";
      return nullptr;
    }
    // Scene texture is upscaled by ImGui to the window - keep it nearest so
    // the 30px cells stay crisp at any integer multiple and only blur on
    // non-integer fractions (which is unavoidable without per-frame resize).
    SDL_SetTextureScaleMode(scene_tex_, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(scene_tex_, SDL_BLENDMODE_BLEND);
  }

  // Save and clear any global render scale while drawing into our target
  // texture (the caller's SDL_SetRenderScale is for mapping ImGui's logical
  // coords to physical; it shouldn't apply to our pixel-accurate target).
  float saved_sx = 1.f, saved_sy = 1.f;
  SDL_GetRenderScale(renderer_, &saved_sx, &saved_sy);
  SDL_SetRenderTarget(renderer_, scene_tex_);
  SDL_SetRenderScale(renderer_, 1.f, 1.f);
  SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
  SDL_RenderClear(renderer_);
  // All subsequent draws must alpha-blend: gridlines have per-pixel alpha,
  // the skin texture has alpha, and the game-over overlay is translucent.
  // Without this the last draw simply overwrites both RGB and alpha.
  SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

  // Backdrop: playfield background, border, gridlines.
  draw_play_background();
  draw_board_border();
  draw_gridlines();

  // Content: board cells, checkpoint overlay, plan overlay, ghost, active piece.
  draw_board(state.board);
  // if (vm.checkpoint_overlay)
  //   draw_checkpoint_overlay(*vm.checkpoint_overlay);
  if (!vm.plan_overlay.empty())
    draw_plan_overlay(vm.plan_overlay);
  draw_ghost(state.ghost_piece);
  draw_piece(state.current_piece);
  if (state.hold_piece) {
    draw_mini_piece(*state.hold_piece, !state.hold_available, L::kHoldColX, 17);
  }
  for (int i = 0; i < 5; i++) {
    draw_mini_piece(state.preview[i], false, L::kNextColX, 17 - 3 * i);
  }

  // Game-over darken overlay above everything.
  if (state.game_over) {
    constexpr float play_x = L::kBoardColX * L::kTileSize;
    constexpr float play_y = row_px(L::kPlayRows - 1);
    draw_solid({play_x, play_y, L::kBoardCols * L::kTileSize,
                L::kPlayRows * L::kTileSize},
               Color(0, 0, 0, 150));
  }
  SDL_SetRenderTarget(renderer_, nullptr);
  SDL_SetRenderScale(renderer_, saved_sx, saved_sy);
  return scene_tex_;
}

void Renderer::draw_play_background() {
  if (settings_.board_opacity == 0)
    return;
  constexpr float t = L::kTileSize;
  constexpr float px = L::kBoardColX * t;
  constexpr float py = row_px(L::kPlayRows - 1);
  draw_solid({px, py, L::kBoardCols * t, L::kPlayRows * t},
             Color(0, 0, 0, settings_.board_opacity));
}

void Renderer::draw_board_border() {
  constexpr float t = L::kTileSize;
  constexpr float o = L::kBoardOutline;
  constexpr float px = L::kBoardColX * t;
  constexpr float py = row_px(L::kPlayRows - 1);
  constexpr float bw = L::kBoardCols * t;
  constexpr float ph = L::kPlayRows * t;
  draw_solid({px - o, py - o, bw + 2 * o, o}, Color(120, 120, 120, 255)); // top
  draw_solid({px - o, py + ph, bw + 2 * o, o},
             Color(120, 120, 120, 255));                       // bottom
  draw_solid({px - o, py, o, ph}, Color(120, 120, 120, 255));  // left
  draw_solid({px + bw, py, o, ph}, Color(120, 120, 120, 255)); // right
}

void Renderer::draw_gridlines() {
  if (settings_.grid_opacity == 0)
    return;
  static constexpr auto kLines = [] {
    constexpr float t = L::kTileSize;
    constexpr float px = L::kBoardColX * t;
    constexpr float py = row_px(L::kPlayRows - 1);
    constexpr float bw = L::kBoardCols * t;
    constexpr float ph = L::kPlayRows * t;
    std::array<LineSeg, (L::kBoardCols - 1) + (L::kPlayRows - 1)> lines{};
    int i = 0;
    for (int col = 1; col < L::kBoardCols; ++col)
      lines[i++] = {px + col * t, py, px + col * t, py + ph};
    for (int row = 1; row < L::kPlayRows; ++row)
      lines[i++] = {px, py + row * t, px + bw, py + row * t};
    return lines;
  }();
  SDL_SetRenderDrawColor(renderer_, 255, 255, 255, settings_.grid_opacity);
  for (auto &l : kLines)
    SDL_RenderLine(renderer_, l.x1, l.y1, l.x2, l.y2);
}

void Renderer::draw_board(const Board &board) {
  constexpr float t = L::kTileSize;
  for (int row = 0; row < L::kPlayRows + L::kPadRowsNorth; ++row) {
    for (int col = 0; col < L::kBoardCols; ++col) {
      auto cc = board.cell_color(col, row);
      if (cc != CellColor::Empty) {
        float px = (L::kBoardColX + col) * t;
        float py = row_px(row);
        draw_tile(px, py, cell_to_skin(cc));
      }
    }
  }
}

void Renderer::draw_piece(const Piece &piece) {
  constexpr float t = L::kTileSize;
  int skin = piece_to_skin(piece.type);
  for (auto &cell : piece.cells_absolute()) {
    float px = (L::kBoardColX + cell.x) * t;
    float py = row_px(cell.y);
    draw_tile(px, py, skin);
  }
}

void Renderer::draw_ghost(const Piece &ghost) {
  constexpr float t = L::kTileSize;
  Color tint(255, 255, 255, settings_.ghost_opacity);
  int skin =
      settings_.colored_ghost ? piece_to_skin(ghost.type) : L::kSkinGhost;
  for (auto &cell : ghost.cells_absolute()) {
    float px = (L::kBoardColX + cell.x) * t;
    float py = row_px(cell.y);
    draw_tile(px, py, skin, tint);
  }
}

void Renderer::draw_mini_piece(PieceType type, bool greyed, float region_col,
                               float region_y_up) {
  constexpr float t = L::kTileSize;
  const auto &cells =
      kPieceCells[static_cast<int>(type)][static_cast<int>(Rotation::North)];
  int min_x = cells[0].x, max_x = cells[0].x;
  int min_y = cells[0].y, max_y = cells[0].y;
  for (auto &c : cells) {
    min_x = std::min(min_x, c.x);
    max_x = std::max(max_x, c.x);
    min_y = std::min(min_y, c.y);
    max_y = std::max(max_y, c.y);
  }
  float bb_cx = (min_x + max_x + 1) * 0.5f;
  float bb_cy = (min_y + max_y + 1) * 0.5f;
  float off_x = L::kSideCols * 0.5f - bb_cx;
  float off_y = L::kMiniRows * 0.5f - bb_cy;

  int skin_tile = greyed ? L::kSkinGreyedHold : piece_to_skin(type);
  for (auto &c : cells) {
    float cx = region_col + (c.x + off_x);
    float cy_up = region_y_up + (c.y + off_y);
    float px = cx * t;
    float py = row_px(cy_up);
    draw_tile(px, py, skin_tile);
  }
}

void Renderer::draw_tile(float x, float y, int tile_idx, Color tint) {
  constexpr float t = L::kTileSize;
  SDL_FRect dst{x, y, t, t};
  if (!skin_ok_) {
    static constexpr int kSkinToCellColor[] = {5, 7, 2, 4, 1, 6,
                                               3, 0, 8, 8, 0, 0};
    int cc = kSkinToCellColor[tile_idx];
    auto base = kFallbackColors[cc];
    Color c(base.r, base.g, base.b,
            static_cast<uint8_t>(base.a * tint.a / 255));
    draw_solid(dst, c);
    return;
  }
  SDL_SetTextureColorMod(skin_, tint.r, tint.g, tint.b);
  SDL_SetTextureAlphaMod(skin_, tint.a);
  SDL_RenderTexture(renderer_, skin_, &skin_src_[tile_idx], &dst);
}

void Renderer::draw_solid(const SDL_FRect &dst, Color color) {
  SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
  SDL_RenderFillRect(renderer_, &dst);
}

void Renderer::draw_plan_overlay(
    const std::vector<PlannedPlacement> &placements) {
  constexpr float t = L::kTileSize;
  for (auto &pp : placements) {
    int skin = piece_to_skin(pp.type);
    auto alpha = static_cast<uint8_t>(std::clamp(pp.alpha * 180.f, 0.f, 255.f));
    Color tint(255, 255, 255, alpha);
    for (auto &cell : pp.cells) {
      if (cell.y < 0 || cell.y >= L::kPlayRows + L::kPadRowsNorth)
        continue;
      if (cell.x < 0 || cell.x >= L::kBoardCols)
        continue;
      float px = (L::kBoardColX + cell.x) * t;
      float py = row_px(cell.y);
      draw_tile(px, py, skin, tint);
    }
  }
}

void Renderer::draw_checkpoint_overlay(const CheckpointOverlay &overlay) {
  constexpr float t = L::kTileSize;
  auto alpha = static_cast<uint8_t>(
      std::clamp(overlay.alpha * 255.f, 0.f, 255.f));
  Color color(100, 180, 255, alpha);
  for (int y = 0; y < static_cast<int>(overlay.rows.size()); ++y) {
    uint16_t row = overlay.rows[y];
    for (int x = 0; x < L::kBoardCols; ++x) {
      if ((row >> x) & 1) {
        float px = (L::kBoardColX + x) * t;
        float py = row_px(y);
        draw_solid({px, py, t, t}, color);
      }
    }
  }
}
