#pragma once

#include "game_state.h"
#include "settings.h"
#include "stats.h"
#include <SFML/Graphics.hpp>

struct RenderLayout {
  static constexpr int kTileSize = 40;
  static constexpr int kBoardPadX = 6;
  static constexpr int kWindowTilesW = Board::kWidth + kBoardPadX * 2;
  static constexpr int kWindowTilesH = Board::kVisibleHeight + 2;
  static constexpr int kWindowW = kWindowTilesW * kTileSize;
  static constexpr int kWindowH = kWindowTilesH * kTileSize;

  // Skin atlas: 372x30, 12 tiles of 30x30 separated by 1px blank columns.
  // Order: Z, L, O, S, I, J, T, ghost, garbage, perm_garbage, greyed_hold,
  // debug
  static constexpr int kSkinTile = 30;
  static constexpr int kSkinPitch = 31; // tile + 1px separator
  static constexpr int kSkinTiles = 12;

  // Skin tile indices.
  static constexpr int kSkinGhost = 7;
  static constexpr int kSkinGarbage = 8;
  static constexpr int kSkinGreyedHold = 10;
};

class Renderer {
public:
  Renderer(sf::RenderWindow &window, const Settings &settings, Stats &stats);

  // Full redraw: re-render board scene + stats + present.
  void draw(const GameState &state);
  // Stats-only redraw: blit cached board + fresh stats + present.
  void draw_stats();

  void handle_resize(unsigned int width, unsigned int height);

  // Map CellColor/PieceType to skin tile index.
  static int cell_to_skin(CellColor cc);
  static int piece_to_skin(PieceType type);

private:
  // Render board scene (board + hold + preview + border) to board_tex_.
  void render_board_scene(const GameState &state);

  // Clear window, blit cached board_tex_, draw stats text, display.
  void blit_and_present();

  // Draw stats text overlay to window_.
  void draw_stats_text(TimePoint now);

  void draw_board_border();
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
  Stats &stats_;
  sf::Texture skin_;
  bool skin_ok_ = false;
  sf::VertexArray tex_verts_;
  sf::VertexArray solid_verts_;
  float board_x_;
  float board_y_;

  // Cached board scene — only re-rendered when game state changes.
  sf::RenderTexture board_tex_;

  // Stats text rendering.
  sf::Font font_;
  bool font_ok_ = false;
};
