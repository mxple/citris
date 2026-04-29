#include "clipboard.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <cstring>

namespace imp {

#ifdef __EMSCRIPTEN__

#include <emscripten.h>

// Calls navigator.clipboard.read(), finds the first image/* blob, decodes it
// onto an offscreen canvas, and returns the raw RGBA pixels.  Returns null if
// no image is available or the user denied permission.
// ASYNCIFY lets this suspend the wasm module while the browser await completes.
EM_ASYNC_JS(uint8_t*, em_clipboard_read_image, (int* out_w, int* out_h), {
  try {
    var items = await navigator.clipboard.read();
    var blob = null;
    for (var i = 0; i < items.length; i++) {
      var types = items[i].types;
      for (var j = 0; j < types.length; j++) {
        if (types[j].startsWith("image/")) {
          blob = await items[i].getType(types[j]);
          break;
        }
      }
      if (blob) break;
    }
    if (!blob) return 0;

    var bmp = await createImageBitmap(blob);
    var canvas = new OffscreenCanvas(bmp.width, bmp.height);
    var ctx = canvas.getContext("2d");
    ctx.drawImage(bmp, 0, 0);
    var imageData = ctx.getImageData(0, 0, bmp.width, bmp.height);

    var nBytes = imageData.data.length;
    var ptr = _malloc(nBytes);
    HEAPU8.set(imageData.data, ptr);
    HEAP32[out_w >> 2] = bmp.width;
    HEAP32[out_h >> 2] = bmp.height;
    return ptr;
  } catch (e) {
    return 0;
  }
});

std::optional<CvImage> read_clipboard_image() {
  int w = 0, h = 0;
  uint8_t* pixels = em_clipboard_read_image(&w, &h);
  if (!pixels || w <= 0 || h <= 0) {
    if (pixels) free(pixels);
    return std::nullopt;
  }
  CvImage img;
  img.w = w;
  img.h = h;
  img.rgba.assign(pixels, pixels + size_t(w) * size_t(h) * 4);
  free(pixels);
  return img;
}

#else

namespace {

std::optional<CvImage> surface_to_image(SDL_Surface* src) {
  if (!src) return std::nullopt;
  SDL_Surface* rgba = SDL_ConvertSurface(src, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(src);
  if (!rgba) return std::nullopt;

  CvImage img;
  img.w = rgba->w;
  img.h = rgba->h;
  img.rgba.resize(size_t(img.w) * size_t(img.h) * 4);

  // Copy row by row to honor the source pitch (may include padding).
  const auto* pixels = static_cast<const uint8_t*>(rgba->pixels);
  size_t row_bytes = size_t(img.w) * 4;
  for (int y = 0; y < img.h; ++y) {
    std::memcpy(img.rgba.data() + size_t(y) * row_bytes,
                pixels + size_t(y) * size_t(rgba->pitch), row_bytes);
  }
  SDL_DestroySurface(rgba);
  return img;
}

std::optional<CvImage> try_decode_mime(const char* mime) {
  if (!SDL_HasClipboardData(mime)) return std::nullopt;
  size_t size = 0;
  void* data = SDL_GetClipboardData(mime, &size);
  if (!data || size == 0) {
    if (data) SDL_free(data);
    return std::nullopt;
  }
  SDL_IOStream* io = SDL_IOFromConstMem(data, size);
  if (!io) {
    SDL_free(data);
    return std::nullopt;
  }
  // IMG_Load_IO closes `io` regardless of success when closeio=true. We free
  // the underlying buffer ourselves after.
  SDL_Surface* surf = IMG_Load_IO(io, true);
  SDL_free(data);
  return surface_to_image(surf);
}

}  // namespace

std::optional<CvImage> read_clipboard_image() {
  if (auto img = try_decode_mime("image/png")) return img;
  if (auto img = try_decode_mime("image/bmp")) return img;
  if (auto img = try_decode_mime("image/jpeg")) return img;
  return std::nullopt;
}

#endif

}  // namespace imp
