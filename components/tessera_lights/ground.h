#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include "esphome/components/smart_display/plugin_api.h"

namespace esphome::tessera_lights {

// A colour as a lit ground for white words (the dashboard's glow_color): saturated a little more, and never brighter
// than `peak`, less for a grey colour.
inline lv_color_t lit(uint32_t tone, float peak) {
  float r = (tone >> 16) & 0xFF, g = (tone >> 8) & 0xFF, b = tone & 0xFF;
  const float hi = std::max({r, g, b, 1.0f}), lo = std::min({r, g, b}), avg = (r + g + b) / 3;
  r = std::max(0.0f, avg + (r - avg) * 1.2f), g = std::max(0.0f, avg + (g - avg) * 1.2f),
  b = std::max(0.0f, avg + (b - avg) * 1.2f);
  peak *= 0.4f + 0.6f * std::min(1.0f, (hi - lo) / hi * 2.0f);
  const float scale = peak / std::max({r, g, b, 1.0f});
  return lv_color_make((uint8_t) (r * scale), (uint8_t) (g * scale), (uint8_t) (b * scale));
}

// The colour of white light at a temperature in kelvin (Tanner Helland's fit).
inline uint32_t kelvin_rgb(int kelvin) {
  const float k = std::clamp(kelvin, 1000, 12000) / 100.0f;
  const float r = k <= 66 ? 255 : std::clamp(329.7f * std::pow(k - 60, -0.1332f), 0.0f, 255.0f);
  const float g = k <= 66 ? std::clamp(99.47f * std::log(k) - 161.12f, 0.0f, 255.0f)
                          : std::clamp(288.12f * std::pow(k - 60, -0.0755f), 0.0f, 255.0f);
  const float b = k >= 66 ? 255 : k <= 19 ? 0 : std::clamp(138.52f * std::log(k - 10) - 305.04f, 0.0f, 255.0f);
  return ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) b;
}

// A picture of `w` x `h` whose colour at (x, y) `want_at` gives, Floyd-Steinberg dithered into RGB565 so a soft blend
// shows no bands.
template <class Want> inline void dither(uint16_t *pixels, int w, int h, Want want_at) {
  std::vector<float> rows(6 * (w + 2), 0.0f);
  float *cur = rows.data(), *next = cur + 3 * (w + 2);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      float want[3];
      want_at(x, y, want);
      const int levels[3] = {31, 63, 31};
      int q[3];
      for (int c = 0; c < 3; c++) {
        const int i = (x + 1) * 3 + c;
        const float v = std::clamp(want[c] + cur[i], 0.0f, 255.0f);
        q[c] = (int) (v * levels[c] / 255.0f + 0.5f);
        const float e = v - q[c] * 255.0f / levels[c];
        cur[i + 3] += e * 7 / 16;
        next[i - 3] += e * 3 / 16;
        next[i] += e * 5 / 16;
        next[i + 3] += e / 16;
      }
      pixels[y * w + x] = (uint16_t) ((q[0] << 11) | (q[1] << 5) | q[2]);
    }
    std::swap(cur, next);
    std::fill(next, next + 3 * (w + 2), 0.0f);
  }
}

// A diagonal blend from `top` (top left) to `foot` (bottom right).
inline void gradient(uint16_t *pixels, int w, int h, lv_color_t top, lv_color_t foot) {
  dither(pixels, w, h, [&](int x, int y, float *want) {
    const float t = std::min(1.0f, 0.55f * y / std::max(1, h - 1) + 0.45f * x / std::max(1, w - 1));
    want[0] = top.red + (foot.red - top.red) * t;
    want[1] = top.green + (foot.green - top.green) * t;
    want[2] = top.blue + (foot.blue - top.blue) * t;
  });
}

// A picture in RGB565 that LVGL can show: the descriptor of `pixels`.
inline void describe(lv_image_dsc_t &dsc, uint8_t *pixels, int w, int h) {
  dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  dsc.header.w = w;
  dsc.header.h = h;
  dsc.header.stride = w * 2;
  dsc.data = pixels;
  dsc.data_size = (size_t) w * h * 2;
}

}  // namespace esphome::tessera_lights
