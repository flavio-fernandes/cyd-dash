#pragma once
// Pure calculations, shared with the offline tests. Comfort bands are illustrative.
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace cyd_climate {
inline float celsius(float value, bool fahrenheit) { return fahrenheit ? (value - 32) / 1.8f : value; }
inline uint32_t mix(uint32_t a, uint32_t b, float t) {
  t = std::max(0.0f, std::min(1.0f, t));
  uint32_t out = 0;
  for (int shift : {16, 8, 0}) {
    float x = (a >> shift) & 255, y = (b >> shift) & 255;
    out |= uint32_t(std::lround(x + (y - x) * t)) << shift;
  }
  return out;
}
inline uint32_t colour(float c) {
  if (c <= 21) return mix(0x70BFFF, 0x32D583, (c - 16) / 5);
  if (c <= 26) return mix(0x32D583, 0xFFB547, (c - 21) / 5);
  return mix(0xFFB547, 0xF04452, (c - 26) / 5);
}
inline const char *band(float c) { return c < 19 ? "Cool" : c <= 24 ? "Comfortable" : c <= 28 ? "Warm" : "Hot"; }
inline float ease(float current, float target) {
  return std::fabs(target - current) < 0.08f ? target : current + (target - current) * 0.35f;
}
inline int arc(float c) { return int(std::lround(std::max(0.0f, std::min(1.0f, (c - 10) / 25)) * 270)); }
}  // namespace cyd_climate
