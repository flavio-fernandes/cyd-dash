#pragma once
#include "esphome.h"
#include "climate.h"
#include <string>
namespace cyd_thermostat {
static lv_obj_t *title, *hero, *unit_label, *comfort, *humidity, *target_label, *footer, *trend, *ring, *humidity_ring;
static std::string texts[8];
static const uint32_t GREY = 0xAAB7C4;
static float previous = NAN, target = NAN, shown = NAN;
static int last_angle = -1, last_humidity = -1;
static uint32_t last_colour = 0;
static bool fresh = false;
static std::string state = "waiting";
inline void text(lv_obj_t *o, int i, const std::string &s) {
  if (texts[i] == s) return;
  texts[i] = s; lv_label_set_text(o, s.c_str());
}
inline lv_obj_t *label(lv_obj_t *root, const lv_font_t *font, int x, int y, int width, uint32_t color) {
  auto o = lv_label_create(root);
  lv_obj_set_style_text_font(o, font, 0);
  lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
  lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(o, width); lv_obj_set_pos(o, x, y);
  lv_label_set_long_mode(o, LV_LABEL_LONG_DOT); lv_label_set_text(o, "");
  return o;
}
inline lv_obj_t *arc(lv_obj_t *root, int x, int y, int diameter, int width, uint32_t colour) {
  auto o = lv_arc_create(root); lv_obj_remove_style_all(o);
  lv_obj_clear_flag(o, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_set_pos(o, x, y); lv_obj_set_size(o, diameter, diameter);
  lv_arc_set_rotation(o, 135); lv_arc_set_bg_angles(o, 0, 270);
  lv_obj_set_style_arc_width(o, width, LV_PART_MAIN);
  lv_obj_set_style_arc_width(o, width, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(o, lv_color_hex(0x293B48), LV_PART_MAIN);
  lv_obj_set_style_arc_color(o, lv_color_hex(colour), LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(o, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(o, true, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(o, true, LV_PART_INDICATOR);
  return o;
}
inline void paint() {
  int a = std::isfinite(shown) ? cyd_climate::arc(shown) : 270;
  uint32_t c = fresh && std::isfinite(shown) ? cyd_climate::colour(shown) : GREY;
  if (a != last_angle) { last_angle = a; lv_arc_set_angles(ring, 0, a); }
  if (c != last_colour) { last_colour = c; lv_obj_set_style_arc_color(ring, lv_color_hex(c), LV_PART_INDICATOR); }
}
inline void build(lv_obj_t *root) {
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  auto f16 = lv_obj_get_style_text_font(lv_obj_get_child(root, 0), LV_PART_MAIN);
  auto f20 = lv_obj_get_style_text_font(lv_obj_get_child(root, 1), LV_PART_MAIN);
  auto fhero = lv_obj_get_style_text_font(lv_obj_get_child(root, 2), LV_PART_MAIN);
  title = label(root, f20, 16, 9, 288, 0xFFFFFF);
  ring = arc(root, 12, 43, 166, 8, GREY);
  hero = label(root, fhero, 24, 84, 142, 0xFFFFFF);
  unit_label = label(root, f20, 55, 139, 80, GREY);
  comfort = label(root, f16, 15, 190, 160, GREY);
  humidity_ring = arc(root, 200, 49, 90, 5, 0x70BFFF);
  humidity = label(root, f20, 204, 77, 82, 0xFFFFFF);
  auto caption = label(root, f16, 185, 121, 120, GREY); lv_label_set_text(caption, "Humidity");
  target_label = label(root, f16, 179, 155, 132, GREY);
  trend = label(root, f20, 180, 183, 130, GREY);
  footer = label(root, f16, 8, 219, 304, GREY);
  paint();
}
inline void render(const cyd_page::Ctx &ctx) {
  if (!ctx.visible) return;
  using namespace cyd_slots;
  // A bound unit may not have arrived yet; use the manifest's default until it does.
  std::string unit = valid(CYD_SLOT_thermostat_unit) ? cyd_slots::text(CYD_SLOT_thermostat_unit) : "°C";
  bool fahrenheit = unit == "°F";
  bool unit_ok = fahrenheit || unit == "°C";
  float value = number(CYD_SLOT_thermostat_temperature);
  float c = cyd_climate::celsius(value, fahrenheit);
  bool valid_value = unit_ok && std::isfinite(c) && c >= -50 && c <= 60;
  int64_t age_s = age(CYD_SLOT_thermostat_temperature);
  if (valid(CYD_SLOT_thermostat_updated_at) && ctx.now >= 0)
    age_s = std::max(age_s, ctx.now - epoch(CYD_SLOT_thermostat_updated_at));
  fresh = valid_value && age_s >= 0 && age_s < 1200;
  state = !unit_ok ? "invalid unit" : !valid_value ? "waiting" : fresh ? "fresh" : "stale";
  text(title, 0, cyd_slots::text(CYD_SLOT_thermostat_label));
  char b[48];
  if (valid_value) snprintf(b, sizeof b, "%.1f", value); else snprintf(b, sizeof b, "--.-");
  text(hero, 1, b); text(unit_label, 2, unit_ok ? unit : "?");
  text(comfort, 3, !valid_value ? "Waiting" : !fresh ? "Stale reading" : cyd_climate::band(c));
  float h = number(CYD_SLOT_thermostat_humidity);
  bool h_ok = std::isfinite(h) && h >= 0 && h <= 100 && age(CYD_SLOT_thermostat_humidity) >= 0 && age(CYD_SLOT_thermostat_humidity) < 1200;
  if (h_ok) snprintf(b, sizeof b, "%.0f%%", h); else snprintf(b, sizeof b, "--%%");
  text(humidity, 4, b);
  int ha = h_ok ? int(std::lround(h * 2.7f)) : 0;
  if (ha != last_humidity) { last_humidity = ha; lv_arc_set_angles(humidity_ring, 0, ha); }
  float sp = number(CYD_SLOT_thermostat_setpoint);
  if (unit_ok && std::isfinite(sp)) snprintf(b, sizeof b, "Target %.1f", sp); else b[0] = 0;
  text(target_label, 5, b);
  if (!fresh) { text(trend, 6, ""); previous = NAN; }
  else if (!std::isfinite(previous) || c != previous) {
    const char *arrow = !std::isfinite(previous) ? "" : c > previous + 0.05f ? LV_SYMBOL_UP : c < previous - 0.05f ? LV_SYMBOL_DOWN : "";
    text(trend, 6, arrow); previous = c;
  }
  text(footer, 7, !unit_ok ? "Choose °C or °F" : !valid_value ? "Waiting for temperature" : !fresh ? "Stale - " + cyd_slots::dur(age_s) + " ago" : "Updated " + cyd_slots::dur(age_s) + " ago");
  target = valid_value ? c : NAN;
  // No animation on first data or stale values; later changes settle in bounded steps.
  if (!std::isfinite(shown) || !fresh || !std::isfinite(target)) shown = target;
  paint();
}
inline void tick10(bool visible) {
  if (!visible || !fresh || !std::isfinite(target) || shown == target) return;
  shown = cyd_climate::ease(shown, target); paint();
}
inline void status(JsonObject root) { root["thermostat"]["state"] = state; }
}  // namespace cyd_thermostat
