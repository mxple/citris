#include "renderer.h"

Renderer::Renderer(sf::RenderWindow &window)
    : window_(window), vertices_(sf::PrimitiveType::Triangles) {
  board_x_ = kBoardPadX * kTileSize;
  board_y_ = 1 * kTileSize;
}

void Renderer::draw(const GameState &state) {
  window_.clear(sf::Color(20, 20, 20));
  vertices_.clear();

  draw_board_border();
  draw_board(state.board);
  draw_ghost(state.ghost_piece);
  draw_piece(state.current_piece, piece_color(state.current_piece.type));
  draw_hold(state.hold_piece, state.hold_available);
  draw_preview(state.preview);

  if (state.game_over)
    draw_game_over();

  flush();
  window_.display();
}

void Renderer::draw_board_border() {
  // Border as 4 thin quads (top, bottom, left, right).
  float bw = Board::kWidth * kTileSize;
  float bh = Board::kVisibleHeight * kTileSize;
  float x = board_x_;
  float y = board_y_;
  sf::Color c(80, 80, 80);
  push_quad({x - 1, y - 1}, {bw + 2, 1}, c);  // top
  push_quad({x - 1, y + bh}, {bw + 2, 1}, c); // bottom
  push_quad({x - 1, y}, {1, bh}, c);          // left
  push_quad({x + bw, y}, {1, bh}, c);         // right
}

void Renderer::draw_board(const Board &board) {
  for (int row = 0; row < Board::kVisibleHeight; ++row) {
    for (int col = 0; col < Board::kWidth; ++col) {
      if (board.filled(col, row)) {
        auto pos = grid_to_pixel(col, row);
        push_quad(pos, {kTileSize - 1, kTileSize - 1},
                  piece_color(board.cell_color(col, row)));
      }
    }
  }
}

void Renderer::draw_piece(const Piece &piece, sf::Color color) {
  for (auto &cell : piece.cells_absolute()) {
    if (cell.y < Board::kVisibleHeight) {
      push_quad(grid_to_pixel(cell.x, cell.y), {kTileSize - 1, kTileSize - 1},
                color);
    }
  }
}

void Renderer::draw_ghost(const Piece &ghost) {
  auto color = piece_color(ghost.type);
  color.a = 60;
  for (auto &cell : ghost.cells_absolute()) {
    if (cell.y < Board::kVisibleHeight) {
      push_quad(grid_to_pixel(cell.x, cell.y), {kTileSize - 1, kTileSize - 1},
                color);
    }
  }
}

void Renderer::draw_hold(std::optional<PieceType> hold, bool available) {
  if (!hold)
    return;

  auto color = piece_color(*hold);
  if (!available)
    color = sf::Color(80, 80, 80);

  float px = kTileSize * 0.5f;
  float py = board_y_ + kTileSize;
  draw_mini_piece(*hold, px, py, color);
}

void Renderer::draw_preview(const std::array<PieceType, 6> &preview) {
  float px = board_x_ + Board::kWidth * kTileSize + kTileSize * 0.5f;
  float py = board_y_ + kTileSize;

  for (int i = 0; i < 5; ++i) {
    draw_mini_piece(preview[i], px, py, piece_color(preview[i]));
    py += kTileSize * 3;
  }
}

void Renderer::draw_game_over() {
  push_quad({0, 0}, {kWindowW, kWindowH}, sf::Color(0, 0, 0, 150));
}

void Renderer::draw_mini_piece(PieceType type, float px, float py,
                               sf::Color color) {
  int ti = static_cast<int>(type);
  auto &cells = kPieceCells[ti][static_cast<int>(Rotation::North)];

  float mini = kTileSize * 0.7f;
  for (auto &c : cells) {
    push_quad({px + c.x * mini, py + (2 - c.y) * mini}, {mini - 1, mini - 1},
              color);
  }
}

sf::Color Renderer::piece_color(PieceType type) const {
  switch (type) {
  case PieceType::I:
    return sf::Color(135, 206, 250);
  case PieceType::O:
    return sf::Color(255, 255, 0);
  case PieceType::T:
    return sf::Color(186, 85, 211);
  case PieceType::S:
    return sf::Color(50, 205, 50);
  case PieceType::Z:
    return sf::Color(255, 105, 180);
  case PieceType::J:
    return sf::Color(30, 144, 255);
  case PieceType::L:
    return sf::Color(255, 165, 0);
  }
  return sf::Color::White;
}

sf::Vector2f Renderer::grid_to_pixel(int col, int row) const {
  float px = board_x_ + col * kTileSize;
  float py = board_y_ + (Board::kVisibleHeight - 1 - row) * kTileSize;
  return {px, py};
}

void Renderer::push_quad(sf::Vector2f pos, sf::Vector2f size, sf::Color color) {
  // Two triangles forming a rectangle.
  sf::Vector2f tl = pos;
  sf::Vector2f tr = {pos.x + size.x, pos.y};
  sf::Vector2f bl = {pos.x, pos.y + size.y};
  sf::Vector2f br = {pos.x + size.x, pos.y + size.y};

  vertices_.append(sf::Vertex{tl, color});
  vertices_.append(sf::Vertex{tr, color});
  vertices_.append(sf::Vertex{br, color});

  vertices_.append(sf::Vertex{tl, color});
  vertices_.append(sf::Vertex{br, color});
  vertices_.append(sf::Vertex{bl, color});
}

void Renderer::flush() {
  if (vertices_.getVertexCount() > 0)
    window_.draw(vertices_);
}
