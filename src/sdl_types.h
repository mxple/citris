#pragma once
#include <SDL3/SDL.h>
#include <cstdint>

struct Color {
  uint8_t r = 0, g = 0, b = 0, a = 255;
  constexpr Color() = default;
  constexpr Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
      : r(r), g(g), b(b), a(a) {}
  static constexpr Color Transparent() { return {0, 0, 0, 0}; }
  static constexpr Color White() { return {255, 255, 255, 255}; }
  SDL_FColor to_sdl() const {
    return {r / 255.f, g / 255.f, b / 255.f, a / 255.f};
  }
};

using KeyCode = SDL_Keycode;
