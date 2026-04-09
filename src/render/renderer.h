#pragma once

#include "engine/game_state.h"
#include "settings.h"
#include "view_model.h"
#include <SFML/Graphics.hpp>

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
  Renderer(sf::RenderWindow &window, const Settings &settings);

  void draw(const ViewModel &vm);
  void draw_stats(const ViewModel &vm);

  void handle_resize(unsigned int width, unsigned int height);

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
  sf::Vector2f grid_to_pixel(int col, int row) const;

  void push_tile(sf::Vector2f pos, sf::Vector2f size, int tile_idx,
                 sf::Color tint = sf::Color::White);
  void push_solid(sf::Vector2f pos, sf::Vector2f size, sf::Color color);

  sf::RenderWindow &window_;
  const Settings &settings_;
  sf::Texture skin_;
  bool skin_ok_ = false;
  sf::VertexArray tex_verts_;
  sf::VertexArray solid_verts_;
  float board_x_;
  float board_y_;

  sf::RenderTexture board_tex_;
  sf::Font &font_;

  int last_drawn_piece_gen_ = -1;
  LastClear displayed_clear_;
  GameState last_state_;
};

void handle_resize(sf::RenderWindow &window, unsigned w, unsigned h,
                   bool auto_scale = true);
float handle_resize_fill_width(sf::RenderWindow &window, unsigned w,
                               unsigned h, float scale_factor = 1.f);
