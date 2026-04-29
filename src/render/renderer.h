#pragma once

#include "engine/game_state.h"
#include "engine/piece.h"
#include "render/layout.h"
#include "settings.h"
#include "view_model.h"
#include <SDL3/SDL.h>
#include <array>

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
