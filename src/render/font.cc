#include "font.h"
#include "FreeMono_otf.h"
#include <iostream>
#include <string>
#include <unordered_map>

static std::unordered_map<unsigned, TTF_Font *> &font_cache() {
  static std::unordered_map<unsigned, TTF_Font *> cache;
  return cache;
}

TTF_Font *get_font(unsigned size) {
  auto &cache = font_cache();
  auto it = cache.find(size);
  if (it != cache.end())
    return it->second;

  SDL_IOStream *io =
      SDL_IOFromMem(const_cast<unsigned char *>(FreeMono_otf), FreeMono_otf_len);
  if (!io) {
    std::cerr << "Failed to create IOStream for font: " << SDL_GetError()
              << "\n";
    return nullptr;
  }

  TTF_Font *font = TTF_OpenFontIO(io, true, static_cast<float>(size));
  if (!font) {
    std::cerr << "Failed to open font at size " << size << ": "
              << SDL_GetError() << "\n";
    return nullptr;
  }

  cache[size] = font;
  return font;
}

struct TextKey {
  std::string str;
  unsigned size;
  uint32_t color;
  bool operator==(const TextKey &o) const {
    return size == o.size && color == o.color && str == o.str;
  }
};

struct TextKeyHash {
  size_t operator()(const TextKey &k) const {
    size_t h = std::hash<std::string>{}(k.str);
    h ^= std::hash<unsigned>{}(k.size) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<uint32_t>{}(k.color) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

static std::unordered_map<TextKey, CachedText, TextKeyHash> &text_cache() {
  static std::unordered_map<TextKey, CachedText, TextKeyHash> cache;
  return cache;
}

const CachedText *get_text(SDL_Renderer *renderer, const char *str,
                           unsigned size, Color color) {
  if (!str || !str[0])
    return nullptr;
  uint32_t col = (uint32_t(color.r) << 24) | (uint32_t(color.g) << 16) |
                 (uint32_t(color.b) << 8) | uint32_t(color.a);
  TextKey key{str, size, col};
  auto &cache = text_cache();
  auto it = cache.find(key);
  if (it != cache.end())
    return &it->second;

  TTF_Font *f = get_font(size);
  if (!f)
    return nullptr;
  SDL_Color sc = {color.r, color.g, color.b, color.a};
  SDL_Surface *surf = TTF_RenderText_Blended(f, str, 0, sc);
  if (!surf)
    return nullptr;
  SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
  float w = static_cast<float>(surf->w);
  float h = static_cast<float>(surf->h);
  SDL_DestroySurface(surf);
  if (!tex)
    return nullptr;

  auto [ins, _] = cache.emplace(std::move(key), CachedText{tex, w, h});
  return &ins->second;
}

struct AtlasKey {
  unsigned size;
  uint32_t color;
  bool operator==(const AtlasKey &o) const {
    return size == o.size && color == o.color;
  }
};

struct AtlasKeyHash {
  size_t operator()(const AtlasKey &k) const {
    return std::hash<unsigned>{}(k.size) ^
           (std::hash<uint32_t>{}(k.color) << 1);
  }
};

static std::unordered_map<AtlasKey, GlyphAtlas, AtlasKeyHash> &atlas_cache() {
  static std::unordered_map<AtlasKey, GlyphAtlas, AtlasKeyHash> cache;
  return cache;
}

const GlyphAtlas *get_glyph_atlas(SDL_Renderer *renderer, unsigned size,
                                  Color color) {
  uint32_t col = (uint32_t(color.r) << 24) | (uint32_t(color.g) << 16) |
                 (uint32_t(color.b) << 8) | uint32_t(color.a);
  AtlasKey key{size, col};
  auto &cache = atlas_cache();
  auto it = cache.find(key);
  if (it != cache.end())
    return &it->second;

  TTF_Font *f = get_font(size);
  if (!f)
    return nullptr;
  SDL_Color sc = {color.r, color.g, color.b, color.a};

  // Monospace: every glyph's advance equals the rendered width of any one
  // character. Use "M" to probe.
  int cell_w = 0, cell_h = 0;
  if (!TTF_GetStringSize(f, "M", 1, &cell_w, &cell_h) || cell_w <= 0 ||
      cell_h <= 0) {
    return nullptr;
  }

  constexpr int kFirst = 32;
  constexpr int kLast = 126;
  constexpr int kCount = kLast - kFirst + 1;
  int atlas_w = cell_w * kCount;
  int atlas_h = cell_h;

  SDL_Surface *atlas_surf =
      SDL_CreateSurface(atlas_w, atlas_h, SDL_PIXELFORMAT_RGBA32);
  if (!atlas_surf) {
    std::cerr << "Failed to create glyph atlas surface: " << SDL_GetError()
              << "\n";
    return nullptr;
  }
  SDL_FillSurfaceRect(atlas_surf, nullptr, 0);

  GlyphAtlas atlas;
  atlas.cell_w = static_cast<float>(cell_w);
  atlas.cell_h = static_cast<float>(cell_h);
  atlas.tex_w = static_cast<float>(atlas_w);
  atlas.tex_h = static_cast<float>(atlas_h);

  for (int c = kFirst; c <= kLast; ++c) {
    char s[2] = {static_cast<char>(c), 0};
    SDL_Surface *gs = TTF_RenderText_Blended(f, s, 1, sc);
    if (!gs)
      continue;
    int dx = (c - kFirst) * cell_w;
    SDL_Rect dst = {dx, 0, gs->w, gs->h};
    SDL_SetSurfaceBlendMode(gs, SDL_BLENDMODE_NONE);
    SDL_BlitSurface(gs, nullptr, atlas_surf, &dst);

    GlyphAtlas::Glyph &g = atlas.glyphs[c];
    g.w = static_cast<float>(gs->w);
    g.h = static_cast<float>(gs->h);
    g.u0 = static_cast<float>(dx) / static_cast<float>(atlas_w);
    g.v0 = 0.f;
    g.u1 = static_cast<float>(dx + gs->w) / static_cast<float>(atlas_w);
    g.v1 = static_cast<float>(gs->h) / static_cast<float>(atlas_h);
    SDL_DestroySurface(gs);
  }

  SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, atlas_surf);
  SDL_DestroySurface(atlas_surf);
  if (!tex)
    return nullptr;
  SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
  atlas.tex = tex;

  auto [ins, _] = cache.emplace(key, atlas);
  return &ins->second;
}

void shutdown_fonts() {
  for (auto &[k, v] : text_cache())
    if (v.tex)
      SDL_DestroyTexture(v.tex);
  text_cache().clear();
  for (auto &[k, v] : atlas_cache())
    if (v.tex)
      SDL_DestroyTexture(v.tex);
  atlas_cache().clear();
  for (auto &[size, font] : font_cache())
    TTF_CloseFont(font);
  font_cache().clear();
}
