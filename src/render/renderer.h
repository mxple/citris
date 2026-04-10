#pragma once

#include "engine/game_state.h"
#include "engine/piece.h"
#include "sdl_types.h"
#include "settings.h"
#include "vec2.h"
#include "view_model.h"
#include <SDL3/SDL.h>
#include <array>
#include <cstdint>
#include <vector>

struct RenderLayout {
  // Base constants (compile-time, tile counts)
  static constexpr int kBoardPadX = 5;
  static constexpr int kWindowTilesW = Board::kWidth + kBoardPadX * 2;
  static constexpr int kWindowTilesH = Board::kVisibleHeight + 4;

  // Scaled values (set by init())
  static inline float kTileSize = 40.f;
  static inline float kWindowW = kWindowTilesW * 40.f;
  static inline float kWindowH = kWindowTilesH * 40.f;
  static inline float kScale = 1.f;

  // Base (unscaled) text sizes — use in viewports that handle their own scaling
  static constexpr unsigned kBaseFontS = 12;
  static constexpr unsigned kBaseFontM = 18;
  static constexpr unsigned kBaseFontL = 22;
  static constexpr unsigned kBaseFontXL = 28;
  static constexpr unsigned kBaseFontXXL = 48;

  // Scaled text sizes (set by init())
  static inline unsigned kFontS = kBaseFontS;
  static inline unsigned kFontM = kBaseFontM;
  static inline unsigned kFontL = kBaseFontL;
  static inline unsigned kFontXL = kBaseFontXL;
  static inline unsigned kFontXXL = kBaseFontXXL;

  static void init(float scale) {
    kScale = scale;
    kTileSize = 40.f * scale;
    kWindowW = kWindowTilesW * kTileSize;
    kWindowH = kWindowTilesH * kTileSize;
    kFontS = static_cast<unsigned>(kBaseFontS * scale);
    kFontM = static_cast<unsigned>(kBaseFontM * scale);
    kFontL = static_cast<unsigned>(kBaseFontL * scale);
    kFontXL = static_cast<unsigned>(kBaseFontXL * scale);
    kFontXXL = static_cast<unsigned>(kBaseFontXXL * scale);
  }

  // Skin texture coords — NOT scaled (pixel coords in skin.png)
  static constexpr int kSkinTile = 30;
  static constexpr int kSkinPitch = 31;
  static constexpr int kSkinTiles = 12;

  static constexpr int kSkinGhost = 7;
  static constexpr int kSkinGarbage = 8;
  static constexpr int kSkinGreyedHold = 10;
};

class Renderer {
public:
  Renderer(SDL_Renderer *renderer, const Settings &settings);
  ~Renderer();

  void draw(const ViewModel &vm);
  void draw_stats(const ViewModel &vm);

  void handle_resize();

  static int cell_to_skin(CellColor cc);
  static int piece_to_skin(PieceType type);

private:
  void render_board_scene(const GameState &state);
  void blit_and_present(const ViewModel &vm);
  float draw_stats_text(const Stats::Snapshot &stats, float y);
  float draw_action_text(const GameState &state, float y);
  void draw_hud(const HudData &hud, const GameState &state);

  void draw_board_border();
  void draw_gridlines();
  void draw_board(const Board &board);
  void draw_piece(const Piece &piece);
  void draw_ghost(const Piece &ghost);
  void draw_hold(std::optional<PieceType> hold, bool available);
  void draw_preview(const std::array<PieceType, 6> &preview);
  void draw_game_over();
  void draw_mini_piece(PieceType type, float px, float py, int tile);
  Vec2f grid_to_pixel(int col, int row) const;

  void push_tile(Vec2f pos, Vec2f size, int tile_idx,
                 Color tint = Color::White());
  void push_solid(Vec2f pos, Vec2f size, Color color);

  void draw_text(const char *str, unsigned font_size, float x, float y,
                 Color color);
  void draw_text_centered_x(const char *str, unsigned font_size, float cx,
                             float y, Color color, float *out_w = nullptr);

  // Dynamic text via glyph atlas. Vertices accumulate in text_verts_ /
  // text_indices_ and are flushed once per present in blit_and_present.
  void draw_text_atlas(const char *str, unsigned font_size, float x, float y,
                       Color color);
  void flush_text_batch();

  void build_templates();

  SDL_Renderer *renderer_;
  const Settings &settings_;
  SDL_Texture *skin_ = nullptr;
  float skin_w_ = 0.f, skin_h_ = 0.f;
  bool skin_ok_ = false;

  std::vector<SDL_Vertex> tex_verts_;
  std::vector<uint16_t> tex_indices_;
  std::vector<SDL_Vertex> solid_verts_;
  std::vector<uint16_t> solid_indices_;
  std::vector<SDL_Vertex> text_verts_;
  std::vector<uint16_t> text_indices_;
  const struct GlyphAtlas *current_text_atlas_ = nullptr;

  // Pre-baked geometry templates, populated once in the constructor after
  // the skin texture has been loaded. Each template stores 4 cells × 4
  // verts in local (cell-offset) pixel coordinates with skin UVs baked in.
  struct TileVert {
    float lx, ly;
    float u, v;
  };
  struct PieceTemplate {
    TileVert verts[16];
  };
  std::array<std::array<PieceTemplate, 4>, 7> piece_tpl_full_;
  std::array<PieceTemplate, 7> piece_tpl_mini_;
  std::array<std::array<float, 4>, RenderLayout::kSkinTiles> skin_uv_;
  bool templates_ok_ = false;

  float board_x_;
  float board_y_;

  SDL_Texture *board_tex_ = nullptr;

  int last_drawn_piece_gen_ = -1;
  LastClear displayed_clear_;
  GameState last_state_;
};

void handle_resize(SDL_Renderer *renderer, bool auto_scale = true);
