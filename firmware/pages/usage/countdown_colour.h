#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cyd_countdown {
// HSV H=0..275 degrees, S=0.55, V=1.0. Pastel blue is the darkest point.
inline uint32_t rainbow(float progress) {
  constexpr float saturation = 0.55f, base = 1.0f - saturation;
  float h = std::max(0.0f, std::min(1.0f, progress)) * 275.0f / 60.0f;
  float x = saturation * (1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f));
  float r = 0, g = 0, b = 0;
  if (h < 1) { r = saturation; g = x; }
  else if (h < 2) { r = x; g = saturation; }
  else if (h < 3) { g = saturation; b = x; }
  else if (h < 4) { g = x; b = saturation; }
  else { r = x; b = saturation; }
  return (uint32_t(std::lround((r + base) * 255)) << 16) |
         (uint32_t(std::lround((g + base) * 255)) << 8) |
          uint32_t(std::lround((b + base) * 255));
}
inline uint32_t colour(int64_t resets, int64_t now, bool fresh, bool inferred, bool snoozed) {
  if (!fresh || inferred || snoozed || resets < 0) return 0x788695;
  return rainbow(1.0f - float(resets - now) / 18000.0f);
}
}  // namespace cyd_countdown
