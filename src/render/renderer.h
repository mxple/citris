#pragma once

#include "engine/game_state.h"
#include "engine/piece.h"
#include "settings.h"
#include "view_model.h"
#include <SDL3/SDL.h>
#include <array>

// Constants for rendering game
struct RenderLayout {
  static constexpr int kBoardCols = Board::kWidth;        // 10
  static constexpr int kPlayRows = Board::kVisibleHeight; // 20
  static constexpr int kPadRowsNorth = 3;                 // spawn area
  static constexpr int kPadRowsSouth = 1; // 1 cell padding under play area
  static constexpr int kSideCols = 5;     // hold / next width
  static constexpr int kMiniRows = 3;     // hold / preview slot height

  static constexpr int kSceneCols = kSideCols + kBoardCols + kSideCols;
  static constexpr int kSceneRows = kPlayRows + kPadRowsNorth + kPadRowsSouth;

  // Column offsets of the three main columns.
  static constexpr int kHoldColX = 0;
  static constexpr int kBoardColX = kSideCols;
  static constexpr int kNextColX = kSideCols + kBoardCols;

  // Skin texture coords — pixel coords in skin.png
  static constexpr int kTileSize = 30;
  static constexpr int kSkinPitch = 31;
  static constexpr int kSkinTiles = 12;

  static constexpr int kBoardOutline = 4; // border thickness in pixels

  static constexpr int kSkinGhost = 7;
  static constexpr int kSkinGarbage = 8;
  static constexpr int kSkinGreyedHold = 10;
};

class Renderer {
public:
  Renderer(SDL_Renderer *renderer, const Settings &settings);
  ~Renderer();

  // Render the full game scene (hold area, playfield, current piece,
  // preview area, plan overlay) into an internal SDL_Texture
  // of fixed size (kSceneCols * kSkinTile) × (kSceneRows * kSkinTile).
  // `slot` selects which of two internal textures to use — versus mode
  // passes slot=0 for the left board and slot=1 for the right.
  // The caller is responsible for scaling this texture to the display target.
  SDL_Texture *draw_scene_to_texture(const ViewModel &vm, int slot = 0);

  static constexpr int kSceneTexWidth =
      RenderLayout::kSceneCols * RenderLayout::kTileSize;
  static constexpr int kSceneTexHeight =
      RenderLayout::kSceneRows * RenderLayout::kTileSize;

  static int cell_to_skin(CellColor cc);
  static int piece_to_skin(PieceType type);

  // Underlying SDL renderer — exposed so debug UI can create its own textures
  // (clipboard-image preview) without owning a separate SDL_Renderer handle.
  SDL_Renderer *sdl_renderer() const { return renderer_; }

private:
  void load_skin();

  void draw_play_background();
  void draw_board_border();
  void draw_gridlines();
  void draw_board(const Board &board);
  void draw_piece(const Piece &piece);
  void draw_ghost(const Piece &ghost);
  void draw_mini_piece(PieceType type, bool greyed, float region_col,
                       float region_y_up);
  void draw_plan_overlay(const std::vector<OverlayCell> &cells);
  // Vertical amber bar, drawn inside the scene texture just left of the
  // playfield. Height is proportional to `pending_lines` and clamped to the
  // playfield height so it can never overflow the scene bounds.
  void draw_garbage_meter(int pending_lines);

  void draw_tile(float x, float y, int tile_idx, Color tint = Color::White());
  void draw_solid(const SDL_FRect &dst, Color color);

  SDL_Renderer *renderer_;
  const Settings &settings_;
  SDL_Texture *skin_ = nullptr;
  bool skin_ok_ = false;

  std::array<SDL_FRect, RenderLayout::kSkinTiles> skin_src_;

  // Two render targets so versus mode can hold both boards simultaneously
  // without a per-frame texture recreate. Lazily allocated in slot 1.
  SDL_Texture *scene_tex_[2] = {nullptr, nullptr};
};
