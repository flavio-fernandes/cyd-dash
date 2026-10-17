#pragma once
#include "esphome.h"
#include <string>
namespace cyd_stat {
static lv_obj_t *title, *hero, *unit_label, *level_label, *footer, *track, *fill, *warn_mark, *alert_mark;
static std::string texts[5];
static const uint32_t WHITE = 0xFFFFFF, GREY = 0xAAB7C4, TRACK = 0x293B48;
static const uint32_t GREEN = 0x32D583, AMBER = 0xFFB547, RED = 0xF04452;
static const int BAR_X = 28, BAR_Y = 158, BAR_W = 264;
static uint32_t hero_colour = 0, level_colour = 0, fill_colour = 0;
static int fill_shown = -1, fill_target = 0, warn_x = -2, alert_x = -2;
static std::string state = "waiting", level = "none";
inline void text(lv_obj_t *o, int i, const std::string &s) {
  if (texts[i] == s) return;
  texts[i] = s; lv_label_set_text(o, s.c_str());
}
inline void colour(lv_obj_t *o, uint32_t &cache, uint32_t c) {
  if (cache == c) return;
  cache = c; lv_obj_set_style_text_color(o, lv_color_hex(c), 0);
}
inline lv_obj_t *label(lv_obj_t *root, const lv_font_t *font, int y, uint32_t color) {
  auto o = lv_label_create(root);
  lv_obj_set_style_text_font(o, font, 0);
  lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
  lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(o, 300); lv_obj_set_pos(o, 10, y);
  lv_label_set_long_mode(o, LV_LABEL_LONG_DOT); lv_label_set_text(o, "");
  return o;
}
inline lv_obj_t *box(lv_obj_t *root, int x, int y, int w, int h, uint32_t c) {
  auto o = lv_obj_create(root); lv_obj_remove_style_all(o);
  lv_obj_clear_flag(o, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_set_pos(o, x, y); lv_obj_set_size(o, w, h);
  lv_obj_set_style_radius(o, h / 2, 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(c), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  return o;
}
// A text slot that is bound to a topic is empty until its first message: use the default.
inline std::string text_or(int slot, const char *fallback) {
  return cyd_slots::valid(slot) && !cyd_slots::text(slot).empty() ? cyd_slots::text(slot) : fallback;
}
// Where a level sits on the bar, which ends a quarter above the highest level set.
inline int bar_x(float v, float top) { return int(std::lround(std::max(0.0f, std::min(v / top, 1.0f)) * BAR_W)); }
inline void mark(lv_obj_t *o, int &cache, int x) {
  if (cache == x) return;
  cache = x;
  if (x < 0) { lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); return; }
  lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN); lv_obj_set_x(o, BAR_X + x - 1);
}
inline void paint_fill() {
  lv_obj_set_width(fill, std::max(fill_shown, 1));
  if (fill_shown <= 0) lv_obj_add_flag(fill, LV_OBJ_FLAG_HIDDEN); else lv_obj_clear_flag(fill, LV_OBJ_FLAG_HIDDEN);
}
inline void build(lv_obj_t *root) {
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  auto f16 = lv_obj_get_style_text_font(lv_obj_get_child(root, 0), LV_PART_MAIN);
  auto f20 = lv_obj_get_style_text_font(lv_obj_get_child(root, 1), LV_PART_MAIN);
  auto fhero = lv_obj_get_style_text_font(lv_obj_get_child(root, 2), LV_PART_MAIN);
  title = label(root, f20, 12, WHITE);
  hero = label(root, fhero, 50, WHITE);
  unit_label = label(root, f20, 110, GREY);
  track = box(root, BAR_X, BAR_Y, BAR_W, 10, TRACK);
  fill = box(root, BAR_X, BAR_Y, 1, 10, GREEN);
  warn_mark = box(root, 0, BAR_Y - 5, 3, 20, AMBER);
  alert_mark = box(root, 0, BAR_Y - 5, 3, 20, RED);
  lv_obj_add_flag(warn_mark, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(alert_mark, LV_OBJ_FLAG_HIDDEN);
  level_label = label(root, f20, 182, GREY);
  footer = label(root, f16, 218, GREY);
  fill_shown = 0; paint_fill();
}
inline void render(const cyd_page::Ctx &ctx) {
  if (!ctx.visible) return;
  using namespace cyd_slots;
  float value = number(CYD_SLOT_stat_value);
  float warn = number(CYD_SLOT_stat_warn), alert = number(CYD_SLOT_stat_alert);
  float d = number(CYD_SLOT_stat_decimals);
  int decimals = std::isfinite(d) ? std::max(0, std::min(3, int(d))) : 0;
  bool ok = std::isfinite(value);
  int64_t age_s = age_of(CYD_SLOT_stat_value, CYD_SLOT_stat_updated_at, ctx.now);
  if (ok && age(CYD_SLOT_stat_value) > age_s) age_s = age(CYD_SLOT_stat_value);
  bool fresh = ok && age_s >= 0 && age_s < 1200;
  state = !ok ? "waiting" : fresh ? "fresh" : "stale";
  level = !ok ? "none" : std::isfinite(alert) && value > alert ? "alert" : std::isfinite(warn) && value > warn ? "warn" : "normal";
  uint32_t c = level == "alert" ? RED : level == "warn" ? AMBER : GREEN;
  text(title, 0, text_or(CYD_SLOT_stat_label, "Power"));
  char b[48];
  if (ok) snprintf(b, sizeof b, "%.*f", decimals, value); else snprintf(b, sizeof b, "--");
  text(hero, 1, b);
  text(unit_label, 2, text_or(CYD_SLOT_stat_unit, "W"));
  colour(hero, hero_colour, !fresh ? GREY : level == "normal" ? WHITE : c);
  // The word carries the level too, so colour is never the only signal.
  text(level_label, 3, !ok ? "Waiting" : !fresh ? "Stale reading" : level == "alert" ? "Alert" : level == "warn" ? "High" : "Normal");
  colour(level_label, level_colour, fresh ? c : GREY);
  text(footer, 4, !ok ? "Waiting for a value" : !fresh ? "Stale - " + dur(age_s) + " ago" : "Updated " + dur(age_s) + " ago");
  // The bar shows the value against the levels; without any level it stays an empty track.
  float top = std::isfinite(alert) ? alert * 1.25f : std::isfinite(warn) ? warn * 1.25f : NAN;
  bool scaled = std::isfinite(top) && top > 0;
  mark(warn_mark, warn_x, scaled && std::isfinite(warn) ? bar_x(warn, top) : -1);
  mark(alert_mark, alert_x, scaled && std::isfinite(alert) ? bar_x(alert, top) : -1);
  uint32_t fc = fresh ? c : GREY;
  if (fc != fill_colour) { fill_colour = fc; lv_obj_set_style_bg_color(fill, lv_color_hex(fc), 0); }
  fill_target = ok && scaled ? bar_x(value, top) : 0;
  // The first value and stale values jump; later changes glide in tick10.
  if (fill_shown <= 0 || !fresh) { fill_shown = fill_target; paint_fill(); }
}
// Glide the small bar toward a new value in a few steps, then stop.
inline void tick10(bool visible) {
  if (!visible || fill_shown == fill_target) return;
  int step = (fill_target - fill_shown) / 3;
  if (step == 0) step = fill_target > fill_shown ? 1 : -1;
  fill_shown += step; paint_fill();
}
inline void status(JsonObject root) {
  root["stat"]["state"] = state;
  root["stat"]["level"] = level;
}
}  // namespace cyd_stat
