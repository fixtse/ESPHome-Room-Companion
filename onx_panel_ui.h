#pragma once
// Rectangular-panel additions on top of the native UI (touch panels only):
//   - flattens the base layout's round shells (radius >= half its width)
//   - media detail rearranged for a tall screen: large cover art, title,
//     progress bar and previous / play-pause / next buttons (no header: swipe
//     horizontally to close, pull down from the top for notifications)
//   - idle screen: clock (default), photo slideshow or dashboard stream
// Everything is created lazily on LVGL's existing objects; the shared packages
// are not modified.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include "lvgl.h"
#include "onx_render.h"
#include "esphome/core/hal.h"
#include "onx_geom.h"
#include "onx_display_settings.h"

namespace onx_panel_ui {
using onx_geom::S;

// Logical screen after the configured orientation (Device settings → Display
// orientation; these panels have no accelerometer). The board's native size is
// portrait; quarter turns 1 and 3 give landscape. Applied once at boot.
inline int native_width = 320, native_height = 480, width = 320, height = 480;
inline uint8_t turns = 0;
inline void configure_screen(int w, int h, uint8_t quarter_turns) {
  native_width = w; native_height = h; turns = quarter_turns & 3;
  width = turns & 1 ? h : w;
  height = turns & 1 ? w : h;
}
inline bool landscape() { return width > height; }

// ------------------------------------------------------------ hit testing
inline std::vector<lv_obj_t *> touch_targets;  // Visible buttons owned by LVGL.
inline lv_obj_t *idle_badge = nullptr, *idle_badge_label = nullptr;

// ------------------------------------------------------------ round shells
// The base layout draws its pages inside a 240x240 circle. On a rectangle those
// circles show as a lighter disc: square every large rounded object, but keep
// the panel's own round buttons (LV_RADIUS_CIRCLE would otherwise match).
inline void flatten(lv_obj_t *obj) {
  if (obj != idle_badge && lv_obj_get_style_radius(obj, LV_PART_MAIN) >= S(100) &&
      std::find(touch_targets.begin(), touch_targets.end(), obj) == touch_targets.end())
    onx_render::style_radius(obj, 0, LV_PART_MAIN);
  const uint32_t n = lv_obj_get_child_count(obj);
  for (uint32_t i = 0; i < n; ++i) flatten(lv_obj_get_child(obj, i));
}
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

// ------------------------------------------------------------ media detail
struct Media {
  lv_obj_t *shell, *cover, *title, *artist, *arc, *transport, *icon, *playlist, *action, *room;
};
// True while the local wake word is listening (examples/room-companion/wake-word.yaml).
inline std::function<bool()> wake_word_on;

inline lv_obj_t *media_bar = nullptr, *media_row = nullptr, *media_play = nullptr, *media_play_label = nullptr;
inline std::function<void(bool previous)> on_track;
inline std::function<void()> on_play_pause;
inline std::function<bool()> is_playing;
inline std::function<bool()> has_quick_actions;
inline std::function<void()> open_quick_actions;
inline lv_obj_t *media_actions = nullptr, *media_actions_label = nullptr, *media_art_frame = nullptr;
// Elapsed and total time under the progress bar.
inline lv_obj_t *media_elapsed = nullptr, *media_total = nullptr;
inline const lv_font_t *time_font = nullptr;
inline std::function<float()> media_duration;  // Seconds; NAN when unknown.
inline std::string duration_text(float seconds) {
  if (!std::isfinite(seconds) || seconds < 0) return "";
  const int t = int(seconds), h = t / 3600, m = t / 60 % 60, sec = t % 60;
  char out[16];
  if (h) snprintf(out, sizeof(out), "%d:%02d:%02d", h, m, sec);
  else snprintf(out, sizeof(out), "%d:%02d", m, sec);
  return out;
}
// Top of the artwork: there is no header on details any more.
inline int media_top() { return S(12); }
inline void create_media_controls(lv_obj_t *shell, const lv_font_t *font) {
  media_bar = lv_bar_create(shell);
  lv_bar_set_range(media_bar, 0, 1000);
  lv_obj_set_style_bg_color(media_bar, lv_color_hex(0x414850), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(media_bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(media_bar, lv_color_hex(onx_display_settings::accent()), LV_PART_INDICATOR);
  lv_obj_set_style_radius(media_bar, S(3), LV_PART_MAIN);
  lv_obj_set_style_radius(media_bar, S(3), LV_PART_INDICATOR);
  lv_obj_remove_flag(media_bar, LV_OBJ_FLAG_CLICKABLE);
  media_elapsed = lv_label_create(shell);
  media_total = lv_label_create(shell);
  for (auto *label : {media_elapsed, media_total}) {
    lv_obj_set_style_text_font(label, time_font != nullptr ? time_font : font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(onx_display_settings::muted_text()), 0);
    lv_label_set_text(label, "");
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
  }

  media_actions = round_button(shell, S(40), 0x2A2F35);
  media_actions_label = symbol(media_actions, LV_SYMBOL_LIST, font);
  lv_obj_add_event_cb(media_actions, [](lv_event_t *) { if (open_quick_actions) open_quick_actions(); }, LV_EVENT_CLICKED, nullptr);
  media_art_frame = lv_obj_create(shell);
  lv_obj_remove_style_all(media_art_frame);
  lv_obj_set_style_radius(media_art_frame, S(18), 0);
  lv_obj_set_style_clip_corner(media_art_frame, true, 0);
  lv_obj_remove_flag(media_art_frame, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(media_art_frame, LV_OBJ_FLAG_CLICKABLE);
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
  media_play = round_button(media_row, S(56), onx_display_settings::accent_strong());
  media_play_label = symbol(media_play, LV_SYMBOL_PLAY, font);
  lv_obj_add_event_cb(media_play, [](lv_event_t *) { if (on_play_pause) on_play_pause(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *next = round_button(media_row, S(44), 0x2A2F35);
  symbol(next, LV_SYMBOL_NEXT, font);
  lv_obj_add_event_cb(next, [](lv_event_t *) { if (on_track) on_track(false); }, LV_EVENT_CLICKED, nullptr);
}

// Called every tick while the media detail page is shown.
inline void sync_media(const Media &m, const lv_font_t *font) {
  const bool media = !hidden(m.cover) || !hidden(m.title);
  if (media_bar == nullptr) {
    if (!media) return;
    create_media_controls(m.shell, font);
  }
  const bool playlist = !hidden(m.playlist);
  show(media_row, media && !playlist);
  show(media_art_frame, media && !hidden(m.cover));
  const bool actions = media && !playlist && has_quick_actions && has_quick_actions();
  show(media_actions, actions);
  if (!media) { show(media_bar, false); return; }
  const int w = width, h = height, top = media_top(), row_h = S(64), row_gap = S(12);
  // Square shell covering the whole screen (detail pages hide the strip).
  if (lv_obj_get_style_width(m.shell, LV_PART_MAIN) != w || lv_obj_get_style_height(m.shell, LV_PART_MAIN) != h)
    lv_obj_set_size(m.shell, w, h);
  // Accent colour follows the device setting without recreating the controls.
  onx_render::style_bg_color(media_bar, lv_color_hex(onx_display_settings::accent()), LV_PART_INDICATOR);
  onx_render::style_bg_color(media_play, lv_color_hex(onx_display_settings::accent_strong()), 0);
  // Portrait: artwork on top, text, progress and transport below.
  // Landscape: square artwork on the left, the same column on the right.
  const bool wide = landscape();
  const int cover_size = wide ? std::max(48, std::min(h - 2 * top, w * 45 / 100))
                              : std::max(48, std::min(w - 48, h - top - 12 - 84 - 28 - row_h - row_gap));
  const int col_x = wide ? top + cover_size + S(12) : 0;      // Text/transport column.
  const int col_w = wide ? w - col_x - S(8) : w;
  const int col_dx = col_x + col_w / 2 - w / 2;                 // Offset from the screen centre.
  lv_image_header_t header{};
  if (lv_image_decoder_get_info(lv_image_get_src(m.cover), &header) == LV_RESULT_OK && header.w && header.h) {
    lv_image_set_pivot(m.cover, header.w / 2, header.h / 2);
    const int scale = cover_size * 256 / std::max(header.w,header.h);
    if (lv_image_get_scale_x(m.cover) != scale) lv_image_set_scale(m.cover,scale);
  }
  const int cover_dx = wide ? top + cover_size / 2 - w / 2 : 0;
  lv_obj_set_size(media_art_frame, cover_size, cover_size);
  place(media_art_frame, LV_ALIGN_TOP_MID, cover_dx, top);
  if (lv_obj_get_parent(m.cover) != media_art_frame) lv_obj_set_parent(m.cover, media_art_frame);
  place(m.cover, LV_ALIGN_CENTER, 0, 0);
  place(media_actions, LV_ALIGN_TOP_RIGHT, -S(12), top);
  if (actions) lv_obj_move_foreground(media_actions);
  // Without artwork the placeholder icon sits in the middle of the artwork area.
  int icon_h = 0;
  if (m.icon != nullptr && lv_obj_check_type(m.icon, &lv_image_class)) {
    lv_image_header_t ih{};
    if (lv_image_decoder_get_info(lv_image_get_src(m.icon), &ih) == LV_RESULT_OK)
      icon_h = int(ih.h) * lv_image_get_scale_y(m.icon) / 256;
  }
  place(m.icon, LV_ALIGN_TOP_MID, cover_dx, top + std::max(0, (cover_size - icon_h) / 2));
  place(m.transport, LV_ALIGN_TOP_MID, cover_dx, top + cover_size / 2);
  const int text_y = wide ? top + S(8) : top + cover_size + 12;
  place(m.title, LV_ALIGN_TOP_MID, col_dx, text_y);
  place(m.artist, LV_ALIGN_TOP_MID, col_dx, text_y + 28);
  place(m.action, LV_ALIGN_TOP_MID, col_dx, text_y + 52);
  show(m.room, false);
  for (auto *label : {m.action, m.room, m.title, m.artist}) set_width(label, col_w - S(24));
  // The base layout's round progress ring becomes a straight bar.
  const bool progress = !hidden(m.arc);
  onx_render::style_opa(m.arc, LV_OPA_TRANSP, 0);
  // The base layout's paused/transport glyph over the artwork is redundant with the
  // play/pause button. Its own refresh keeps un-hiding it while paused, so
  // hiding it here each tick made it flicker: keep it transparent instead.
  onx_render::style_opa(m.transport, LV_OPA_TRANSP, 0);
  show(media_bar, !playlist && progress);
  if (m.arc != nullptr) lv_bar_set_value(media_bar, lv_arc_get_value(m.arc), LV_ANIM_OFF);
  if (lv_obj_get_style_width(media_bar, LV_PART_MAIN) != col_w - S(40)) lv_obj_set_size(media_bar, col_w - S(40), S(5));
  // Times sit just above the bar, at its ends.
  const int bar_y = h - row_h - row_gap - 20, bar_w = col_w - S(40);
  place(media_bar, LV_ALIGN_TOP_MID, col_dx, bar_y);
  const float duration = media_duration ? media_duration() : NAN;
  const bool times = !playlist && progress && std::isfinite(duration) && duration > 0;
  show(media_elapsed, times);
  show(media_total, times);
  if (times) {
    const int line_h = lv_font_get_line_height(time_font != nullptr ? time_font : font);
    const std::string elapsed = duration_text(duration * lv_arc_get_value(m.arc) / 1000.0f), total = duration_text(duration);
    if (elapsed != lv_label_get_text(media_elapsed)) lv_label_set_text(media_elapsed, elapsed.c_str());
    if (total != lv_label_get_text(media_total)) lv_label_set_text(media_total, total.c_str());
    place(media_elapsed, LV_ALIGN_TOP_LEFT, col_x + (col_w - bar_w) / 2, bar_y - line_h - S(2));
    place(media_total, LV_ALIGN_TOP_RIGHT, -(w - col_x - col_w) - (col_w - bar_w) / 2, bar_y - line_h - S(2));
  }
  set_width(media_row, col_w);
  place(media_row, LV_ALIGN_BOTTOM_MID, col_dx, -row_gap);
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

// Notification count at the top right of the idle clock (hidden when none).
inline void create_idle_badge(lv_obj_t *standby_page, const lv_font_t *font, const void *bell) {
  idle_badge = lv_obj_create(standby_page);
  lv_obj_remove_flag(idle_badge, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_size(idle_badge, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(idle_badge, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(idle_badge, 0, 0);
  lv_obj_set_style_bg_opa(idle_badge, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(idle_badge, S(9), 0);
  lv_obj_set_style_pad_ver(idle_badge, S(3), 0);
  lv_obj_set_style_pad_column(idle_badge, S(6), 0);
  lv_obj_set_flex_flow(idle_badge, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(idle_badge, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  if (bell != nullptr) {
    lv_obj_t *glyph = lv_image_create(idle_badge);
    lv_image_set_src(glyph, bell);
    lv_obj_set_style_image_recolor_opa(glyph, LV_OPA_COVER, 0);
    lv_obj_remove_flag(glyph, LV_OBJ_FLAG_CLICKABLE);
  }
  idle_badge_label = lv_label_create(idle_badge);
  lv_obj_set_style_text_font(idle_badge_label, font, 0);
  lv_label_set_text(idle_badge_label, "");
  lv_obj_align(idle_badge, LV_ALIGN_TOP_RIGHT, -S(12), S(8));
  show(idle_badge, false);
}
inline void update_idle_badge(size_t count, bool visible) {
  if (idle_badge == nullptr) return;
  visible = visible && count > 0;
  if (visible) {
    char out[8];
    snprintf(out, sizeof(out), "%u", static_cast<unsigned>(count > 99 ? 99 : count));
    if (strcmp(lv_label_get_text(idle_badge_label), out) != 0) lv_label_set_text(idle_badge_label, out);
    onx_render::style_bg_color(idle_badge, lv_color_hex(onx_display_settings::surface()), 0);
    onx_render::style_text_color(idle_badge_label, lv_color_hex(onx_display_settings::foreground()), 0);
    if (lv_obj_get_child_count(idle_badge) > 1)
      onx_render::style_image_recolor(lv_obj_get_child(idle_badge, 0), lv_color_hex(onx_display_settings::accent()), 0);
    if (hidden(idle_badge)) lv_obj_move_foreground(idle_badge);
  }
  show(idle_badge, visible);
}

}  // namespace onx_panel_ui
