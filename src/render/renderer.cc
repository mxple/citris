#include "renderer.h"
#include "font.h"
#include <SDL3_image/SDL_image.h>
#include <cstdio>
#include <iostream>

using L = RenderLayout;

static constexpr Color kFallbackColors[] = {
    Color::Transparent(),  // Empty
    Color(135, 206, 250),  // I
    Color(255, 255, 0),    // O
    Color(186, 85, 211),   // T
    Color(50, 205, 50),    // S
    Color(255, 105, 180),  // Z
    Color(30, 144, 255),   // J
    Color(255, 165, 0),    // L
    Color(128, 128, 128),  // Garbage
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
  board_x_ = L::kBoardPadX * L::kTileSize;
  board_y_ = 2 * L::kTileSize;

  if (!settings_.skin_path.empty()) {
    auto resolved = settings_.resolve(settings_.skin_path);
    skin_ = IMG_LoadTexture(renderer_, resolved.c_str());
    if (!skin_) {
      std::cerr << "Failed to load skin: " << resolved << " (" << SDL_GetError() << ")\n";
    } else {
      skin_ok_ = true;
      SDL_SetTextureScaleMode(skin_, SDL_SCALEMODE_NEAREST);
      SDL_GetTextureSize(skin_, &skin_w_, &skin_h_);
    }
  }

  board_tex_ = SDL_CreateTexture(
      renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
      static_cast<int>(L::kWindowW), static_cast<int>(L::kWindowH));
  if (!board_tex_)
    std::cerr << "Failed to create board render texture: " << SDL_GetError() << "\n";
  else
    SDL_SetTextureScaleMode(board_tex_, SDL_SCALEMODE_NEAREST);

  // Reserve realistic upper bounds once so the hot path never reallocates.
  tex_verts_.reserve(1024);
  tex_indices_.reserve(1536);
  solid_verts_.reserve(256);
  solid_indices_.reserve(384);
  text_verts_.reserve(1024);
  text_indices_.reserve(1536);

  build_templates();
}

void Renderer::build_templates() {
  if (!skin_ok_) {
    templates_ok_ = false;
    return;
  }

  for (int t = 0; t < L::kSkinTiles; ++t) {
    float u0 = static_cast<float>(t * L::kSkinPitch) / skin_w_;
    float u1 =
        (static_cast<float>(t * L::kSkinPitch) + L::kSkinTile) / skin_w_;
    float v0 = 0.f;
    float v1 = static_cast<float>(L::kSkinTile) / skin_h_;
    skin_uv_[t] = {u0, v0, u1, v1};
  }

  auto bake_quad = [](TileVert *dst, float lx, float ly, float size,
                      const std::array<float, 4> &uv) {
    dst[0] = {lx, ly, uv[0], uv[1]};
    dst[1] = {lx + size, ly, uv[2], uv[1]};
    dst[2] = {lx + size, ly + size, uv[2], uv[3]};
    dst[3] = {lx, ly + size, uv[0], uv[3]};
  };

  // Full-size piece templates: local coords relative to the bottom-left
  // cell of the piece bounding box, i.e. (col_off * T, -row_off * T). The
  // render-time origin corresponds to the piece's (x, y) cell.
  for (int pt = 0; pt < 7; ++pt) {
    int skin_tile = piece_to_skin(static_cast<PieceType>(pt));
    const auto &uv = skin_uv_[skin_tile];
    for (int rot = 0; rot < 4; ++rot) {
      const auto &cells = kPieceCells[pt][rot];
      auto &tpl = piece_tpl_full_[pt][rot];
      for (int i = 0; i < 4; ++i) {
        float lx = cells[i].x * L::kTileSize;
        float ly = -cells[i].y * L::kTileSize;
        bake_quad(&tpl.verts[i * 4], lx, ly, L::kTileSize, uv);
      }
    }
  }

  // Mini templates: North rotation only, positions match draw_mini_piece
  // layout (c.x * mini, (2 - c.y) * mini).
  const float mini = L::kTileSize * 0.7f;
  for (int pt = 0; pt < 7; ++pt) {
    int skin_tile = piece_to_skin(static_cast<PieceType>(pt));
    const auto &uv = skin_uv_[skin_tile];
    const auto &cells = kPieceCells[pt][static_cast<int>(Rotation::North)];
    auto &tpl = piece_tpl_mini_[pt];
    for (int i = 0; i < 4; ++i) {
      float lx = cells[i].x * mini;
      float ly = (2 - cells[i].y) * mini;
      bake_quad(&tpl.verts[i * 4], lx, ly, mini, uv);
    }
  }

  templates_ok_ = true;
}

Renderer::~Renderer() {
  if (board_tex_)
    SDL_DestroyTexture(board_tex_);
  if (skin_)
    SDL_DestroyTexture(skin_);
}

void Renderer::handle_resize() {
  ::handle_resize(renderer_, settings_.auto_scale);
}

void Renderer::draw(const ViewModel &vm) {
  // static int drawcnt = 0 ;
  // std::cerr << drawcnt++ << std::endl;
  if (vm.state.last_clear.piece_gen != last_drawn_piece_gen_ &&
      (vm.state.last_clear.lines > 0 ||
       vm.state.last_clear.spin != SpinKind::None)) {
    last_drawn_piece_gen_ = vm.state.last_clear.piece_gen;
    displayed_clear_ = vm.state.last_clear;
  }
  last_state_ = vm.state;

  render_board_scene(vm.state);
  blit_and_present(vm);
}

void Renderer::draw_stats(const ViewModel &vm) {
  last_state_ = vm.state;
  blit_and_present(vm);
}

void Renderer::blit_and_present(const ViewModel &vm) {
  // Blit the board texture to the window. It covers the full logical
  // window, so no preceding clear is needed.
  SDL_RenderTexture(renderer_, board_tex_, nullptr, nullptr);

  float sidebar_y = board_y_ + L::kTileSize * 5.f;
  sidebar_y = draw_stats_text(vm.stats, sidebar_y);
  sidebar_y = draw_action_text(last_state_, sidebar_y);

  if (vm.hud)
    draw_hud(*vm.hud, vm.state);

  flush_text_batch();
  SDL_RenderPresent(renderer_);
}

void Renderer::render_board_scene(const GameState &state) {
  SDL_SetRenderTarget(renderer_, board_tex_);
  SDL_SetRenderDrawColor(renderer_, 20, 20, 20, 255);
  SDL_RenderClear(renderer_);

  tex_verts_.clear();
  tex_indices_.clear();
  solid_verts_.clear();
  solid_indices_.clear();

  draw_board_border();
  draw_gridlines();
  draw_board(state.board);
  draw_ghost(state.ghost_piece);
  draw_piece(state.current_piece);
  draw_hold(state.hold_piece, state.hold_available);
  draw_preview(state.preview);

  if (state.game_over)
    draw_game_over();

  if (!tex_verts_.empty()) {
    SDL_RenderGeometryRaw(
        renderer_, skin_, &tex_verts_[0].position.x, sizeof(SDL_Vertex),
        &tex_verts_[0].color, sizeof(SDL_Vertex),
        &tex_verts_[0].tex_coord.x, sizeof(SDL_Vertex),
        static_cast<int>(tex_verts_.size()), tex_indices_.data(),
        static_cast<int>(tex_indices_.size()),
        static_cast<int>(sizeof(uint16_t)));
  }
  if (!solid_verts_.empty()) {
    SDL_RenderGeometryRaw(
        renderer_, nullptr, &solid_verts_[0].position.x, sizeof(SDL_Vertex),
        &solid_verts_[0].color, sizeof(SDL_Vertex),
        &solid_verts_[0].tex_coord.x, sizeof(SDL_Vertex),
        static_cast<int>(solid_verts_.size()), solid_indices_.data(),
        static_cast<int>(solid_indices_.size()),
        static_cast<int>(sizeof(uint16_t)));
  }

  SDL_SetRenderTarget(renderer_, nullptr);
}

float Renderer::draw_stats_text(const Stats::Snapshot &stats, float y) {
  const float kLineH = L::kFontL + 2.f;
  const Color kLabelColor(140, 140, 140);
  const Color kValueColor(220, 220, 220);

  float x = 10.f;
  float val_x = x + 60.f * L::kScale;

  auto draw_line = [&](const char *label, const std::string &value) {
    draw_text(label, L::kFontL, x, y, kLabelColor);
    draw_text_atlas(value.c_str(), L::kFontL, val_x, y, kValueColor);
    y += kLineH;
  };

  auto fmt_rate = [](int count, float secs) -> std::string {
    if (secs < 0.001f)
      return "0.00";
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.2f", static_cast<float>(count) / secs);
    return buf;
  };

  auto fmt_time = [](float secs) -> std::string {
    int total_ms = static_cast<int>(secs * 1000);
    int mins = total_ms / 60000;
    int s = (total_ms % 60000) / 1000;
    int hundredths = (total_ms % 1000) / 10;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d.%02d", mins, s, hundredths);
    return buf;
  };

  draw_line("TIME", fmt_time(stats.elapsed_s));
  draw_line("B2B", std::to_string(stats.b2b));
  draw_line("CMB", std::to_string(stats.combo));
  draw_line("LNS", std::to_string(stats.lines));
  draw_line("ATK", std::to_string(stats.attack));
  draw_line("PC", std::to_string(stats.pcs));

  y += kLineH * 0.5f;

  draw_line("KPS", fmt_rate(stats.inputs, stats.elapsed_s));
  draw_line("PPS", fmt_rate(stats.pieces, stats.elapsed_s));
  draw_line("APS", fmt_rate(stats.attack, stats.elapsed_s));

  return y;
}

float Renderer::draw_action_text(const GameState &state, float y) {
  if (displayed_clear_.lines == 0)
    return y;

  std::string label;
  switch (displayed_clear_.spin) {
  case SpinKind::TSpin:
    label = "tspin ";
    break;
  case SpinKind::Mini:
    label = "tspin mini ";
    break;
  case SpinKind::AllSpin:
    label = "allspin ";
    break;
  case SpinKind::None:
    break;
  }

  static constexpr const char *kLineNames[] = {"", "single", "double", "triple",
                                                "quad"};
  int idx = std::clamp(displayed_clear_.lines, 0, 4);
  label += kLineNames[idx];

  const float kLineH = L::kFontL + 2.f;
  float x = 10.f;
  y += kLineH * 0.5f;

  draw_text(label.c_str(), L::kFontL, x, y, Color(255, 255, 100));
  y += kLineH;

  if (displayed_clear_.perfect_clear) {
    draw_text("perfect clear", L::kFontL, x, y, Color(255, 200, 50));
    y += kLineH;
  }

  return y;
}

void Renderer::draw_hud(const HudData &hud, const GameState &state) {
  float cx = board_x_ + 5.f * L::kTileSize;

  if (!hud.center_text.empty()) {
    float bottom = board_y_ + 20.f * L::kTileSize + 5.f;
    draw_text_centered_x(hud.center_text.c_str(), L::kFontXL, cx, bottom,
                          hud.center_color);
  }

  if (state.game_over && !hud.game_over_label.empty()) {
    float cy = board_y_ + 10.f * L::kTileSize;

    draw_text_centered_x(hud.game_over_label.c_str(), L::kFontXL, cx,
                          cy - 50.f * L::kScale, hud.game_over_label_color);

    if (!hud.game_over_detail.empty()) {
      unsigned detail_size =
          static_cast<unsigned>(hud.game_over_detail_size * L::kScale);
      draw_text_centered_x(hud.game_over_detail.c_str(), detail_size, cx, cy,
                            hud.game_over_detail_color);
    }
  }
}

void Renderer::draw_board_border() {
  float bw = Board::kWidth * L::kTileSize;
  float bh = Board::kVisibleHeight * L::kTileSize;
  float x = board_x_;
  float y = board_y_;
  Color c(80, 80, 80);
  push_solid({x - 1, y - 1}, {bw + 2, 1}, c);
  push_solid({x - 1, y + bh}, {bw + 2, 1}, c);
  push_solid({x - 1, y}, {1, bh}, c);
  push_solid({x + bw, y}, {1, bh}, c);
}

void Renderer::draw_gridlines() {
  if (settings_.grid_opacity == 0)
    return;
  Color c(255, 255, 255, settings_.grid_opacity);
  float bx = board_x_;
  float by = board_y_;
  float bw = Board::kWidth * L::kTileSize;
  float bh = Board::kVisibleHeight * L::kTileSize;

  for (int col = 1; col < Board::kWidth; ++col) {
    float x = bx + col * L::kTileSize;
    push_solid({x, by}, {1, bh}, c);
  }
  for (int row = 1; row < Board::kVisibleHeight; ++row) {
    float y = by + row * L::kTileSize;
    push_solid({bx, y}, {bw, 1}, c);
  }
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
  if (!templates_ok_) {
    int tile = piece_to_skin(piece.type);
    for (auto &cell : piece.cells_absolute()) {
      push_tile(grid_to_pixel(cell.x, cell.y), {L::kTileSize, L::kTileSize},
                tile);
    }
    return;
  }

  int pt = static_cast<int>(piece.type);
  int rot = static_cast<int>(piece.rotation);
  const auto &tpl = piece_tpl_full_[pt][rot];
  float origin_x = board_x_ + piece.x * L::kTileSize;
  float origin_y =
      board_y_ + (Board::kVisibleHeight - 1 - piece.y) * L::kTileSize;

  SDL_FColor white = {1.f, 1.f, 1.f, 1.f};
  uint16_t base = static_cast<uint16_t>(tex_verts_.size());
  for (int i = 0; i < 16; ++i) {
    const auto &tv = tpl.verts[i];
    tex_verts_.push_back(
        {{origin_x + tv.lx, origin_y + tv.ly}, white, {tv.u, tv.v}});
  }
  for (int q = 0; q < 4; ++q) {
    uint16_t b = base + q * 4;
    tex_indices_.push_back(b);
    tex_indices_.push_back(b + 1);
    tex_indices_.push_back(b + 2);
    tex_indices_.push_back(b);
    tex_indices_.push_back(b + 2);
    tex_indices_.push_back(b + 3);
  }
}

void Renderer::draw_ghost(const Piece &ghost) {
  Color tint(255, 255, 255, settings_.ghost_opacity);
  int tile =
      settings_.colored_ghost ? piece_to_skin(ghost.type) : L::kSkinGhost;

  if (!templates_ok_) {
    for (auto &cell : ghost.cells_absolute()) {
      if (cell.y < Board::kVisibleHeight)
        push_tile(grid_to_pixel(cell.x, cell.y),
                  {L::kTileSize, L::kTileSize}, tile, tint);
    }
    return;
  }

  int pt = static_cast<int>(ghost.type);
  int rot = static_cast<int>(ghost.rotation);
  const auto &tpl = piece_tpl_full_[pt][rot];
  const auto &uv = skin_uv_[tile];
  auto cells = ghost.cells_absolute();
  float origin_x = board_x_ + ghost.x * L::kTileSize;
  float origin_y =
      board_y_ + (Board::kVisibleHeight - 1 - ghost.y) * L::kTileSize;
  SDL_FColor sc = tint.to_sdl();
  float S = L::kTileSize;

  for (int i = 0; i < 4; ++i) {
    if (cells[i].y >= Board::kVisibleHeight)
      continue;
    float lx = tpl.verts[i * 4].lx;
    float ly = tpl.verts[i * 4].ly;
    uint16_t base = static_cast<uint16_t>(tex_verts_.size());
    tex_verts_.push_back({{origin_x + lx, origin_y + ly}, sc, {uv[0], uv[1]}});
    tex_verts_.push_back(
        {{origin_x + lx + S, origin_y + ly}, sc, {uv[2], uv[1]}});
    tex_verts_.push_back(
        {{origin_x + lx + S, origin_y + ly + S}, sc, {uv[2], uv[3]}});
    tex_verts_.push_back(
        {{origin_x + lx, origin_y + ly + S}, sc, {uv[0], uv[3]}});
    tex_indices_.push_back(base);
    tex_indices_.push_back(base + 1);
    tex_indices_.push_back(base + 2);
    tex_indices_.push_back(base);
    tex_indices_.push_back(base + 2);
    tex_indices_.push_back(base + 3);
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
  push_solid({0, 0}, {L::kWindowW, L::kWindowH}, Color(0, 0, 0, 150));
}

void Renderer::draw_mini_piece(PieceType type, float px, float py, int tile) {
  if (!templates_ok_) {
    int ti = static_cast<int>(type);
    auto &cells = kPieceCells[ti][static_cast<int>(Rotation::North)];
    float mini = L::kTileSize * 0.7f;
    for (auto &c : cells)
      push_tile({px + c.x * mini, py + (2 - c.y) * mini}, {mini, mini}, tile);
    return;
  }

  // Template has UVs baked for the piece's own tile. When the caller
  // requests a different tile (e.g. greyed-out hold), override UVs.
  int pt = static_cast<int>(type);
  int own_tile = piece_to_skin(type);
  const auto &tpl = piece_tpl_mini_[pt];
  const auto &uv = skin_uv_[tile];
  bool uv_override = (tile != own_tile);
  SDL_FColor white = {1.f, 1.f, 1.f, 1.f};
  float mini = L::kTileSize * 0.7f;

  for (int i = 0; i < 4; ++i) {
    float lx = tpl.verts[i * 4].lx;
    float ly = tpl.verts[i * 4].ly;
    uint16_t base = static_cast<uint16_t>(tex_verts_.size());
    if (uv_override) {
      tex_verts_.push_back({{px + lx, py + ly}, white, {uv[0], uv[1]}});
      tex_verts_.push_back(
          {{px + lx + mini, py + ly}, white, {uv[2], uv[1]}});
      tex_verts_.push_back(
          {{px + lx + mini, py + ly + mini}, white, {uv[2], uv[3]}});
      tex_verts_.push_back(
          {{px + lx, py + ly + mini}, white, {uv[0], uv[3]}});
    } else {
      for (int k = 0; k < 4; ++k) {
        const auto &tv = tpl.verts[i * 4 + k];
        tex_verts_.push_back({{px + tv.lx, py + tv.ly}, white, {tv.u, tv.v}});
      }
    }
    tex_indices_.push_back(base);
    tex_indices_.push_back(base + 1);
    tex_indices_.push_back(base + 2);
    tex_indices_.push_back(base);
    tex_indices_.push_back(base + 2);
    tex_indices_.push_back(base + 3);
  }
}

Vec2f Renderer::grid_to_pixel(int col, int row) const {
  float px = board_x_ + col * L::kTileSize;
  float py = board_y_ + (Board::kVisibleHeight - 1 - row) * L::kTileSize;
  return {px, py};
}

void Renderer::push_tile(Vec2f pos, Vec2f size, int tile_idx, Color tint) {
  if (!skin_ok_) {
    static constexpr int kSkinToCellColor[] = {5, 7, 2, 4, 1, 6,
                                               3, 0, 8, 8, 0, 0};
    int cc = kSkinToCellColor[tile_idx];
    auto base = kFallbackColors[cc];
    Color c(base.r, base.g, base.b,
            static_cast<uint8_t>(base.a * tint.a / 255));
    push_solid(pos, size, c);
    return;
  }

  const auto &uv = skin_uv_[tile_idx];
  float tx0 = uv[0], ty0 = uv[1], tx1 = uv[2], ty1 = uv[3];

  SDL_FColor sc = tint.to_sdl();
  uint16_t base = static_cast<uint16_t>(tex_verts_.size());

  tex_verts_.push_back({{pos.x, pos.y}, sc, {tx0, ty0}});
  tex_verts_.push_back({{pos.x + size.x, pos.y}, sc, {tx1, ty0}});
  tex_verts_.push_back({{pos.x + size.x, pos.y + size.y}, sc, {tx1, ty1}});
  tex_verts_.push_back({{pos.x, pos.y + size.y}, sc, {tx0, ty1}});

  tex_indices_.push_back(base);
  tex_indices_.push_back(base + 1);
  tex_indices_.push_back(base + 2);
  tex_indices_.push_back(base);
  tex_indices_.push_back(base + 2);
  tex_indices_.push_back(base + 3);
}

void Renderer::push_solid(Vec2f pos, Vec2f size, Color color) {
  SDL_FColor sc = color.to_sdl();
  uint16_t base = static_cast<uint16_t>(solid_verts_.size());

  solid_verts_.push_back({{pos.x, pos.y}, sc, {0, 0}});
  solid_verts_.push_back({{pos.x + size.x, pos.y}, sc, {0, 0}});
  solid_verts_.push_back({{pos.x + size.x, pos.y + size.y}, sc, {0, 0}});
  solid_verts_.push_back({{pos.x, pos.y + size.y}, sc, {0, 0}});

  solid_indices_.push_back(base);
  solid_indices_.push_back(base + 1);
  solid_indices_.push_back(base + 2);
  solid_indices_.push_back(base);
  solid_indices_.push_back(base + 2);
  solid_indices_.push_back(base + 3);
}

void Renderer::draw_text_atlas(const char *str, unsigned font_size, float x,
                               float y, Color color) {
  if (!str || !str[0])
    return;
  const GlyphAtlas *atlas = get_glyph_atlas(renderer_, font_size, color);
  if (!atlas || !atlas->tex)
    return;

  if (current_text_atlas_ && current_text_atlas_ != atlas)
    flush_text_batch();
  current_text_atlas_ = atlas;

  SDL_FColor white = {1.f, 1.f, 1.f, 1.f};
  float pen = x;
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(str);
       *p; ++p) {
    unsigned char c = *p;
    if (c < 32 || c >= 127) {
      pen += atlas->cell_w;
      continue;
    }
    const auto &g = atlas->glyphs[c];
    float w = g.w > 0 ? g.w : atlas->cell_w;
    float h = g.h > 0 ? g.h : atlas->cell_h;
    uint16_t base = static_cast<uint16_t>(text_verts_.size());
    text_verts_.push_back({{pen, y}, white, {g.u0, g.v0}});
    text_verts_.push_back({{pen + w, y}, white, {g.u1, g.v0}});
    text_verts_.push_back({{pen + w, y + h}, white, {g.u1, g.v1}});
    text_verts_.push_back({{pen, y + h}, white, {g.u0, g.v1}});
    text_indices_.push_back(base);
    text_indices_.push_back(base + 1);
    text_indices_.push_back(base + 2);
    text_indices_.push_back(base);
    text_indices_.push_back(base + 2);
    text_indices_.push_back(base + 3);
    pen += atlas->cell_w;
  }
}

void Renderer::flush_text_batch() {
  if (text_verts_.empty() || !current_text_atlas_) {
    text_verts_.clear();
    text_indices_.clear();
    current_text_atlas_ = nullptr;
    return;
  }
  SDL_RenderGeometryRaw(
      renderer_, current_text_atlas_->tex, &text_verts_[0].position.x,
      sizeof(SDL_Vertex), &text_verts_[0].color, sizeof(SDL_Vertex),
      &text_verts_[0].tex_coord.x, sizeof(SDL_Vertex),
      static_cast<int>(text_verts_.size()), text_indices_.data(),
      static_cast<int>(text_indices_.size()),
      static_cast<int>(sizeof(uint16_t)));
  text_verts_.clear();
  text_indices_.clear();
  current_text_atlas_ = nullptr;
}

void Renderer::draw_text(const char *str, unsigned font_size, float x, float y,
                          Color color) {
  const CachedText *t = get_text(renderer_, str, font_size, color);
  if (!t)
    return;
  SDL_FRect dst = {x, y, t->w, t->h};
  SDL_RenderTexture(renderer_, t->tex, nullptr, &dst);
}

void Renderer::draw_text_centered_x(const char *str, unsigned font_size,
                                      float cx, float y, Color color,
                                      float *out_w) {
  const CachedText *t = get_text(renderer_, str, font_size, color);
  if (!t)
    return;
  SDL_FRect dst = {cx - t->w / 2.f, y, t->w, t->h};
  SDL_RenderTexture(renderer_, t->tex, nullptr, &dst);
  if (out_w)
    *out_w = t->w;
}

void handle_resize(SDL_Renderer *renderer, bool auto_scale) {
  if (auto_scale)
    SDL_SetRenderLogicalPresentation(renderer, static_cast<int>(L::kWindowW),
                                     static_cast<int>(L::kWindowH),
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
  else
    SDL_SetRenderLogicalPresentation(renderer, static_cast<int>(L::kWindowW),
                                     static_cast<int>(L::kWindowH),
                                     SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
}

