#pragma once
// Usage page (docs/cyd-ui.md §3-4): two 270° gauges with the edge-glow countdown, every
// per-gauge state and the waiting/fresh/stale screen states. Widgets are created once at boot
// (build) and only updated afterwards (render, tick10): showing or rebuilding a page is the
// expensive moment (C2). Needs model.h (CydPlan) included before this file.
#include "esphome.h"
#include "countdown_colour.h"
#include <cmath>
#include <string>
#include <algorithm>

namespace cyd_usage {

// Palette, docs/cyd-ui.md §3.1 (C2 §2).
static const uint32_t BG = 0x101820, TRACK = 0x202A36, CAPZ = 0x35475A, GREEN = 0x32D583,
                      AMBER = 0xFFB547, RED = 0xF04452, GREY = 0xAAB7C4, VIOLET = 0xB58AFF,
                      WHITE = 0xFFFFFF;
enum State { WAITING = 0, FRESH = 1, STALE = 2 };

static const lv_font_t *f16, *f20, *f24, *f28, *f32, *fcompact;
// C4b layout, measured at build and logged by the self-test (core/calib.yaml).
static int sub_ink = 0, sub_base = 0, wk_ink = 0;
static int sub_w = 0, sub_h = 0, sub_y = 0, upd_y = 0, upd_h = 0, nod_y = 0, nod_h = 0;
static uint32_t glow_steps = 0, band_steps = 0, pulse_steps = 0, ticks = 0;

// ---- small helpers: every setter skips the call when nothing changed ----
inline void set_hidden(lv_obj_t *o, bool h) {
  if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) == h) return;
  if (h) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
}
inline void set_text(lv_obj_t *o, std::string &cache, const std::string &s) {
  if (cache == s) return;
  cache = s;
  lv_label_set_text(o, s.c_str());
}
inline void set_text_color(lv_obj_t *o, uint32_t &cache, uint32_t c) {
  if (cache == c) return;
  cache = c;
  lv_obj_set_style_text_color(o, lv_color_hex(c), LV_PART_MAIN);
}
inline void set_arc_color(lv_obj_t *o, uint32_t &cache, uint32_t c, lv_style_selector_t part) {
  if (cache == c) return;
  cache = c;
  lv_obj_set_style_arc_color(o, lv_color_hex(c), part);
}
inline void set_arc_opa(lv_obj_t *o, int &cache, int opa, lv_style_selector_t part) {
  if (cache == opa) return;
  cache = opa;
  lv_obj_set_style_arc_opa(o, opa, part);
}
inline void set_angles(lv_obj_t *o, int &s_cache, int &e_cache, int s, int e) {
  if (s_cache == s && e_cache == e) return;
  s_cache = s; e_cache = e;
  lv_arc_set_angles(o, s, e);
}
inline lv_obj_t *mk_box(lv_obj_t *parent) {
  auto o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}
// A square-ended 270° arc layer (rotation 135, open at the bottom). The main part is the
// background track (transparent unless asked for); the indicator is the drawn segment.
inline lv_obj_t *mk_arc(lv_obj_t *parent, int x, int y, int d, int w) {
  auto o = lv_arc_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, d, d);
  lv_arc_set_rotation(o, 135);
  lv_arc_set_bg_angles(o, 0, 270);
  lv_arc_set_angles(o, 0, 0);
  lv_obj_set_style_arc_width(o, w, LV_PART_MAIN);
  lv_obj_set_style_arc_width(o, w, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(o, false, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(o, false, LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(o, LV_OPA_TRANSP, LV_PART_MAIN);
  return o;
}
inline lv_obj_t *mk_label(lv_obj_t *p, const lv_font_t *f, uint32_t col, int cx, int y, const char *t) {
  auto o = lv_label_create(p);
  lv_obj_set_style_text_font(o, f, LV_PART_MAIN);
  lv_obj_set_style_text_color(o, lv_color_hex(col), LV_PART_MAIN);
  lv_label_set_text(o, t);
  lv_obj_align(o, LV_ALIGN_TOP_MID, cx - 160, y);
  return o;
}
inline lv_obj_t *mk_chip(lv_obj_t *row, uint32_t fg, uint32_t bg, bool filled) {
  auto o = lv_label_create(row);
  lv_obj_set_style_text_font(o, f20, LV_PART_MAIN);
  lv_obj_set_style_text_color(o, lv_color_hex(fg), LV_PART_MAIN);
  lv_obj_set_style_pad_hor(o, 5, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(o, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(o, 4, LV_PART_MAIN);
  if (filled) {
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
  } else {
    lv_obj_set_style_border_color(o, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 2, LV_PART_MAIN);
  }
  lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  return o;
}
inline int deg(float pct) {
  if (std::isnan(pct)) return 0;
  return (int) lroundf(std::min(100.0f, std::max(0.0f, pct)) * 2.7f);
}
inline uint32_t fill_colour(float used, float cap) {
  if (std::isnan(used)) return GREY;
  if (std::isnan(cap) || cap <= 0) return GREEN;
  if (used >= cap) return RED;
  if (used >= 0.8f * cap) return AMBER;
  return GREEN;
}
inline float urgency(int64_t resets, int64_t now, float window) {
  if (resets < 0) return 0;
  float u = 1.0f - float(resets - now) / window;
  return std::min(1.0f, std::max(0.0f, u));
}
// §3.1 countdown format: 47m, 2h13, 4d6h, now.
inline std::string countdown(int64_t resets, int64_t now) {
  if (resets < 0 || now < 0) return "--";
  int64_t r = resets - now;
  if (r <= 0) return "now";
  char b[16];
  if (r < 3600) snprintf(b, sizeof b, "%dm", int(std::max<int64_t>(1, r / 60)));
  else if (r < 86400) snprintf(b, sizeof b, "%dh%02d", int(r / 3600), int(r % 3600 / 60));
  else snprintf(b, sizeof b, "%dd%dh", int(r / 86400), int(r % 86400 / 3600));
  return b;
}

// ---- one ring: track + cap zone, fill, cap tick, edge glow, dashed ghost ----
struct RingView {
  bool data, inferred, dim, grey, full, glow, fast;
  float used, cap, urgency;
  uint32_t colour;
  bool donated = false;
  bool breathe = true;
};
struct Ring {
  static const int DASHES = 10;
  int cx = 0, cy = 0, R = 0, w = 0;
  lv_obj_t *track = nullptr, *fill = nullptr, *tick = nullptr, *glow_box = nullptr, *glow = nullptr;
  lv_obj_t *dash[DASHES];
  int cap_s = -1, cap_e = -1, fill_s = -1, fill_e = -1, tick_s = -1, tick_e = -1, glow_s = -1, glow_e = -1;
  int cap_opa = -1, fill_opa = -1, tick_opa = -1, glow_opa = -1;
  uint32_t fill_col = 0, cap_col = 0, tick_col = 0;
  bool glow_on = false, fast = false;
  float phase = 0, period = 4.5f;
  bool pulse_on = false;  // over the cap: the whole red fill breathes instead of the glow

  void build(lv_obj_t *p, int x, int y, int radius, int width) {
    cx = x; cy = y; R = radius; w = width;
    track = mk_arc(p, cx - R, cy - R, 2 * R, w);
    lv_obj_set_style_arc_color(track, lv_color_hex(TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(track, LV_OPA_COVER, LV_PART_MAIN);
    fill = mk_arc(p, cx - R, cy - R, 2 * R, w);
    tick = mk_arc(p, cx - R - 1, cy - R - 1, 2 * R + 2, w + 2);  // 1 px proud on both edges
    lv_obj_set_style_arc_color(tick, lv_color_hex(WHITE), LV_PART_INDICATOR);
    // The glow arc sits in a small clipping box over its segment, so an opacity step
    // invalidates ~27x27 px instead of the arc's 128x128 box (C2 §4: 27 ms per full box).
    glow_box = mk_box(p);
    glow = mk_arc(glow_box, 0, 0, 2 * R, w);
    lv_obj_set_style_arc_color(glow, lv_color_hex(WHITE), LV_PART_INDICATOR);
    lv_obj_add_flag(glow_box, LV_OBJ_FLAG_HIDDEN);
    int rd = R - w / 2 + 1;  // thin ghost outline along the band's middle
    for (int i = 0; i < DASHES; i++) {
      dash[i] = mk_arc(p, cx - rd, cy - rd, 2 * rd, 3);
      lv_obj_set_style_arc_color(dash[i], lv_color_hex(GREY), LV_PART_INDICATOR);
      int s = i * (270 - 12) / (DASHES - 1);
      lv_arc_set_angles(dash[i], s, s + 12);
      lv_obj_add_flag(dash[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  void place_glow(int u) {
    int s = std::max(0, u - 7);
    if (s == glow_s && u == glow_e) return;
    float th = (135.0f + (s + u) / 2.0f) * float(M_PI) / 180.0f;
    float rm = R - w / 2.0f;
    int S = w + 14;
    int bx = (int) lroundf(cx + rm * cosf(th) - S / 2.0f);
    int by = (int) lroundf(cy + rm * sinf(th) - S / 2.0f);
    lv_obj_set_pos(glow_box, bx, by);
    lv_obj_set_size(glow_box, S, S);
    lv_obj_set_pos(glow, cx - R - bx, cy - R - by);
    set_angles(glow, glow_s, glow_e, s, u);
  }
  void update(const RingView &v) {
    bool ghost = v.data && v.inferred;
    for (auto d : dash) set_hidden(d, !ghost);
    set_hidden(track, ghost);
    bool show = v.data && !ghost;
    int u = v.full ? 270 : deg(v.used);
    int c = deg(v.cap);
    // cap zone = the track's indicator, 0 -> cap_pct
    set_arc_color(track, cap_col, v.grey ? TRACK : (v.donated ? VIOLET : CAPZ), LV_PART_INDICATOR);
    set_arc_color(tick, tick_col, (!v.grey && v.donated) ? VIOLET : WHITE, LV_PART_INDICATOR);
    set_arc_opa(track, cap_opa, (show && !std::isnan(v.cap) && c > 0) ? (v.dim ? 128 : 255) : 0, LV_PART_INDICATOR);
    set_angles(track, cap_s, cap_e, 0, std::max(c, 1));
    set_hidden(fill, !show || u <= 0);
    bool pulse = show && u > 0 && !v.grey && !v.dim && v.colour == RED && v.breathe;
    if (show && u > 0) {
      set_angles(fill, fill_s, fill_e, 0, u);
      set_arc_color(fill, fill_col, v.grey ? GREY : v.colour, LV_PART_INDICATOR);
      if (!pulse) set_arc_opa(fill, fill_opa, v.dim ? 90 : 255, LV_PART_INDICATOR);
    }
    pulse_on = pulse;
    set_hidden(tick, !show || std::isnan(v.cap));
    if (show && !std::isnan(v.cap)) {
      set_angles(tick, tick_s, tick_e, std::max(0, c - 1), std::min(270, std::max(c - 1, 0) + 2));
      set_arc_opa(tick, tick_opa, v.dim ? 128 : 255, LV_PART_INDICATOR);
    }
    bool on = show && v.glow && u > 0 && !pulse;
    if (on) {
      place_glow(u);
      period = 4.5f - 3.0f * v.urgency;  // 4.5 s -> 1.5 s as the reset nears
      fast = v.fast && v.urgency >= 0.5f;
    }
    if (on != glow_on) {
      glow_on = on;
      phase = 0;
      set_hidden(glow_box, !on);
    }
  }
  // Called at 10 Hz; steps at 10 Hz when fast, else 5 Hz (C2 §4).
  void step(uint32_t k) {
    if (!glow_on || (!fast && (k & 1))) return;
    phase += (fast ? 0.1f : 0.2f) / period;
    phase -= floorf(phase);
    float b = (1.0f - cosf(2.0f * float(M_PI) * phase)) / 2.0f;  // 0 -> 1 -> 0
    int opa = (int) lroundf(255.0f * (0.08f + 0.28f * b));         // 8% -> 36% -> 8%
    set_arc_opa(glow, glow_opa, opa, LV_PART_INDICATOR);
    ++glow_steps;
  }
  // The red breath: one 2 s cycle on the 10 Hz clock, full -> ~58 % -> full. A style step
  // redraws the whole 128 px fill (~27 ms), so tick10 grants turns at 5 Hz in total.
  void breathe(uint32_t k, bool turn) {
    if (!pulse_on || !turn) return;
    float b = (1.0f + cosf(2.0f * float(M_PI) * (k % 20) / 20.0f)) / 2.0f;  // 1 -> 0 -> 1
    set_arc_opa(fill, fill_opa, (int) lroundf(255.0f * (0.58f + 0.42f * b)), LV_PART_INDICATOR);
    ++pulse_steps;
  }
};

// ---- one gauge: title, two rings, centre text, weekly line, chip row ----
struct Gauge {
  int cx = 0;
  Ring five, week;
  lv_obj_t *title, *pct, *sub, *sub_value, *wk, *plus, *chips, *auth, *limit, *paused, *age;
  std::string t_title, t_pct, t_sub, t_wk, t_age;
  uint32_t c_pct = 0, c_wk = 0, c_sub = 0x788695;
  const lv_font_t *pct_font = nullptr;
  bool auth_bad = false;

  // C4b: the 5h line is 16 px (the owner's choice over the guideline's 20 px) and sits in the rings'
  // bottom gap, low enough that its top corners clear the arcs' 45° end cuts by >= 3 px at the
  // widest text: clearance = (ink top - cy - w/2) / sqrt(2). Below cy + 46 px there is no arc.
  // The label box starts above the ink (leading), so it is placed by the ink of 'h'.
  static int ink_top(const lv_font_t *f, uint32_t ch) {
    lv_font_glyph_dsc_t g;
    if (!lv_font_get_glyph_dsc(f, &g, ch, 0)) return 0;
    return std::max(0, int(lv_font_get_line_height(f) - f->base_line) - (g.ofs_y + g.box_h));
  }
  void align_sub() {
    lv_obj_update_layout(sub);
    lv_obj_update_layout(sub_value);
    int prefix = lv_obj_get_width(sub), value = lv_obj_get_width(sub_value);
    int left = cx - (prefix + value) / 2;
    lv_obj_align(sub, LV_ALIGN_TOP_LEFT, left, sub_y);
    lv_obj_align(sub_value, LV_ALIGN_TOP_LEFT, left + prefix, sub_y);
  }
  void place_sub(int cy) {
    // Measure the actual split widgets, including the prefix's trailing space.
    lv_obj_update_layout(sub);
    int prefix = lv_obj_get_width(sub), w = 0;
    auto measure = [&](const std::string &text) {
      lv_label_set_text(sub_value, text.c_str());
      lv_obj_update_layout(sub_value);
      w = std::max(w, prefix + int(lv_obj_get_width(sub_value)));
    };
    for (int minutes = 1; minutes <= 300; ++minutes)
      measure(countdown(minutes * 60, 0));
    measure("now"); measure("--");
    lv_label_set_text(sub_value, "--");
    sub_ink = ink_top(f16, 'h');
    int y = cy + (w + 1) / 2 + 5 - sub_ink;
    sub_base = y + lv_font_get_line_height(f16) - f16->base_line;
    wk_ink = 172 + ink_top(f20, 'k');
    sub_w = w;
    sub_y = y;
    sub_h = lv_font_get_line_height(f16);
    align_sub();
  }
  void build(lv_obj_t *p, int x, const char *name) {
    cx = x;
    const int cy = 118;
    title = mk_label(p, f20, WHITE, cx, 26, name);
    lv_obj_set_width(title, 148); lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    five.build(p, cx, cy, 64, 13);
    week.build(p, cx, cy, 47, 10);
    pct = mk_label(p, f32, GREY, cx, 98, "--");
    sub = mk_label(p, f16, GREY, cx, 146, "5h ");
    sub_value = mk_label(p, f16, GREY, cx, 146, "--");
    place_sub(cy);
    wk = mk_label(p, fcompact, GREY, cx, 172, "wk --");
    plus = mk_label(p, fcompact, VIOLET, cx, 172, " +");
    set_hidden(plus, true);
    chips = mk_box(p);
    lv_obj_set_pos(chips, cx - 80, 194);
    lv_obj_set_size(chips, 160, 26);
    lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chips, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(chips, 4, LV_PART_MAIN);
    auth = mk_chip(chips, WHITE, RED, true);
    lv_label_set_text(auth, "AUTH");
    limit = mk_chip(chips, RED, RED, false);
    lv_label_set_text(limit, "limit");
    lv_obj_set_style_text_font(limit, f16, LV_PART_MAIN);
    paused = mk_chip(chips, GREY, GREY, false);
    lv_label_set_text(paused, "paused");
    age = mk_chip(chips, GREY, BG, true);
    lv_obj_set_style_text_font(age, f16, LV_PART_MAIN);
  }
  void update(const CydPlan &p, int64_t now, int st, bool blink_on) {
    st = p.state;
    set_text(title, t_title, p.label.empty() ? "Waiting" : p.label);
    bool data = st != WAITING, grey = st == STALE;
    bool dim = !grey && (p.snoozed || p.old);
    bool d5 = data && cyd_donated(p.five);
    bool dw = data && cyd_donated(p.weekly);
    char b[32];
    float u5 = p.five.inferred ? 0.0f : p.five.used;
    uint32_t c5 = grey ? GREY : (p.limit_reached ? RED : fill_colour(u5, p.five.cap));
    bool g5 = data && !grey && !p.five.inferred && !dim && !std::isnan(u5) && u5 > 0;
    five.update({data, p.five.inferred, dim, grey, p.limit_reached, g5, true, u5, p.five.cap,
                 urgency(p.five.resets, now, 5 * 3600.0f), c5, d5});
    float uw = p.weekly.inferred ? 0.0f : p.weekly.used;
    uint32_t cw = grey ? GREY : fill_colour(uw, p.weekly.cap);
    bool gw = data && !grey && !p.weekly.inferred && !dim && !std::isnan(uw) && uw > 0;
    week.update({data, p.weekly.inferred, dim, grey, false, gw, false, uw, p.weekly.cap,
                 urgency(p.weekly.resets, now, 7 * 86400.0f), cw, dw, false});
    std::string s;
    if (!data) s = "--";
    else if (p.five.inferred) s = "0%*";
    else if (std::isnan(u5)) s = "--%";
    else { snprintf(b, sizeof b, "%.0f%%", u5); s = b; }
    set_text(pct, t_pct, s);
    const lv_font_t *f = s.size() >= 4 ? f28 : f32;
    if (f != pct_font) { pct_font = f; lv_obj_set_style_text_font(pct, f, LV_PART_MAIN); }
    set_text_color(pct, c_pct, (!data || p.five.inferred) ? GREY : c5);
    std::string value = data ? countdown(p.five.resets, now) : std::string("--");
    if (value != t_sub) {
      set_text(sub_value, t_sub, value);
      align_sub();
    }
    set_text_color(sub_value, c_sub, cyd_countdown::colour(
        p.five.resets, now, st == FRESH, p.five.inferred, p.snoozed));
    if (!data) s = "wk --";
    else {
      std::string n;
      if (p.weekly.inferred) n = "0%*";
      else if (std::isnan(uw)) n = "--%";
      else { snprintf(b, sizeof b, "%.0f%%", uw); n = b; }
      s = "wk " + n + " " + countdown(p.weekly.resets, now);
    }
    bool donated = d5 || dw;
    if (s != t_wk || lv_obj_has_flag(plus, LV_OBJ_FLAG_HIDDEN) == donated) {
      set_text(wk, t_wk, s);
      set_hidden(plus, !donated);
      lv_obj_update_layout(wk);
      lv_obj_update_layout(plus);
      int w = lv_obj_get_width(wk), extra = donated ? lv_obj_get_width(plus) : 0;
      lv_obj_align(wk, LV_ALIGN_TOP_LEFT, cx - (w + extra) / 2, 172);
      lv_obj_align(plus, LV_ALIGN_TOP_LEFT, cx - (w + extra) / 2 + w, 172);
    }
    set_text_color(wk, c_wk, (!data || p.weekly.inferred) ? GREY : cw);
    auth_bad = data && !p.auth.empty() && p.auth != "ok";
    set_hidden(auth, !auth_bad || !blink_on);
    bool at_limit = cyd_limit_chip(p);
    set_hidden(limit, !at_limit);
    set_hidden(paused, !(data && p.snoozed));
    // AUTH, limit and an age chip together are wider than the 160 px row: the age chip steps
    // aside (the rings stay dim) so nothing is clipped or runs into the other gauge.
    set_hidden(age, !(data && !grey && p.old) || (at_limit && auth_bad));
    if (data && !grey && p.old) set_text(age, t_age, cyd_measurement_age(p.measured_at, now));
    bool has_week = !std::isnan(p.weekly.used);
    set_hidden(week.track, !has_week); set_hidden(wk, !has_week);
    if (!has_week) set_hidden(plus, !d5);
  }
  void step(uint32_t k, bool turn) {
    five.step(k);
    week.step(k);
    five.breathe(k, turn);
    if (auth_bad) set_hidden(auth, k % 10 >= 7);
  }
};

static Gauge gauges[2];
static lv_obj_t *updated = nullptr, *waiting = nullptr, *spinner = nullptr, *nodata = nullptr, *band = nullptr;
static std::string t_updated, t_waiting, t_nodata;
static int state = WAITING, band_y = 0, spin = 0;

inline void build(lv_obj_t *root, const lv_font_t *a16, const lv_font_t *a20, const lv_font_t *a24,
                  const lv_font_t *a28, const lv_font_t *a32) {
  f16 = a16; f20 = a20; f24 = a24; f28 = a28; f32 = a32;
  lv_obj_set_size(root, 320, 240);
  lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  gauges[0].build(root, cyd_usage_data::count() == 1 ? 160 : 80, "Waiting");
  if (cyd_usage_data::count() == 2) gauges[1].build(root, 240, "Waiting");
  // C4b: the bottom lines keep their whole glyph box (descenders too) on screen at drift +3 px.
  upd_h = lv_font_get_line_height(f16);
  upd_y = 240 - 3 - upd_h;
  updated = mk_label(root, f16, 0xAAB7C4, 160, upd_y, "");
  lv_obj_add_flag(updated, LV_OBJ_FLAG_HIDDEN);
  waiting = mk_label(root, f20, GREY, 172, 196, "connecting...");
  spinner = mk_arc(root, 34, 196, 22, 3);
  lv_obj_set_style_arc_color(spinner, lv_color_hex(TRACK), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(spinner, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_arc_color(spinner, lv_color_hex(GREY), LV_PART_INDICATOR);
  lv_arc_set_bg_angles(spinner, 0, 360);
  lv_arc_set_angles(spinner, 0, 90);
  nod_h = lv_font_get_line_height(f24);
  nod_y = 240 - 3 - nod_h;
  nodata = mk_label(root, f24, AMBER, 160, nod_y, "NO DATA");
  lv_obj_add_flag(nodata, LV_OBJ_FLAG_HIDDEN);
  band = mk_box(root);
  lv_obj_set_size(band, 320, 8);
  lv_obj_set_style_bg_color(band, lv_color_hex(WHITE), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(band, 89, LV_PART_MAIN);  // 35 % white (C2 §4: ~11 % CPU at 10 Hz)
  lv_obj_add_flag(band, LV_OBJ_FLAG_HIDDEN);
}

// Called every second and after each parsed payload: countdowns, states, chips.
inline void render(int64_t now, int st, uint32_t msg_age, bool connected) {
  if (!updated) return;
  state = st;
  bool blink_on = (millis() / 1000) & 1;
  for (int i = 0; i < cyd_usage_data::count(); i++) gauges[i].update(cyd_usage_plans[i], now, st, blink_on);
  char b[40];
  set_hidden(updated, st != FRESH);
  if (st == FRESH) { snprintf(b, sizeof b, "updated %um ago", unsigned(msg_age / 60)); set_text(updated, t_updated, b); }
  set_hidden(waiting, st != WAITING);
  set_hidden(spinner, st != WAITING);
  if (st == WAITING) set_text(waiting, t_waiting, "waiting for data");
  set_hidden(nodata, st != STALE);
  set_hidden(band, st != STALE);
  if (st == STALE) { snprintf(b, sizeof b, "NO DATA - %um", unsigned(msg_age / 60)); set_text(nodata, t_nodata, b); }
}

// Called at 10 Hz: the only animation clock (no lv_anim, C2 §4). Skipped when the page is
// not on screen.
inline void tick10(bool visible) {
  if (!updated || !visible) return;
  uint32_t k = ++ticks;
  // Breath turns: every 2nd tick; with two red gauges they take those turns in alternation,
  // so at most one fill arc is redrawn per 200 ms.
  int reds = 0;
  for (int i = 0; i < cyd_usage_data::count(); ++i) reds += gauges[i].five.pulse_on;
  for (int i = 0; i < cyd_usage_data::count(); ++i)
    gauges[i].step(k, (k & 1) == 0 && (reds < 2 || int((k >> 1) & 1) == i));
  if (state == STALE) {
    band_y = (band_y + 4) % 240;  // a 6 s sweep
    lv_obj_set_y(band, band_y);
    ++band_steps;
    float b = (1.0f - cosf(2.0f * float(M_PI) * (k % 20) / 20.0f)) / 2.0f;  // 2 s pulse
    lv_obj_set_style_text_opa(nodata, (lv_opa_t) lroundf(255.0f * (0.35f + 0.65f * b)), LV_PART_MAIN);
  } else if (state == WAITING && k % 5 == 0) {
    spin = (spin + 30) % 360;  // slow spinner: 2 steps/s, one turn in 6 s
    lv_arc_set_rotation(spinner, spin);
  }
}

inline int glow_opa(int g, int ring) {
  const Ring &r = ring ? gauges[g].week : gauges[g].five;
  return r.glow_on ? std::max(0, r.glow_opa) : 0;
}

// ---- registry hooks (manifest.yaml) ----
// build: the hidden font holders are the first children of usage_root (page.yaml):
// 16, 20, 24, 28 and 32 px, then the 20 px condensed face.
inline void build_page(lv_obj_t *root) {
  auto font = [&](int i) { return lv_obj_get_style_text_font(lv_obj_get_child(root, i), LV_PART_MAIN); };
  fcompact = font(5);
  build(root, font(0), font(1), font(2), font(3), font(4));
}

inline void status(JsonObject root) {
  root["glow_steps"] = glow_steps;
  auto g = root["glow_opa"].to<JsonArray>();
  for (int i = 0; i < 2 * cyd_usage_data::count(); i++) g.add(glow_opa(i / 2, i % 2));
  root["band_steps"] = band_steps;
  // pulse: the breathing fill's opacity per gauge, 0 when that gauge is not over its cap.
  auto u = root["pulse"].to<JsonArray>();
  for (int i = 0; i < cyd_usage_data::count(); i++)
    u.add(gauges[i].five.pulse_on ? std::max(0, gauges[i].five.fill_opa) : 0);
  root["pulse_steps"] = pulse_steps;
}

// The `selftest` command: the countdown colours and the measured C4b layout. Serial only.
inline void selftest() {
  for (int i = 0; i <= 4; ++i)
    ESP_LOGI("cyd", "C7b RAINBOW p=%.2f rgb=%06X", i / 4.0f, unsigned(cyd_countdown::rainbow(i / 4.0f)));
  ESP_LOGI("cyd", "LAYOUT 5h label w=%d h=%d top=%d ink %d..%d (ring centre 118: clearance %.1f px), wk ink top %d; updated top=%d h=%d; nodata top=%d h=%d",
           sub_w, sub_h, sub_y, sub_y + sub_ink, sub_base, (sub_y + sub_ink - 118 - sub_w / 2.0f) / 1.41421f, wk_ink,
           upd_y, upd_h, nod_y, nod_h);
}

}  // namespace cyd_usage
