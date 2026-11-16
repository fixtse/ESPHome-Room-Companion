#pragma once
#include <cstdint>
#include <cstdlib>
#define LV_COLOR_16_SWAP 0
#define LV_COLOR_FORMAT_ARGB8888 1
struct lv_image_dsc_t { struct { unsigned magic, cf, w, h, stride; } header; unsigned data_size; const uint8_t *data; };
struct lv_font_t { uint16_t line_height, base_line; const lv_font_t *fallback; };
struct lv_obj_t {};
inline lv_font_t *lv_imgfont_create(uint16_t, const void *(*)(const lv_font_t*,uint32_t,uint32_t,int32_t*,void*), void*) {return nullptr;}
inline const lv_font_t *lv_obj_get_style_text_font(lv_obj_t*,int) {return nullptr;}
inline void lv_obj_set_style_text_font(lv_obj_t*,const lv_font_t*,int) {}
#define LV_PART_MAIN 0
#define LV_IMAGE_HEADER_MAGIC 0x19
struct lv_event_t {};
#define LV_EVENT_REFR_READY 0
inline void lv_image_cache_drop(const void*) {}
inline void *lv_display_get_default() { return nullptr; }
inline void lv_display_add_event_cb(void*, void (*)(lv_event_t*), int, void*) {}
