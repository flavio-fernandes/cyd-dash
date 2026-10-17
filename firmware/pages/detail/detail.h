#pragma once
// Detail reads the usage slots directly, including each gauge's freshness.
#include "esphome.h"
#include "usage_slots.h"
#include "usage.h"
namespace cyd_detail {
using namespace cyd_usage;
static const lv_font_t *f16, *f20, *fcompact;
static lv_obj_t *rows[2][6], *titles[2], *foot = nullptr;
static std::string texts[2][6], title_text[2], t_foot;
static uint32_t c_foot = 0;
static const uint32_t SECONDARY = 0xAAB7C4;
inline int center(int i) { return cyd_usage_data::count() == 1 ? 160 : 80 + 160 * i; }
inline std::string pct(const CydWindow &w) {
  if (w.inferred) return "0%*";
  if (std::isnan(w.used)) return "--%";
  char b[16]; snprintf(b, sizeof b, "%.0f%%", w.used); return b;
}
inline std::string summary(const char *name, const CydWindow &w) {
  if (std::isnan(w.cap)) return std::string(name) + " " + pct(w);
  char b[16]; snprintf(b, sizeof b, "%.0f%%", w.cap);
  return std::string(name) + " " + pct(w) + " of " + b;
}
inline void build(lv_obj_t *root) {
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  f16 = lv_obj_get_style_text_font(lv_obj_get_child(root, 0), LV_PART_MAIN);
  f20 = lv_obj_get_style_text_font(lv_obj_get_child(root, 1), LV_PART_MAIN);
  fcompact = lv_obj_get_style_text_font(lv_obj_get_child(root, 2), LV_PART_MAIN);
  if (cyd_usage_data::count() == 2) {
    auto rule = mk_box(root); lv_obj_set_pos(rule, 159, 34); lv_obj_set_size(rule, 2, 170);
    lv_obj_set_style_bg_color(rule, lv_color_hex(TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, LV_PART_MAIN);
  }
  for (int i = 0; i < cyd_usage_data::count(); ++i) {
    titles[i] = mk_label(root, f20, WHITE, center(i), 6, "Waiting");
    lv_obj_set_width(titles[i], 148); lv_label_set_long_mode(titles[i], LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(titles[i], LV_TEXT_ALIGN_CENTER, 0);
    for (int j = 0; j < 6; ++j)
      rows[i][j] = mk_label(root, fcompact, j == 1 || j == 3 ? SECONDARY : WHITE, center(i), 35 + 28 * j, "");
    lv_obj_set_style_text_font(rows[i][4], f16, 0);
    lv_obj_set_style_pad_hor(rows[i][5], 4, 0);
    lv_obj_set_style_bg_color(rows[i][5], lv_color_hex(RED), 0);
  }
  foot = mk_label(root, f16, SECONDARY, 160, 237 - lv_font_get_line_height(f16), "");
}
inline void render(int64_t now, int st, uint32_t msg_age) {
  if (!foot) return;
  for (int i = 0; i < cyd_usage_data::count(); ++i) {
    const auto &p = cyd_usage_plans[i];
    set_text(titles[i], title_text[i], p.label.empty() ? "Waiting" : p.label);
    bool data = p.state != WAITING;
    bool bad = !p.auth.empty() && p.auth != "ok";
    CydCapNote note = cyd_cap_note(p);
    std::string s[] = {summary("5h", p.five), "resets " + countdown(p.five.resets, now),
      std::isnan(p.weekly.used) ? "" : summary("week", p.weekly),
      std::isnan(p.weekly.used) ? "" : "resets " + countdown(p.weekly.resets, now),
      cyd_cap_text(p, now), p.auth.empty() ? "" : bad ? "AUTH NOT OK" : "login ok"};
    for (int j = 0; j < 6; ++j) set_text(rows[i][j], texts[i][j], data ? s[j] : j == 0 ? "waiting for data" : "");
    lv_obj_set_style_text_color(rows[i][4], lv_color_hex(
        note == CYD_CAP_PAUSED ? SECONDARY : note == CYD_CAP_LIMIT ? RED : VIOLET), 0);
    lv_obj_set_style_bg_opa(rows[i][5], data && bad ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  }
  std::string text = st == WAITING ? "waiting for data" : (st == STALE ? "NO DATA - " : "updated ") + cyd_slots::dur(msg_age) + (st == FRESH ? " ago" : "");
  set_text(foot, t_foot, text); set_text_color(foot, c_foot, st == STALE ? AMBER : SECONDARY);
}
inline void selftest() { ESP_LOGI("cyd", "Detail uses %d gauge columns", cyd_usage_data::count()); }
}
