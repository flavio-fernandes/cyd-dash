#pragma once
// Refresh monitor: times each LVGL refresh that rendered (render + flush), in 60 s windows.
// The status JSON reports it as refr_n, refr_ms, refr_max_ms, busy (% of the time spent
// drawing) and inv_px (average invalidated pixels per refresh), whatever page is on screen.
#include "esphome.h"
#include <algorithm>

namespace cyd_perf {

static uint32_t refr_t0 = 0, win_start = 0, win_n = 0, win_us = 0, win_max = 0;
static uint32_t last_n = 0, last_us = 0, last_max = 0, last_ms = 0, win_px = 0, last_px = 0;
static bool rendered = false;

inline void refr_cb(lv_event_t *e) {
  auto code = lv_event_get_code(e);
  if (code == LV_EVENT_REFR_START) {
    refr_t0 = micros();
    rendered = false;
  } else if (code == LV_EVENT_RENDER_START) {
    rendered = true;
  } else if (code == LV_EVENT_REFR_READY && rendered) {
    uint32_t us = micros() - refr_t0;
    ++win_n;
    win_us += us;
    win_max = std::max(win_max, us);
  } else if (code == LV_EVENT_INVALIDATE_AREA) {
    auto *a = (const lv_area_t *) lv_event_get_param(e);
    if (a) win_px += lv_area_get_size(a);
  }
}

// Once at boot.
inline void begin() {
  win_start = millis();
  auto disp = lv_display_get_default();
  if (!disp) return;
  lv_display_add_event_cb(disp, refr_cb, LV_EVENT_REFR_START, nullptr);
  lv_display_add_event_cb(disp, refr_cb, LV_EVENT_RENDER_START, nullptr);
  lv_display_add_event_cb(disp, refr_cb, LV_EVENT_REFR_READY, nullptr);
  lv_display_add_event_cb(disp, refr_cb, LV_EVENT_INVALIDATE_AREA, nullptr);
}

// Every second: close the window after 60 s.
inline void roll_window() {
  uint32_t now = millis();
  if (now - win_start < 60000) return;
  last_n = win_n; last_us = win_us; last_max = win_max; last_ms = now - win_start; last_px = win_px;
  win_n = win_us = win_max = win_px = 0;
  win_start = now;
}
inline float busy_pct() { return last_ms ? 100.0f * last_us / 1000.0f / last_ms : 0; }
inline float refr_ms() { return last_n ? last_us / 1000.0f / last_n : 0; }

}  // namespace cyd_perf
