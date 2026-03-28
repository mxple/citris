#pragma once

#include "game_state.h"
#include <SFML/Graphics.hpp>

class Renderer {
public:
  static constexpr int kTileSize = 40;
  static constexpr int kBoardPadX = 6;
  static constexpr int kWindowTilesW = Board::kWidth + kBoardPadX * 2;
  static constexpr int kWindowTilesH = Board::kVisibleHeight + 2;
  static constexpr int kWindowW = kWindowTilesW * kTileSize;
  static constexpr int kWindowH = kWindowTilesH * kTileSize;

  explicit Renderer(sf::RenderWindow &window);

  void draw(const GameState &state);

  sf::Color piece_color(PieceType type) const;

private:
  void draw_board_border();
  void draw_board(const Board &board);
  void draw_piece(const Piece &piece, sf::Color color);
  void draw_ghost(const Piece &ghost);
  void draw_hold(std::optional<PieceType> hold, bool available);
  void draw_preview(const std::array<PieceType, 6> &preview);
  void draw_game_over();
  void draw_mini_piece(PieceType type, float px, float py, sf::Color color);

  sf::Vector2f grid_to_pixel(int col, int row) const;

  // Append a quad (2 triangles, 6 vertices) to the vertex batch.
  void push_quad(sf::Vector2f pos, sf::Vector2f size, sf::Color color);

  // Flush the accumulated vertex batch in one draw call.
  void flush();

  sf::RenderWindow &window_;
  sf::VertexArray vertices_;

  float board_x_;
  float board_y_;
};
