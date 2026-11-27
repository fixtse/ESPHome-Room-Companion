#pragma once
// Host tests for onx_glyphs.h: just the LVGL font types it uses.
#include <cstdint>
#include <cstdlib>
struct lv_draw_buf_t { struct { uint32_t stride; } header; void *data; };
struct lv_font_glyph_dsc_t {
  uint16_t adv_w, box_w, box_h; int16_t ofs_x, ofs_y; uint32_t stride; int format; int is_placeholder;
  int req_raw_bitmap; struct { const void *src; } gid;
};
struct lv_font_t {
  bool (*get_glyph_dsc)(const lv_font_t *, lv_font_glyph_dsc_t *, uint32_t, uint32_t);
  const void *(*get_glyph_bitmap)(lv_font_glyph_dsc_t *, lv_draw_buf_t *);
  int32_t line_height, base_line;
  const lv_font_t *fallback;
  void *user_data;
};
struct lv_timer_t {};
struct lv_obj_t {};
#define LV_FONT_GLYPH_FORMAT_A8 1
inline int style_reports = 0;
inline void lv_obj_report_style_change(void *) { ++style_reports; }
inline void *lv_malloc_zeroed(size_t n) { return calloc(1, n); }
inline lv_timer_t *lv_timer_create(void (*)(lv_timer_t *), uint32_t, void *) { static lv_timer_t t; return &t; }
