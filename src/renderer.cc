#include "renderer.h"
#include <cstdio>
#include <iostream>

using L = RenderLayout;

// Fallback pastel colors indexed by CellColor enum (0=Empty..8=Garbage).
static constexpr sf::Color kFallbackColors[] = {
    sf::Color::Transparent,   // Empty
    sf::Color(135, 206, 250), // I
    sf::Color(255, 255, 0),   // O
    sf::Color(186, 85, 211),  // T
    sf::Color(50, 205, 50),   // S
    sf::Color(255, 105, 180), // Z
    sf::Color(30, 144, 255),  // J
    sf::Color(255, 165, 0),   // L
    sf::Color(128, 128, 128), // Garbage
};

// CellColor -> skin tile index.
// Skin order: Z=0, L=1, O=2, S=3, I=4, J=5, T=6, ghost=7, garbage=8, ...
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
    return 0; // shouldn't be called
  }
  return 0;
}

int Renderer::piece_to_skin(PieceType type) {
  return cell_to_skin(piece_to_cell_color(type));
}

Renderer::Renderer(sf::RenderWindow &window, const Settings &settings,
                   Stats &stats)
    : window_(window), settings_(settings), stats_(stats),
      tex_verts_(sf::PrimitiveType::Triangles),
      solid_verts_(sf::PrimitiveType::Triangles) {
  board_x_ = L::kBoardPadX * L::kTileSize;
  board_y_ = 1 * L::kTileSize;

  if (!settings_.skin_path.empty()) {
    skin_ok_ = skin_.loadFromFile(settings_.skin_path);
    if (!skin_ok_)
      std::cerr << "Failed to load skin: " << settings_.skin_path << "\n";
    skin_.setSmooth(false);
  }

  if (!board_tex_.resize({static_cast<unsigned>(L::kWindowW),
                          static_cast<unsigned>(L::kWindowH)}))
    std::cerr << "Failed to create board render texture\n";

  if (!settings_.font_path.empty()) {
    font_ok_ = font_.openFromFile(settings_.font_path);
    if (!font_ok_)
      std::cerr << "Failed to load font: " << settings_.font_path << "\n";
  }
}

void Renderer::handle_resize(unsigned int width, unsigned int height) {
  auto view = window_.getView();
  float logicalW = L::kWindowW;
  float logicalH = L::kWindowH;
  float windowW = static_cast<float>(width);
  float windowH = static_cast<float>(height);
  float scale = std::min(windowW / logicalW, windowH / logicalH);
  float viewportW = (logicalW * scale) / windowW;
  float viewportH = (logicalH * scale) / windowH;
  float viewportX = (1.f - viewportW) / 2.f;
  float viewportY = (1.f - viewportH) / 2.f;
  view.setSize({logicalW, logicalH});
  view.setCenter({logicalW / 2.f, logicalH / 2.f});
  view.setViewport({{viewportX, viewportY}, {viewportW, viewportH}});
  window_.setView(view);
}

void Renderer::draw(const GameState &state) {
  render_board_scene(state);
  blit_and_present();
}

void Renderer::draw_stats() { blit_and_present(); }

void Renderer::blit_and_present() {
  window_.clear(sf::Color(20, 20, 20));
  sf::Sprite board_sprite(board_tex_.getTexture());
  window_.draw(board_sprite);
  draw_stats_text(std::chrono::steady_clock::now());
  window_.display();
}

void Renderer::render_board_scene(const GameState &state) {
  board_tex_.clear(sf::Color(20, 20, 20));
  tex_verts_.clear();
  solid_verts_.clear();

  draw_board_border();
  draw_board(state.board);
  draw_ghost(state.ghost_piece);
  draw_piece(state.current_piece);
  draw_hold(state.hold_piece, state.hold_available);
  draw_preview(state.preview);

  if (state.game_over)
    draw_game_over();

  if (tex_verts_.getVertexCount() > 0) {
    sf::RenderStates rs;
    rs.texture = &skin_;
    board_tex_.draw(tex_verts_, rs);
  }
  if (solid_verts_.getVertexCount() > 0)
    board_tex_.draw(solid_verts_);

  board_tex_.display();
}

void Renderer::draw_stats_text(TimePoint now) {
  if (!font_ok_)
    return;

  auto stats = stats_.snapshot(now);

  constexpr unsigned int kFontSize = 20;
  constexpr float kLineH = 22.f;
  const sf::Color kLabelColor(140, 140, 140);
  const sf::Color kValueColor(220, 220, 220);

  float x = 10.f;
  // Below hold piece: hold is at board_y_ + kTileSize, ~3 tiles tall.
  float y = board_y_ + L::kTileSize * 5.f;
  float val_x = x + 60.f; // TODO: tune column offset

  auto draw_line = [&](const char *label, const std::string &value) {
    sf::Text lbl(font_, label, kFontSize);
    lbl.setFillColor(kLabelColor);
    lbl.setPosition({x, y});
    window_.draw(lbl);

    sf::Text val(font_, value, kFontSize);
    val.setFillColor(kValueColor);
    val.setPosition({val_x, y});
    window_.draw(val);

    y += kLineH;
  };

  auto fmt_rate = [](int count, float secs) -> std::string {
    if (secs < 0.001f)
      return "0.0";
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.1f", static_cast<float>(count) / secs);
    return buf;
  };

  draw_line("B2B", std::to_string(stats.b2b));
  draw_line("CMB", std::to_string(stats.combo));
  draw_line("LNS", std::to_string(stats.lines));
  draw_line("ATK", std::to_string(stats.attack));
  draw_line("PC", std::to_string(stats.pcs));

  y += kLineH * 0.5f; // gap before rates

  draw_line("KPS", fmt_rate(stats.inputs, stats.elapsed_s));
  draw_line("PPS", fmt_rate(stats.pieces, stats.elapsed_s));
  draw_line("APS", fmt_rate(stats.attack, stats.elapsed_s));
}

void Renderer::draw_board_border() {
  float bw = Board::kWidth * L::kTileSize;
  float bh = Board::kVisibleHeight * L::kTileSize;
  float x = board_x_;
  float y = board_y_;
  sf::Color c(80, 80, 80);
  push_solid({x - 1, y - 1}, {bw + 2, 1}, c);
  push_solid({x - 1, y + bh}, {bw + 2, 1}, c);
  push_solid({x - 1, y}, {1, bh}, c);
  push_solid({x + bw, y}, {1, bh}, c);
}

void Renderer::draw_board(const Board &board) {
  for (int row = 0; row < Board::kVisibleHeight; ++row) {
    for (int col = 0; col < Board::kWidth; ++col) {
      auto cc = board.cell_color(col, row);
      if (cc != CellColor::Empty) {
        push_tile(grid_to_pixel(col, row), {L::kTileSize, L::kTileSize},
                  cell_to_skin(cc));
      }
    }
  }
}

void Renderer::draw_piece(const Piece &piece) {
  int tile = piece_to_skin(piece.type);
  for (auto &cell : piece.cells_absolute()) {
    if (cell.y < Board::kVisibleHeight)
      push_tile(grid_to_pixel(cell.x, cell.y), {L::kTileSize, L::kTileSize},
                tile);
  }
}

void Renderer::draw_ghost(const Piece &ghost) {
  sf::Color tint(255, 255, 255, settings_.ghost_opacity);
  int tile =
      settings_.colored_ghost ? piece_to_skin(ghost.type) : L::kSkinGhost;
  for (auto &cell : ghost.cells_absolute()) {
    if (cell.y < Board::kVisibleHeight)
      push_tile(grid_to_pixel(cell.x, cell.y), {L::kTileSize, L::kTileSize},
                tile, tint);
  }
}

void Renderer::draw_hold(std::optional<PieceType> hold, bool available) {
  if (!hold)
    return;
  float px = L::kTileSize * 0.5f;
  float py = board_y_ + L::kTileSize;
  int tile = available ? piece_to_skin(*hold) : L::kSkinGreyedHold;
  draw_mini_piece(*hold, px, py, tile);
}

void Renderer::draw_preview(const std::array<PieceType, 6> &preview) {
  float px = board_x_ + Board::kWidth * L::kTileSize + L::kTileSize * 0.5f;
  float py = board_y_ + L::kTileSize;
  for (int i = 0; i < 5; ++i) {
    draw_mini_piece(preview[i], px, py, piece_to_skin(preview[i]));
    py += L::kTileSize * 3;
  }
}

void Renderer::draw_game_over() {
  push_solid({0, 0}, {L::kWindowW, L::kWindowH}, sf::Color(0, 0, 0, 150));
}

void Renderer::draw_mini_piece(PieceType type, float px, float py, int tile) {
  int ti = static_cast<int>(type);
  auto &cells = kPieceCells[ti][static_cast<int>(Rotation::North)];
  float mini = L::kTileSize * 0.7f;
  for (auto &c : cells)
    push_tile({px + c.x * mini, py + (2 - c.y) * mini}, {mini, mini}, tile);
}

sf::Vector2f Renderer::grid_to_pixel(int col, int row) const {
  float px = board_x_ + col * L::kTileSize;
  float py = board_y_ + (Board::kVisibleHeight - 1 - row) * L::kTileSize;
  return {px, py};
}

void Renderer::push_tile(sf::Vector2f pos, sf::Vector2f size, int tile_idx,
                         sf::Color tint) {
  if (!skin_ok_) {
    // Fallback: use CellColor-indexed pastels. Skin tile -> approximate
    // CellColor. Skin: Z=0,L=1,O=2,S=3,I=4,J=5,T=6,ghost=7,garbage=8,...
    static constexpr int kSkinToCellColor[] = {5, 7, 2, 4, 1, 6,
                                               3, 0, 8, 8, 0, 0};
    int cc = kSkinToCellColor[tile_idx];
    auto base = kFallbackColors[cc];
    sf::Color c(base.r, base.g, base.b,
                static_cast<uint8_t>(base.a * tint.a / 255));
    push_solid(pos, size, c);
    return;
  }

  // Skin tiles are 30x30 at 31px pitch (30 + 1px separator).
  float tx0 = static_cast<float>(tile_idx * L::kSkinPitch);
  float tx1 = tx0 + L::kSkinTile;
  float ty0 = 0.f;
  float ty1 = static_cast<float>(L::kSkinTile);

  sf::Vector2f tl = pos;
  sf::Vector2f tr = {pos.x + size.x, pos.y};
  sf::Vector2f bl = {pos.x, pos.y + size.y};
  sf::Vector2f br = {pos.x + size.x, pos.y + size.y};

  tex_verts_.append(sf::Vertex{tl, tint, {tx0, ty0}});
  tex_verts_.append(sf::Vertex{tr, tint, {tx1, ty0}});
  tex_verts_.append(sf::Vertex{br, tint, {tx1, ty1}});
  tex_verts_.append(sf::Vertex{tl, tint, {tx0, ty0}});
  tex_verts_.append(sf::Vertex{br, tint, {tx1, ty1}});
  tex_verts_.append(sf::Vertex{bl, tint, {tx0, ty1}});
}

void Renderer::push_solid(sf::Vector2f pos, sf::Vector2f size,
                          sf::Color color) {
  sf::Vector2f tl = pos;
  sf::Vector2f tr = {pos.x + size.x, pos.y};
  sf::Vector2f bl = {pos.x, pos.y + size.y};
  sf::Vector2f br = {pos.x + size.x, pos.y + size.y};

  solid_verts_.append(sf::Vertex{tl, color});
  solid_verts_.append(sf::Vertex{tr, color});
  solid_verts_.append(sf::Vertex{br, color});
  solid_verts_.append(sf::Vertex{tl, color});
  solid_verts_.append(sf::Vertex{br, color});
  solid_verts_.append(sf::Vertex{bl, color});
}
