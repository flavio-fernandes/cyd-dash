#pragma once
#include "esphome.h"
#include "history_model.h"
#include <string>
namespace cyd_history {
using cyd_history_model::History;
static History history;
static lv_obj_t *chart, *dot, *ring, *shimmer, *title, *hero, *unit, *trend, *high_label, *low_label, *status_label, *left_tick, *mid_tick, *placeholder, *hint;
static std::string texts[12], state = "waiting";
static uint32_t seen_at = 0, clock_at = 0, drawn_revision = UINT32_MAX, status_colour = 0, trend_colour = 0, dot_colour = 0xFFFFFF;
static uint64_t clock_ms = 0;
static int64_t epoch_now = -1, epoch_at = 0, drawn_end = INT64_MIN;
static int64_t gap_limit = 120;
static bool seeded = false, stale = false, was_stale = false, was_visible = false, had_data = false;
static int trend_dir = 2;  // 1 rising, -1 falling, 0 steady, 2 none
static float warn = NAN, alert = NAN, data_low = 0, data_high = 1;
static cyd_history_model::Scale axis{0, 1, 1};
// The graph is the page: edge to edge inside the drift margins, more than half the height.
static constexpr int X = 10, Y = 80, W = 300, H = 130, HERO_X = 10, HERO_Y = 22;
// Motion, stepped only by tick10 (the one 100 ms clock) while the page is on screen.
static constexpr int SWEEP_STEPS = 10, PULSE_STEPS = 15, SLIDE_STEPS = 4, RING = 30;
static int sweep = SWEEP_STEPS;  // the graph draws in from the left after the page arrives
static int pulse = 0;            // the ring around the newest reading grows and fades
static int slide = 0;            // frames left while the graph eases one bucket to the left
static float slide_from = 0, slide_px = 0;
static int shimmer_phase = 0;    // the waiting face's soft dotted line
static int trend_opa = 255;
static float shown_value = NAN, target_value = NAN;  // the big number steps towards the reading
static const char *motion = "still";
static uint32_t motion_frames = 0;
inline int64_t now() {
  uint32_t t = millis(); clock_ms += uint32_t(t - clock_at); clock_at = t;
  return clock_ms / 1000;
}
inline uint32_t mix(uint32_t a, uint32_t b, float f) {
  return lv_color_to_u32(lv_color_mix(lv_color_hex(b), lv_color_hex(a), uint8_t(f * 255))) & 0xFFFFFF;
}
// The lowest reading in view is cool blue, the middle is green, the highest is warm violet.
// The warning and alert thresholds take over with amber and red.
inline uint32_t colour(float v) {
  if (stale) return 0x788695;
  if (std::isfinite(alert) && v >= alert) return 0xFF5C6C;
  if (std::isfinite(warn) && v >= warn) return 0xFFB347;
  float f = data_high > data_low ? std::clamp((v - data_low) / (data_high - data_low), 0.0f, 1.0f) : 0.5f;
  return f < 0.5f ? mix(0x4FA8FF, 0x3FDDA0, f * 2) : mix(0x3FDDA0, 0xD78BFF, f * 2 - 1);
}
inline bool set_text(lv_obj_t *o, int i, const std::string &s) {
  if (texts[i] == s) return false;
  texts[i] = s; lv_label_set_text(o, s.c_str()); return true;
}
inline lv_obj_t *label(lv_obj_t *root, const lv_font_t *font, int x, int y, int w, uint32_t color) {
  auto o = lv_label_create(root); lv_obj_set_style_text_font(o, font, 0);
  lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
  lv_obj_set_pos(o, x, y); lv_obj_set_width(o, w); lv_label_set_long_mode(o, LV_LABEL_LONG_DOT);
  lv_label_set_text(o, ""); return o;
}
// A plain, unscrollable box that draws itself in a DRAW_MAIN callback.
inline lv_obj_t *box(lv_obj_t *root, int x, int y, int w, int h) {
  auto o = lv_obj_create(root); lv_obj_remove_style_all(o);
  lv_obj_clear_flag(o, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_pos(o, x, y); lv_obj_set_size(o, w, h); return o;
}
// The y offset that puts a label in `font` on the same baseline as a label in `ref`.
inline int baseline(const lv_font_t *ref, const lv_font_t *font) {
  return (ref->line_height - ref->base_line) - (font->line_height - font->base_line);
}
// Friendly axis labels: -2h, -30m, -24h.
inline std::string ago(int seconds) {
  char b[16];
  if (seconds % 3600 == 0) snprintf(b, sizeof b, "-%dh", seconds / 3600);
  else if (seconds % 60 == 0 || seconds >= 600) snprintf(b, sizeof b, "-%dm", (seconds + 30) / 60);
  else snprintf(b, sizeof b, "-%ds", seconds);
  return b;
}
// While the graph slides, everything sits slide_px to the right of its place.
inline int px(int64_t t) {
  return std::clamp(int((t - (drawn_end - history.window)) * (W - 1) / history.window + std::lround(slide_px)), 0, W - 1);
}
inline int py(float v) {
  double fraction = (double(axis.high) - v) / (double(axis.high) - axis.low);
  return int(std::lround(std::clamp(fraction, 0.0, 1.0) * (H - 1)));
}
// How much of the graph is drawn: it eases out over the sweep, then it is the whole width.
inline int reveal(int step) {
  float f = 1 - float(step) / SWEEP_STEPS;
  return int(std::lround(W * (1 - f * f)));
}
inline void line(lv_layer_t *layer, int x1, int y1, int x2, int y2, uint32_t c, int width) {
  lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d);
  d.p1 = {x1, y1}; d.p2 = {x2, y2};
  d.color = lv_color_hex(c); d.width = width; d.round_start = d.round_end = 1;
  lv_draw_line(layer, &d);
}
inline void fill(lv_layer_t *layer, int x1, int y1, int x2, int y2, uint32_t c, lv_opa_t top, lv_opa_t bottom) {
  lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
  d.bg_color = lv_color_hex(c); d.bg_opa = LV_OPA_COVER;
  d.bg_grad.dir = LV_GRAD_DIR_VER; d.bg_grad.stops_count = 2;
  d.bg_grad.stops[0].color = d.bg_grad.stops[1].color = lv_color_hex(c);
  d.bg_grad.stops[0].opa = top; d.bg_grad.stops[0].frac = 0;
  d.bg_grad.stops[1].opa = bottom; d.bg_grad.stops[1].frac = 255;
  lv_area_t area{x1, y1, x2, y2}; lv_draw_rect(layer, &d, &area);
}
// Walks the smoothed curve. The area pass paints every column exactly once (a column shared
// by two segments belongs to the later one). Each column starts at an opacity proportional to
// its height, so all columns together form one smooth vertical gradient. Only columns lo..hi
// are drawn: the part being redrawn, up to the sweep's edge.
inline void trace(lv_layer_t *layer, const lv_area_t &a, bool area, int lo, int hi) {
  for (int i = 1; i < history.count; ++i) {
    const auto &p = history.points[i - 1], &q = history.points[i];
    // Empty time buckets stay empty rather than implying uninterrupted measurements.
    if (q.time - p.time > gap_limit) continue;
    bool last = i + 1 == history.count || history.points[i + 1].time - q.time > gap_limit;
    int x0 = px(p.time), x1 = px(q.time), old_y = py(p.value);
    if (x1 < lo || x0 > hi + 1) continue;
    for (int x = x0; x <= x1; ++x) {
      float t = x1 == x0 ? 1 : float(x - x0) / (x1 - x0);
      float v = p.value + (q.value - p.value) * t * t * (3 - 2 * t);
      int y = py(v);
      if (x >= lo && x <= hi) {
        uint32_t c = colour(v);
        if (area && (x < x1 || last)) fill(layer, a.x1 + x, a.y1 + y, a.x1 + x, a.y2, c, (stale ? 90 : 210) * (H - 1 - y) / (H - 1), 0);
        if (!area && x > x0) line(layer, a.x1 + x - 1, a.y1 + old_y, a.x1 + x, a.y1 + y, c, 3);
      }
      old_y = y;
    }
  }
}
inline void draw(lv_event_t *e) {
  auto layer = lv_event_get_layer(e); lv_area_t a; lv_obj_get_coords(chart, &a);
  for (int k = 0; k < 5; ++k) {
    int y = a.y1 + k * (H - 1) / 4;
    line(layer, a.x1, y, a.x2, y, k == 4 ? 0x2B3D4A : 0x1C2A35, 1);
  }
  // A small redraw (a sweep strip, the pulse ring) only walks the columns it can see.
  const lv_area_t &clip = layer->_clip_area;
  int lo = std::max(0, int(clip.x1 - a.x1) - 2), hi = std::min({W - 1, int(clip.x2 - a.x1) + 2, reveal(sweep) - 1});
  if (lo > hi) return;
  trace(layer, a, true, lo, hi);
  trace(layer, a, false, lo, hi);
}
// A small filled triangle; the text fonts have no solid arrow.
inline void draw_trend(lv_event_t *e) {
  if (trend_dir == 2) return;
  auto layer = lv_event_get_layer(e); lv_area_t a; lv_obj_get_coords(trend, &a);
  int cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2;
  if (trend_dir == 0) { fill(layer, cx - 6, cy - 1, cx + 6, cy + 1, trend_colour, LV_OPA_COVER, LV_OPA_COVER); return; }
  for (int r = 0; r < 8; ++r) {
    int y = trend_dir > 0 ? cy - 4 + r : cy + 3 - r;
    fill(layer, cx - r, y, cx + r, y, trend_colour, LV_OPA_COVER, LV_OPA_COVER);
  }
}
// The pulse: a ring that grows out of the dot and fades, in the dot's colour.
inline void draw_ring(lv_event_t *e) {
  auto layer = lv_event_get_layer(e); lv_area_t a; lv_obj_get_coords(ring, &a);
  float f = float(pulse) / PULSE_STEPS;
  int cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2, r = 6 + int(f * (RING / 2 - 8));
  lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
  d.bg_opa = LV_OPA_TRANSP; d.radius = LV_RADIUS_CIRCLE;
  d.border_width = 2; d.border_color = lv_color_hex(dot_colour); d.border_opa = lv_opa_t(200 * (1 - f));
  lv_area_t area{cx - r, cy - r, cx + r, cy + r}; lv_draw_rect(layer, &d, &area);
}
// Waiting: a soft dotted line with a glint that drifts slowly from left to right.
inline void draw_shimmer(lv_event_t *e) {
  auto layer = lv_event_get_layer(e); lv_area_t a; lv_obj_get_coords(shimmer, &a);
  int glint = (shimmer_phase % 40) * 10 - 50;
  for (int x = 5; x < W; x += 10) {
    float near = std::max(0.0f, 1 - std::abs(x - glint) / 60.0f);
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(mix(0x4A6070, 0x4FA8FF, near)); d.bg_opa = LV_OPA_COVER; d.radius = 2;
    lv_area_t dotted{a.x1 + x - 2, a.y1 + 1, a.x1 + x + 1, a.y1 + 4}; lv_draw_rect(layer, &d, &dotted);
  }
}
// Only the ring's small square is redrawn, never the whole graph.
inline void invalidate_dot() { if (ring) lv_obj_invalidate(ring); }
// The dot and the ring follow the newest reading; both wait until the sweep has drawn it.
inline void place_dot() {
  if (!history.count || sweep < SWEEP_STEPS) {
    lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN); return;
  }
  auto p = history.points[history.count - 1];
  int x = X + px(p.time), y = Y + py(p.value);
  lv_obj_set_pos(dot, x - 5, y - 5); lv_obj_set_pos(ring, x - RING / 2, y - RING / 2);
  uint32_t c = colour(p.value);
  if (c != dot_colour) { dot_colour = c; lv_obj_set_style_bg_color(dot, lv_color_hex(c), 0); invalidate_dot(); }
  lv_obj_remove_flag(dot, LV_OBJ_FLAG_HIDDEN);
  // Stale: the motion stops, so the ring goes away.
  if (stale) lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(ring, LV_OBJ_FLAG_HIDDEN);
}
inline void start_sweep() {
  sweep = 0; slide = 0; slide_px = 0; pulse = 0;
  lv_obj_invalidate(chart); place_dot();
}
// The unit sits right after the number, and the trend right after the unit.
inline void place_header() {
  lv_obj_update_layout(hero);
  int x = HERO_X + lv_obj_get_width(hero) + 4;
  lv_obj_set_x(unit, x);
  lv_obj_set_x(trend, x + lv_obj_get_width(unit) + 6);
}
inline void show_hero() {
  char b[16];
  cyd_history_model::format_value(b, sizeof b, shown_value);
  if (set_text(hero, 1, b)) place_header();
}
inline void ingest() {
  int64_t t = now();
  history.configure(cyd_slots::number(CYD_SLOT_history_window), cyd_slots::number(CYD_SLOT_history_period));
  const auto &seed_text = cyd_slots::text(CYD_SLOT_history_seed);
  static uint32_t seed_at = 0;
  static bool tried_with_time = false;
  uint32_t incoming_seed_at = cyd_slots::store.values[CYD_SLOT_history_seed].at;
  if (!seeded && !seed_text.empty() && (seed_at != incoming_seed_at || (!tried_with_time && epoch_now >= 0))) {
    seed_at = incoming_seed_at; tried_with_time = epoch_now >= 0;
    seeded = cyd_history_model::seed(history, seed_text.c_str(), t, epoch_now < 0 ? -1 : epoch_now + t - epoch_at);
    // Seed application must not displace a live value already received.
    if (seeded) seen_at = 0;
  }
  const auto &v = cyd_slots::store.values[CYD_SLOT_history_value];
  if (v.valid && v.at != seen_at) {
    seen_at = v.at;
    history.add(t - uint32_t(millis() - v.at) / 1000, v.number);
  }
  history.expire(t);
}
inline void build(lv_obj_t *root) {
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  auto f16 = lv_obj_get_style_text_font(lv_obj_get_child(root, 0), LV_PART_MAIN);
  auto f20 = lv_obj_get_style_text_font(lv_obj_get_child(root, 1), LV_PART_MAIN);
  auto f48 = lv_obj_get_style_text_font(lv_obj_get_child(root, 2), LV_PART_MAIN);
  // One header band: title and status on the first line, then the reading with its unit
  // and trend, with Low/High stacked on the right.
  title = label(root, f20, 12, 4, 160, 0xFFFFFF);
  status_label = label(root, f16, 176, 4 + baseline(f20, f16), 132, 0xAAB7C4);
  lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_RIGHT, 0);
  hero = label(root, f48, HERO_X, HERO_Y, LV_SIZE_CONTENT, 0xFFFFFF);
  lv_obj_set_style_max_width(hero, 165, 0);
  // LONG_DOT needs a one-line height as well as a width bound: never spill into the graph.
  lv_obj_set_height(hero, f48->line_height);
  int unit_y = HERO_Y + baseline(f48, f20);
  unit = label(root, f20, HERO_X, unit_y, LV_SIZE_CONTENT, 0xAAB7C4);
  lv_obj_set_style_max_width(unit, 60, 0);
  trend = box(root, 0, unit_y + (f20->line_height - f20->base_line) - 15, 16, 12);
  lv_obj_add_event_cb(trend, draw_trend, LV_EVENT_DRAW_MAIN, nullptr);
  high_label = label(root, f16, 190, 30, 118, 0xAAB7C4);
  low_label = label(root, f16, 190, 52, 118, 0xAAB7C4);
  lv_obj_set_style_text_align(high_label, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_align(low_label, LV_TEXT_ALIGN_RIGHT, 0);
  chart = box(root, X, Y, W, H);
  lv_obj_add_event_cb(chart, draw, LV_EVENT_DRAW_MAIN, nullptr);
  ring = box(root, X, Y, RING, RING);
  lv_obj_add_event_cb(ring, draw_ring, LV_EVENT_DRAW_MAIN, nullptr);
  lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN);
  dot = box(root, X, Y, 10, 10);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(dot, lv_color_hex(dot_colour), 0);
  lv_obj_set_style_border_width(dot, 2, 0); lv_obj_set_style_border_color(dot, lv_color_hex(0xFFFFFF), 0);
  lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
  placeholder = label(root, f20, X, Y + 38, W, 0xAAB7C4);
  hint = label(root, f16, X, Y + 68, W, 0xAAB7C4);
  lv_obj_set_style_text_align(placeholder, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
  shimmer = box(root, X, Y + 104, W, 6);
  lv_obj_add_event_cb(shimmer, draw_shimmer, LV_EVENT_DRAW_MAIN, nullptr);
  left_tick = label(root, f16, X, Y + H + 3, 100, 0xAAB7C4);
  mid_tick = label(root, f16, X + W / 2 - 50, Y + H + 3, 100, 0xAAB7C4);
  lv_obj_set_style_text_align(mid_tick, LV_TEXT_ALIGN_CENTER, 0);
  auto right_tick = label(root, f16, X + W - 60, Y + H + 3, 60, 0xAAB7C4);
  lv_obj_set_style_text_align(right_tick, LV_TEXT_ALIGN_RIGHT, 0); lv_label_set_text(right_tick, "now");
}
inline void render(const cyd_page::Ctx &ctx) {
  epoch_now = ctx.now; epoch_at = now(); ingest();
  if (!ctx.visible) return;
  int64_t t = now(), age = history.last == INT64_MIN ? -1 : std::max<int64_t>(0, t - history.last);
  stale = age >= std::max(180, history.period * 3);
  state = !history.count ? "waiting" : stale ? "stale" : "fresh";
  set_text(title, 0, cyd_slots::valid(CYD_SLOT_history_label) ? cyd_slots::text(CYD_SLOT_history_label) : "History");
  char b[96];
  float latest = history.count ? history.points[history.count - 1].value : NAN;
  // tick10 steps the big number towards a new reading; the first one, or a stale one, shows at once.
  target_value = latest;
  if (!std::isfinite(shown_value) || !std::isfinite(latest) || stale) shown_value = latest;
  if (set_text(unit, 2, cyd_slots::text(CYD_SLOT_history_unit))) place_header();
  show_hero();
  float low, high; history.range(low, high);
  char reading[24];
  if (history.count) {
    cyd_history_model::format_value(reading, sizeof reading, high);
    snprintf(b, sizeof b, "High %s", reading);
  } else b[0] = 0;
  set_text(high_label, 3, b);
  if (history.count) {
    cyd_history_model::format_value(reading, sizeof reading, low);
    snprintf(b, sizeof b, "Low %s", reading);
  } else b[0] = 0;
  set_text(low_label, 4, b);
  set_text(placeholder, 5, history.count ? "" : "Waiting for a reading");
  set_text(hint, 6, history.count ? "" : "Listening for your topic");
  if (history.count) lv_obj_add_flag(shimmer, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(shimmer, LV_OBJ_FLAG_HIDDEN);
  int dir = 2;
  if (history.count > 1 && !stale) {
    float d = history.points[history.count - 1].value - history.points[history.count - 2].value;
    dir = d > 0.05f ? 1 : d < -0.05f ? -1 : 0;
  }
  set_text(left_tick, 7, ago(history.window));
  set_text(mid_tick, 8, ago(history.window / 2));
  float next_warn = cyd_slots::number(CYD_SLOT_history_warn), next_alert = cyd_slots::number(CYD_SLOT_history_alert);
  bool alerting = std::isfinite(next_alert) && latest >= next_alert, warming = std::isfinite(next_warn) && latest >= next_warn;
  std::string band = alerting ? "Alert" : warming ? "Warm" : "Live";
  set_text(status_label, 9, !history.count ? "Waiting" : stale ? "Stale - " + cyd_slots::dur(age) + " ago" : band + " - " + cyd_slots::dur(age) + " ago");
  uint32_t next_status = !history.count || stale ? 0xAAB7C4 : alerting ? 0xFF8A96 : warming ? 0xFFC870 : 0x7FE3B8;
  if (next_status != status_colour) { status_colour = next_status; lv_obj_set_style_text_color(status_label, lv_color_hex(next_status), 0); }
  auto next_axis = cyd_history_model::scale(low, high, cyd_slots::number(CYD_SLOT_history_min), cyd_slots::number(CYD_SLOT_history_max));
  int64_t end = cyd_history_model::bucket(t, history.period);
  auto equal = [](float a, float b) { return a == b || (std::isnan(a) && std::isnan(b)); };
  if (drawn_revision != history.revision || end != drawn_end || stale != was_stale || next_axis.low != axis.low || next_axis.high != axis.high || !equal(warn, next_warn) || !equal(alert, next_alert)) {
    if (drawn_revision != history.revision) gap_limit = history.gap_limit();
    // A new time bucket while the graph is live: ease it one step to the left.
    if (drawn_end != INT64_MIN && end - drawn_end == history.period && !stale && sweep >= SWEEP_STEPS && history.count) {
      slide_from = slide_px = float(W - 1) * history.period / history.window; slide = SLIDE_STEPS;
    }
    drawn_revision = history.revision; drawn_end = end; was_stale = stale; axis = next_axis; warn = next_warn; alert = next_alert;
    data_low = low; data_high = high;
    lv_obj_invalidate(chart);
    if (history.count && !had_data) start_sweep();
    place_dot();
  }
  had_data = history.count > 0;
  uint32_t next_trend = history.count ? colour(latest) : 0;
  if (dir != trend_dir || next_trend != trend_colour) {
    // A new direction fades in over a few frames; a colour change alone repaints at once.
    if (dir != trend_dir && dir != 2) { trend_opa = 0; lv_obj_set_style_opa(trend, 0, 0); }
    trend_dir = dir; trend_colour = next_trend; lv_obj_invalidate(trend);
  }
}
// Each step redraws a small area: a strip of the sweep, the ring, the dotted line, the number.
// Only the one-bucket slide repaints the graph, for SLIDE_STEPS frames.
inline void tick10(bool visible) {
  ingest();
  bool arrived = visible && !was_visible;
  was_visible = visible;
  if (!visible) { motion = "still"; return; }
  if (arrived && history.count) start_sweep();
  const char *next = "still";
  if (!history.count) {
    ++shimmer_phase; lv_obj_invalidate(shimmer); next = "waiting";
  } else if (sweep < SWEEP_STEPS) {
    int from = reveal(sweep), to = reveal(++sweep);
    lv_area_t a; lv_obj_get_coords(chart, &a);
    lv_area_t strip{a.x1 + from - 3, a.y1, a.x1 + to + 2, a.y2};
    lv_obj_invalidate_area(chart, &strip);
    if (sweep >= SWEEP_STEPS) place_dot();
    next = "sweep";
  } else if (!stale) {
    if (slide > 0) {
      --slide; float f = float(slide) / SLIDE_STEPS; slide_px = slide_from * f * f;
      lv_obj_invalidate(chart); place_dot(); next = "slide";
    } else next = "pulse";
    pulse = (pulse + 1) % PULSE_STEPS; invalidate_dot();
  }
  if (!stale && std::isfinite(target_value) && shown_value != target_value) {
    shown_value = std::fabs(target_value - shown_value) < 0.08f ? target_value : shown_value + (target_value - shown_value) * 0.35f;
    show_hero();
  }
  if (trend_opa < 255) { trend_opa = std::min(255, trend_opa + 85); lv_obj_set_style_opa(trend, trend_opa, 0); }
  motion = next;
  if (strcmp(next, "still") != 0) ++motion_frames;
}
// The whole screen's drawing cost (busy, refr_ms, inv_px) is in the core status fields.
inline void status(JsonObject root) {
  root["history"]["state"] = state; root["history"]["samples"] = history.count;
  root["history"]["buffer_bytes"] = sizeof history.points;
  root["history"]["motion"] = motion; root["history"]["motion_frames"] = motion_frames;
}
inline void selftest() {
  ESP_LOGI("history", "samples=%d buffer=%u motion=%s busy=%.3f refr_ms=%.3f", history.count, unsigned(sizeof history.points), motion, cyd_perf::busy_pct(), cyd_perf::refr_ms());
#ifdef USE_ESP32
  ESP_LOGI("history", "heap=%u", unsigned(cyd_device::free_heap()));
#else
  ESP_LOGI("history", "heap=n/a-preview");
#endif
}
}  // namespace cyd_history
