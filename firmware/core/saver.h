#pragma once
// Screensaver (docs/cyd-ui.md §6): a small box that drifts around a black screen and bounces
// off the edges, one step every 3 s from the shared 100 ms clock. It is part of the core, not
// a page: it is never in the page list or the rotation, and every build has it.
//
// With the usage page in the build it shows each plan's 5-hour % as two small rings (usage.h's
// Ring, no second copy, without the glow). Without it, it shows the clock.
#include "esphome.h"
#include <cmath>
#include <string>

namespace cyd_saver {

static lv_obj_t *box = nullptr;
static uint32_t k = 0, steps = 0;
static int x = 40, y = 50, vx = 9, vy = 7;

#ifdef CYD_HAS_PAGE_usage
using namespace cyd_usage;

static const int W = 156, H = 94;
static lv_obj_t *pct[2], *names[2];
static std::string name_text[2];
static Ring rings[2];
static std::string t_pct[2];
static uint32_t c_pct[2] = {0, 0};

// After the pages are built (it uses the usage page's fonts).
inline void build(lv_obj_t *root) {
  lv_obj_set_size(root, 320, 240);
  lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  box = mk_box(root);
  lv_obj_set_size(box, W, H);
  lv_obj_set_pos(box, x, y);
  for (int i = 0; i < cyd_usage_data::count(); i++) {
    int cx = cyd_usage_data::count() == 1 ? W / 2 : 38 + 80 * i;
    rings[i].build(box, cx, 38, 36, 8);
    pct[i] = mk_label(box, f20, GREY, cx, 28, "--");
    lv_obj_align(pct[i], LV_ALIGN_TOP_MID, cx - W / 2, 28);
    names[i] = mk_label(box, f16, GREY, cx, 74, "");
    lv_obj_set_width(names[i], 74); lv_label_set_long_mode(names[i], LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(names[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(names[i], LV_ALIGN_TOP_MID, cx - W / 2, 74);
  }
}

// Called every second while the saver shows: §3.1 colours, grey unless fresh.
inline void render(int64_t now, int st, const std::string &clock) {
  if (!box) return;
  cyd_usage_data::refresh(now);
  for (int i = 0; i < cyd_usage_data::count(); i++) {
    const CydPlan &p = cyd_usage_plans[i];
    set_text(names[i], name_text[i], p.label);
    bool data = p.state != WAITING, grey = p.state != FRESH || p.snoozed;
    float u = p.five.inferred ? 0.0f : p.five.used;
    uint32_t c = grey ? GREY : (p.limit_reached ? RED : fill_colour(u, p.five.cap));
    rings[i].update({data, p.five.inferred, !grey && p.old, grey, p.limit_reached, false, false, u, p.five.cap, 0.0f, c});
    std::string s;
    if (!data) s = "--";
    else if (p.five.inferred) s = "0%*";
    else if (std::isnan(u)) s = "--%";
    else { char b[8]; snprintf(b, sizeof b, "%.0f%%", u); s = b; }
    set_text(pct[i], t_pct[i], s);
    set_text_color(pct[i], c_pct[i], (!data || p.five.inferred) ? GREY : c);
  }
}

#else  // no usage page: a clock

static const int W = 120, H = 52;
static lv_obj_t *clock_label = nullptr;
static std::string t_clock;

// The first child of the saver root is a hidden label holding the 32 px font (core/saver.yaml).
inline void build(lv_obj_t *root) {
  lv_obj_set_size(root, 320, 240);
  lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  const lv_font_t *font = lv_obj_get_style_text_font(lv_obj_get_child(root, 0), LV_PART_MAIN);
  box = lv_obj_create(root);
  lv_obj_remove_style_all(box);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(box, W, H);
  lv_obj_set_pos(box, x, y);
  clock_label = lv_label_create(box);
  lv_obj_set_style_text_font(clock_label, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(clock_label, lv_color_hex(0x788695), LV_PART_MAIN);  // 5.3:1 on black
  lv_label_set_text(clock_label, "--:--");
  lv_obj_center(clock_label);
}

inline void render(int64_t now, int st, const std::string &clock) {
  if (!clock_label || clock == t_clock) return;
  t_clock = clock;
  lv_label_set_text(clock_label, clock.c_str());
}

#endif

// Called at 10 Hz from the shared clock; one position step every 3 s while visible.
inline void tick(bool visible) {
  if (!box || !visible) return;
  ++k;
#ifdef CYD_HAS_PAGE_usage
  int reds = 0;
  for (int i = 0; i < cyd_usage_data::count(); ++i) reds += rings[i].pulse_on;
  for (int i = 0; i < cyd_usage_data::count(); ++i)
    rings[i].breathe(k, (k & 1) == 0 && (reds < 2 || int((k >> 1) & 1) == i));
#endif
  if (k % 30) return;
  x += vx;
  y += vy;
  if (x < 0) { x = -x; vx = -vx; }
  if (x > 320 - W) { x = 2 * (320 - W) - x; vx = -vx; }
  if (y < 0) { y = -y; vy = -vy; }
  if (y > 240 - H) { y = 2 * (240 - H) - y; vy = -vy; }
  lv_obj_set_pos(box, x, y);
  ++steps;
}

}  // namespace cyd_saver
