#pragma once
// Touch calibration maths (docs/cyd-ui.md §9, C4b). Pure functions, so the calibrate screen, the
// test-only calib_sim / calib_tap / selftest commands all run the same code.
//
// A calibration is {x_min, x_max, y_min, y_max, flags}. It is in ESPHome's terms: the
// touchscreen swaps the raw axes first (flags & SWAP), then maps x_min..x_max of the swapped x
// to screen x (and y likewise), then mirrors (MIRROR_X / MIRROR_Y). ESPHome needs min < max.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

namespace touchcal {

enum : uint16_t { SWAP = 1, MIRROR_X = 2, MIRROR_Y = 4 };
using Cal = std::array<uint16_t, 5>;

// Corner targets of the calibration screen (screen px), in tap order.
static const int TX[4] = {20, 300, 300, 20}, TY[4] = {20, 20, 220, 220};
static const int W = 320, H = 240;
// the owner's four taps on build 6729 (no swap configured, 2026-10-07), as the XPT2046 reported
// them: raw_x, raw_y per corner.
static const int REFERENCE_TAPS[8] = {524, 544, 516, 3378, 3423, 3313, 3553, 540};

// ESPHome 2026.9 Touchscreen::normalize_(), reproduced.
inline int normalize(int v, int lo, int hi, bool inv) {
  int r = v <= lo ? 0 : (v >= hi ? 0xfff : 0xfff * (v - lo) / (hi - lo));
  return inv ? 0xfff - r : r;
}

struct Point {
  int rx, ry;  // the raw pair as the touchscreen reports it (touch.x_raw / y_raw: after the swap)
  int x, y;    // screen px
};

// ESPHome 2026.9 Touchscreen::add_raw_touch_position_(), reproduced: hx/hy are the XPT2046's
// own axes (what a build without swap reports).
inline Point map(const Cal &c, int hx, int hy) {
  Point p{hx, hy, 0, 0};
  if (c[4] & SWAP) std::swap(p.rx, p.ry);
  p.x = normalize(p.rx, c[0], c[1], c[4] & MIRROR_X) * W / 0x1000;
  p.y = normalize(p.ry, c[2], c[3], c[4] & MIRROR_Y) * H / 0x1000;
  return p;
}

// Solve a calibration from the four corner readings r[2i], r[2i+1] (as reported, i.e. after the
// swap in `flags`). It does not assume which reported axis follows screen x: it measures it.
// Returns "" on success (out is set), else the rejection reason (out untouched).
inline std::string solve(const int r[8], uint16_t flags, Cal &out) {
  // mean of reported axis a over corners i and j
  auto side = [&](int a, int i, int j) { return (r[2 * i + a] + r[2 * j + a]) / 2.0f; };
  // change of each reported axis from the left to the right corners, and top to bottom
  float a_x = side(0, 1, 2) - side(0, 0, 3), a_y = side(0, 2, 3) - side(0, 0, 1);
  float b_x = side(1, 1, 2) - side(1, 0, 3), b_y = side(1, 2, 3) - side(1, 0, 1);
  bool cross = fabsf(a_x) + fabsf(b_y) < fabsf(a_y) + fabsf(b_x);  // reported x follows screen y
  int u = cross ? 1 : 0, v = 1 - u;                                // u follows screen x
  float xl = side(u, 0, 3), xr = side(u, 1, 2), yt = side(v, 0, 1), yb = side(v, 2, 3);
  float sx = xr - xl, sy = yb - yt;
  float off = std::max(fabsf(cross ? a_x : b_x), fabsf(cross ? b_y : a_y));  // off-axis change
  char why[80];
  if (fabsf(sx) < 1000 || fabsf(sy) < 800) {
    snprintf(why, sizeof why, "rejected: spread x=%.0f y=%.0f", sx, sy);
    return why;
  }
  if (off > std::min(fabsf(sx), fabsf(sy)) / 2) {
    snprintf(why, sizeof why, "rejected: axes unclear (off-axis %.0f)", off);
    return why;
  }
  // Extrapolate from the targets (20 px in) to the screen edges.
  float kx = sx / (TX[1] - TX[0]), ky = sy / (TY[2] - TY[0]);
  float x0 = xl - TX[0] * kx, x1 = xr + (W - TX[1]) * kx;
  float y0 = yt - TY[0] * ky, y1 = yb + (H - TY[2]) * ky;
  auto edge = [](float f) { return (uint16_t) std::min(4095.0f, std::max(0.0f, roundf(f))); };
  uint16_t swap = (flags & SWAP) ^ (cross ? SWAP : 0);
  out = {edge(std::min(x0, x1)), edge(std::max(x0, x1)), edge(std::min(y0, y1)), edge(std::max(y0, y1)),
         uint16_t(swap | (kx < 0 ? MIRROR_X : 0) | (ky < 0 ? MIRROR_Y : 0))};
  return "";
}

}  // namespace touchcal
