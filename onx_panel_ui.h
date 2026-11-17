#pragma once
// Rectangular-panel additions on top of the knob UI (touch panels only):
//   - flattens the knob's round shells (radius >= half the knob width)
//   - top-left dashboard button
//   - media detail rearranged for a tall screen: full-width cover art, title,
//     progress bar and previous / play-pause / next buttons
//   - idle screen: clock (knob default), photo slideshow or dashboard stream
// Everything is created lazily on LVGL's existing objects; the knob packages
// are not modified.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include "lvgl.h"
#include "onx_render.h"
#include "esphome/core/hal.h"
#include "onx_geom.h"

namespace onx_panel_ui {
using onx_geom::S;

inline int width = 320, height = 480;

// ------------------------------------------------------------ round shells
// The knob draws its pages inside a 240x240 circle. On a rectangle those
// circles show as a lighter disc: square every large rounded object.
inline void flatten(lv_obj_t *obj) {
  if (lv_obj_get_style_radius(obj, LV_PART_MAIN) >= S(100)) onx_render::style_radius(obj, 0, LV_PART_MAIN);
  const uint32_t n = lv_obj_get_child_count(obj);
  for (uint32_t i = 0; i < n; ++i) flatten(lv_obj_get_child(obj, i));
}

// ------------------------------------------------------------ hit testing
inline std::vector<lv_obj_t *> touch_targets;  // Visible buttons owned by LVGL.
inline bool hit(int x, int y) {
  for (auto *obj : touch_targets) {
    if (obj == nullptr || !lv_obj_is_visible(obj)) continue;
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    const int pad = S(6);
    if (x >= a.x1 - pad && x <= a.x2 + pad && y >= a.y1 - pad && y <= a.y2 + pad) return true;
  }
  return false;
}

inline lv_obj_t *round_button(lv_obj_t *parent, int size, uint32_t color) {
  lv_obj_t *b = lv_obj_create(parent);
  lv_obj_set_size(b, size, size);
  onx_render::style_radius(b, LV_RADIUS_CIRCLE, 0);
  onx_render::style_bg_color(b, lv_color_hex(color), 0);
  onx_render::style_bg_opa(b, LV_OPA_COVER, 0);
  onx_render::style_border_width(b, 0, 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  touch_targets.push_back(b);
  return b;
}
inline lv_obj_t *symbol(lv_obj_t *parent, const char *text, const lv_font_t *font) {
  lv_obj_t *l = lv_label_create(parent);
  onx_render::style_text_font(l, font, 0);
  onx_render::style_text_color(l, lv_color_hex(0xFFFFFF), 0);
  lv_label_set_text(l, text);
  lv_obj_center(l);
  return l;
}
inline bool hidden(lv_obj_t *obj) { return obj == nullptr || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN); }
inline void show(lv_obj_t *obj, bool visible) {
  if (obj == nullptr) return;
  if (visible) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}
// Positions only when they differ, so the 50 ms sync does not invalidate.
inline void place(lv_obj_t *obj, lv_align_t align, int x, int y) {
  if (obj == nullptr) return;
  if (lv_obj_get_style_align(obj, LV_PART_MAIN) == align && lv_obj_get_style_x(obj, LV_PART_MAIN) == x && lv_obj_get_style_y(obj, LV_PART_MAIN) == y) return;
  lv_obj_align(obj, align, x, y);
}
inline void set_width(lv_obj_t *obj, int w) {
  if (obj != nullptr && lv_obj_get_style_width(obj, LV_PART_MAIN) != w) lv_obj_set_width(obj, w);
}

// ------------------------------------------------------------ dashboard button
inline lv_obj_t *dash_button = nullptr;
inline std::function<void()> on_dashboard;
inline void create_dashboard_button(const lv_image_dsc_t *icon) {
  dash_button = lv_obj_create(lv_layer_top());
  lv_obj_set_size(dash_button, S(40), S(40));
  lv_obj_align(dash_button, LV_ALIGN_TOP_LEFT, S(6), S(6));
  lv_obj_set_style_radius(dash_button, S(20), 0);
  lv_obj_set_style_bg_color(dash_button, lv_color_hex(0x2A2F35), 0);
  lv_obj_set_style_bg_opa(dash_button, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(dash_button, 0, 0);
  lv_obj_set_style_pad_all(dash_button, 0, 0);
  lv_obj_remove_flag(dash_button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(dash_button, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(dash_button, S(8));
  lv_obj_t *img = lv_image_create(dash_button);
  lv_image_set_src(img, icon);
  lv_obj_center(img);
  lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(dash_button, [](lv_event_t *) { if (on_dashboard) on_dashboard(); }, LV_EVENT_CLICKED, nullptr);
  show(dash_button, false);
}

// ------------------------------------------------------------ media detail
struct Media {
  lv_obj_t *shell, *cover, *title, *artist, *arc, *transport, *icon, *playlist, *action, *room;
};
inline lv_obj_t *media_bar = nullptr, *media_row = nullptr, *media_play_label = nullptr;
inline std::function<void(bool previous)> on_track;
inline std::function<void()> on_play_pause, on_close;
inline lv_obj_t *media_close = nullptr;
inline std::function<bool()> is_playing;
inline void create_media_controls(lv_obj_t *shell, const lv_font_t *font) {
  media_close = round_button(shell, S(36), 0x2A2F35);
  symbol(media_close, LV_SYMBOL_CLOSE, font);
  lv_obj_align(media_close, LV_ALIGN_TOP_MID, 0, S(6));
  lv_obj_add_event_cb(media_close, [](lv_event_t *) { if (on_close) on_close(); }, LV_EVENT_CLICKED, nullptr);
  media_bar = lv_bar_create(shell);
  lv_bar_set_range(media_bar, 0, 1000);
  lv_obj_set_style_bg_color(media_bar, lv_color_hex(0x414850), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(media_bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(media_bar, lv_color_hex(0x62B0FF), LV_PART_INDICATOR);
  lv_obj_set_style_radius(media_bar, S(3), LV_PART_MAIN);
  lv_obj_set_style_radius(media_bar, S(3), LV_PART_INDICATOR);
  lv_obj_remove_flag(media_bar, LV_OBJ_FLAG_CLICKABLE);

  media_row = lv_obj_create(shell);
  lv_obj_set_size(media_row, width, S(64));
  lv_obj_set_style_bg_opa(media_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(media_row, 0, 0);
  lv_obj_set_style_pad_all(media_row, 0, 0);
  lv_obj_remove_flag(media_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(media_row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_flex_flow(media_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(media_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *prev = round_button(media_row, S(44), 0x2A2F35);
  symbol(prev, LV_SYMBOL_PREV, font);
  lv_obj_add_event_cb(prev, [](lv_event_t *) { if (on_track) on_track(true); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *play = round_button(media_row, S(56), 0x1F6FEB);
  media_play_label = symbol(play, LV_SYMBOL_PLAY, font);
  lv_obj_add_event_cb(play, [](lv_event_t *) { if (on_play_pause) on_play_pause(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *next = round_button(media_row, S(44), 0x2A2F35);
  symbol(next, LV_SYMBOL_NEXT, font);
  lv_obj_add_event_cb(next, [](lv_event_t *) { if (on_track) on_track(false); }, LV_EVENT_CLICKED, nullptr);
}

// Called every tick while the knob's detail page is shown.
inline void sync_media(const Media &m, const lv_font_t *font) {
  const bool media = !hidden(m.cover) || !hidden(m.title);
  if (media_bar == nullptr) {
    if (!media) return;
    create_media_controls(m.shell, font);
  }
  const bool playlist = !hidden(m.playlist);
  show(media_row, media && !playlist);
  show(media_close, media && !playlist);
  if (!media) { show(media_bar, false); return; }
  const int w = width, h = height;
  // Square shell covering the whole screen (detail pages hide the strip).
  if (lv_obj_get_style_width(m.shell, LV_PART_MAIN) != w || lv_obj_get_style_height(m.shell, LV_PART_MAIN) != h)
    lv_obj_set_size(m.shell, w, h);
  // Full-width cover art on top, text and controls in the space below.
  place(m.cover, LV_ALIGN_TOP_MID, 0, S(50));
  place(m.icon, LV_ALIGN_TOP_MID, 0, S(50));
  place(m.transport, LV_ALIGN_TOP_MID, 0, w / 2 - S(24));
  const int text_y = S(136);
  place(m.title, LV_ALIGN_TOP_MID, 0, text_y);
  place(m.artist, LV_ALIGN_TOP_MID, 0, text_y + S(24));
  place(m.action, LV_ALIGN_TOP_MID, 0, text_y + S(48));
  place(m.room, LV_ALIGN_TOP_MID, 0, text_y + S(70));
  set_width(m.action, w - S(24));
  set_width(m.room, w - S(24));
  set_width(m.title, w - S(24));
  set_width(m.artist, w - S(24));
  // The knob's round progress ring becomes a straight bar.
  const bool progress = !hidden(m.arc);
  onx_render::style_opa(m.arc, LV_OPA_TRANSP, 0);
  show(m.transport, false);
  show(media_bar, progress || lv_bar_get_value(media_bar) > 0);
  if (m.arc != nullptr) lv_bar_set_value(media_bar, lv_arc_get_value(m.arc), LV_ANIM_OFF);
  lv_obj_set_size(media_bar, w - S(40), S(5));
  place(media_bar, LV_ALIGN_TOP_MID, 0, text_y + S(96));
  place(media_row, LV_ALIGN_BOTTOM_MID, 0, -S(12));
  if (media_play_label && is_playing) {
    const char *want = is_playing() ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY;
    if (strcmp(lv_label_get_text(media_play_label), want) != 0) lv_label_set_text(media_play_label, want);
  }
  if (m.playlist) {
    lv_obj_set_size(m.playlist, w, h);
    place(m.playlist, LV_ALIGN_CENTER, 0, 0);
  }
}

// ------------------------------------------------------------ idle screen
enum IdleMode { IDLE_CLOCK = 0, IDLE_SLIDESHOW = 1, IDLE_DASHBOARD = 2 };
inline lv_obj_t *slide_layer = nullptr, *slide_image = nullptr, *slide_time = nullptr, *slide_hint = nullptr;
inline std::vector<std::string> slides;
inline size_t slide_index = 0;
inline uint32_t slide_due = 0;
inline bool slide_loading = false;

inline std::vector<std::string> split_urls(const std::string &text) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : text) {
    if (c == ' ' || c == ',' || c == '\n' || c == ';' || c == '\t') {
      if (!cur.empty()) out.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

inline void create_slideshow(lv_obj_t *standby_page, const lv_font_t *time_font, const lv_font_t *hint_font) {
  slide_layer = lv_obj_create(standby_page);
  lv_obj_set_size(slide_layer, width, height);
  lv_obj_set_pos(slide_layer, 0, 0);
  lv_obj_set_style_bg_color(slide_layer, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(slide_layer, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(slide_layer, 0, 0);
  lv_obj_set_style_border_width(slide_layer, 0, 0);
  lv_obj_set_style_pad_all(slide_layer, 0, 0);
  lv_obj_remove_flag(slide_layer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(slide_layer, LV_OBJ_FLAG_CLICKABLE);
  slide_image = lv_image_create(slide_layer);
  lv_obj_center(slide_image);
  slide_hint = lv_label_create(slide_layer);
  lv_obj_set_width(slide_hint, width - S(40));
  lv_obj_set_style_text_font(slide_hint, hint_font, 0);
  lv_obj_set_style_text_color(slide_hint, lv_color_hex(0x9C9C9E), 0);
  lv_obj_set_style_text_align(slide_hint, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(slide_hint, "Add picture addresses in\n\"Slideshow pictures\" in Home Assistant.");
  lv_obj_center(slide_hint);
  slide_time = lv_label_create(slide_layer);
  lv_obj_set_style_text_font(slide_time, time_font, 0);
  lv_obj_set_style_text_color(slide_time, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_color(slide_time, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(slide_time, LV_OPA_50, 0);
  lv_obj_set_style_pad_hor(slide_time, S(8), 0);
  lv_obj_set_style_radius(slide_time, S(8), 0);
  lv_obj_align(slide_time, LV_ALIGN_BOTTOM_LEFT, S(12), -S(12));
  lv_label_set_text(slide_time, "");
  show(slide_layer, false);
}

}  // namespace onx_panel_ui
