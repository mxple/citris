#pragma once

#include "game_state.h"
#include <SFML/Graphics.hpp>

struct RenderLayout {
  static constexpr int kTileSize = 40;
  static constexpr int kBoardPadX = 6;
  static constexpr int kWindowTilesW = Board::kWidth + kBoardPadX * 2;
  static constexpr int kWindowTilesH = Board::kVisibleHeight + 2;
  static constexpr int kWindowW = kWindowTilesW * kTileSize;
  static constexpr int kWindowH = kWindowTilesH * kTileSize;

  // Skin atlas: 372x30, 12 tiles of 30x30 separated by 1px blank columns.
  // Order: Z, L, O, S, I, J, T, ghost, garbage, perm_garbage, greyed_hold, debug
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
  Renderer(sf::RenderWindow &window, const std::string &skin_path);
  void draw(const GameState &state);
  void handle_resize(unsigned int width, unsigned int height);

  // Map CellColor/PieceType to skin tile index.
  static int cell_to_skin(CellColor cc);
  static int piece_to_skin(PieceType type);

private:
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
  sf::Texture skin_;
  bool skin_ok_ = false;
  sf::VertexArray tex_verts_;
  sf::VertexArray solid_verts_;
  float board_x_;
  float board_y_;
};
