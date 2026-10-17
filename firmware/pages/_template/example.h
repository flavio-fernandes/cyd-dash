#pragma once
// Replace example with your page name in all three files. See docs/add-a-page.md.
#include "esphome.h"
#include <ctime>
#include <string>
namespace cyd_example {
static lv_obj_t *clock_label, *spark;
static std::string clock_text;
static int step = 0;
// build runs once; font holders are the first three children of root.
inline void build(lv_obj_t *root) {
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  auto f20 = lv_obj_get_style_text_font(lv_obj_get_child(root, 1), LV_PART_MAIN);
  auto f32 = lv_obj_get_style_text_font(lv_obj_get_child(root, 2), LV_PART_MAIN);
  auto greeting = lv_label_create(root);
  lv_obj_set_style_text_font(greeting, f32, 0);
  lv_obj_set_style_text_color(greeting, lv_color_hex(0xFFFFFF), 0);
  lv_label_set_text(greeting, "Hello World");
  lv_obj_align(greeting, LV_ALIGN_TOP_MID, 0, 58);
  clock_label = lv_label_create(root);
  lv_obj_set_style_text_font(clock_label, f20, 0);
  lv_obj_set_style_text_color(clock_label, lv_color_hex(0xAAB7C4), 0);
  lv_label_set_text(clock_label, "Waiting for time");
  lv_obj_align(clock_label, LV_ALIGN_TOP_MID, 0, 110);
  spark = lv_obj_create(root);
  lv_obj_remove_style_all(spark);
  lv_obj_clear_flag(spark, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_size(spark, 8, 8);
  lv_obj_set_style_radius(spark, 4, 0);
  lv_obj_set_style_bg_color(spark, lv_color_hex(0x42D9E8), 0);
  lv_obj_set_style_bg_opa(spark, LV_OPA_COVER, 0);
  lv_obj_set_pos(spark, 136, 164);
}
// render may run off screen: skip visual changes until visible.
inline void render(const cyd_page::Ctx &ctx) {
  if (!ctx.visible) return;
  std::string text = "Waiting for time";
  if (ctx.now >= 0) {
    time_t t = ctx.now; struct tm parts; localtime_r(&t, &parts);
    char b[24]; strftime(b, sizeof b, "%H:%M:%S", &parts); text = b;
  }
  if (text != clock_text) { clock_text = text; lv_label_set_text(clock_label, text.c_str()); }
}
// The shared 100 ms clock calls this. Animate only the tiny dot, only on screen.
inline void tick10(bool visible) {
  if (!visible || !spark) return;
  step = (step + 1) % 40;
  lv_obj_set_x(spark, 136 + (step <= 20 ? step : 40 - step) * 2);
}
}  // namespace cyd_example
