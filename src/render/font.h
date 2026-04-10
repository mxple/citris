#pragma once

#include "sdl_types.h"
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

TTF_Font *get_font(unsigned size);

struct CachedText {
  SDL_Texture *tex = nullptr;
  float w = 0.f, h = 0.f;
};

// Lazy cache: keeps one texture per (string, size, color) forever.
const CachedText *get_text(SDL_Renderer *renderer, const char *str,
                           unsigned size, Color color);

// Glyph atlas for dynamic text — packs printable ASCII (32..126) into one
// texture per (size, color) pair. Used for numeric stats strings that would
// otherwise flood the whole-string cache.
struct GlyphAtlas {
  SDL_Texture *tex = nullptr;
  float tex_w = 0.f, tex_h = 0.f;
  float cell_w = 0.f, cell_h = 0.f; // monospace: advance and line height
  struct Glyph {
    float u0 = 0.f, v0 = 0.f, u1 = 0.f, v1 = 0.f;
    float w = 0.f, h = 0.f; // pixel size of the rendered glyph image
  };
  Glyph glyphs[128] = {};
};

const GlyphAtlas *get_glyph_atlas(SDL_Renderer *renderer, unsigned size,
                                  Color color);

void shutdown_fonts();
