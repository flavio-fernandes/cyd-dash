#pragma once
// The page registry at run time: navigation, Auto rotation, the nav dots, drift and the page
// hooks. The table (cyd_pages::PAGES) is generated into generated/cyd_registry.h from the
// pages chosen in site.yaml, so nothing here knows a page by name.
#include "esphome.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>

namespace cyd_pages {

// screen_want / screen_shown: 0..CYD_PAGE_COUNT-1 are pages; these two are the core screens.
static const int SCREEN_SAVER = 98, SCREEN_CALIB = 99;
static const uint32_t DOT_ON = 0xFFFFFF, DOT_OFF = 0x5A6878;

static lv_obj_t *roots[CYD_PAGE_COUNT];
static lv_obj_t *dots[CYD_PAGE_COUNT];
static int dot_shown = -1, last_shown = -1;
static bool built = false;
static uint32_t ticks = 0;

inline int clamp(int i) { return i >= 0 && i < CYD_PAGE_COUNT ? i : 0; }
inline const char *name(int i) { return PAGES[clamp(i)].name; }
inline int next(int i) { return (clamp(i) + 1) % CYD_PAGE_COUNT; }
inline int prev(int i) { return (clamp(i) + CYD_PAGE_COUNT - 1) % CYD_PAGE_COUNT; }

// "usage" or "2" -> the page's index; -1 when there is no such page in this build.
inline int lookup(const std::string &s) {
  for (int i = 0; i < CYD_PAGE_COUNT; i++)
    if (s == PAGES[i].name) return i;
  char *end;
  long n = strtol(s.c_str(), &end, 10);
  if (!s.empty() && !*end && n >= 0 && n < CYD_PAGE_COUNT) return int(n);
  return -1;
}

// The next page after i that takes part in Auto rotation (i itself when there is none).
inline int next_rotating(int i) {
  for (int k = 1; k <= CYD_PAGE_COUNT; k++) {
    int q = (clamp(i) + k) % CYD_PAGE_COUNT;
    if (PAGES[q].rotate) return q;
  }
  return i;
}

// Once at boot, with each page's root object (from the generated cyd_pages_build script).
inline void build_all(lv_obj_t *const *r) {
  for (int i = 0; i < CYD_PAGE_COUNT; i++) {
    roots[i] = r[i];
    // Drift moves the page roots ±3 px past the screen edge: never let that make a page
    // scrollable (a scrollbar would appear and add redraws).
    lv_obj_t *screen = lv_obj_get_parent(r[i]);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
    if (PAGES[i].build) PAGES[i].build(r[i]);
  }
  built = true;
}

inline void set_drift(int x, int y) {
  for (int i = 0; i < CYD_PAGE_COUNT; i++)
    if (roots[i]) lv_obj_set_pos(roots[i], x, y);
}

// The page dots, between the ‹ and › buttons (x 56..106 of the bar). Up to 3 pages they sit
// exactly where they always did (10 px, 16 px apart); more pages pack closer. A one-page
// build hides the arrows and the dots: Pin and Sleep stay.
inline void build_nav(lv_obj_t *bar, lv_obj_t *prev_btn, lv_obj_t *next_btn) {
  if (CYD_PAGE_COUNT < 2) {
    lv_obj_add_flag(prev_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(next_btn, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  int pitch = std::min(16, 50 / CYD_PAGE_COUNT);
  int size = std::max(4, std::min(10, pitch - 2));
  int span = (CYD_PAGE_COUNT - 1) * pitch + size;
  int x0 = 81 - span / 2, y = 22 - size / 2;
  for (int i = 0; i < CYD_PAGE_COUNT; i++) {
    lv_obj_t *d = lv_obj_create(bar);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, size, size);
    lv_obj_set_pos(d, x0 + i * pitch, y);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(d, lv_color_hex(DOT_OFF), LV_PART_MAIN);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE);
    dots[i] = d;
  }
}
inline void update_dots(int idx) {
  if (CYD_PAGE_COUNT < 2 || !dots[0] || idx == dot_shown) return;
  for (int i = 0; i < CYD_PAGE_COUNT; i++)
    lv_obj_set_style_bg_color(dots[i], lv_color_hex(i == idx ? DOT_ON : DOT_OFF), LV_PART_MAIN);
  dot_shown = idx;
}

// From the one 100 ms clock (core/ui.yaml). shown = the page on screen, or -1 (sleep, saver,
// calibration). Steps animations, then calls render for every page with a changed slot, for
// the page that just came on screen, and once a second for the page on screen.
inline void tick10(int shown, const cyd_page::Ctx &c) {
  if (!built) return;
  ++ticks;
  for (int i = 0; i < CYD_PAGE_COUNT; i++)
    if (PAGES[i].tick10) PAGES[i].tick10(i == shown);
  uint32_t dirty = cyd_slots::take_dirty();
  bool second = ticks % 10 == 0, arrived = shown != last_shown;
  last_shown = shown;
  for (int i = 0; i < CYD_PAGE_COUNT; i++) {
    if (!PAGES[i].render) continue;
    bool vis = i == shown;
    if (!((dirty >> i) & 1) && !(vis && (second || arrived))) continue;
    cyd_page::Ctx cc = c;
    cc.visible = vis;
    PAGES[i].render(cc);
  }
}

// Status JSON: the page list, then each page's own fields.
inline void status(JsonObject root) {
  auto list = root["pages"].to<JsonArray>();
  for (int i = 0; i < CYD_PAGE_COUNT; i++) list.add(PAGES[i].name);
  for (int i = 0; i < CYD_PAGE_COUNT; i++)
    if (PAGES[i].status) PAGES[i].status(root);
}

inline void selftest_all() {
  for (int i = 0; i < CYD_PAGE_COUNT; i++)
    if (PAGES[i].selftest) PAGES[i].selftest();
}

}  // namespace cyd_pages
