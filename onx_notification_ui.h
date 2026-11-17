#pragma once
// Notification centre UI for the touch panels (see onx_notification::Center).
//   - bell on the main carousel (top right, no background) with the unread count;
//     every screen opens the centre by pulling down from the top edge
//   - arrival banner at the top; tap to open
//   - list of recent notifications (accent bar while unread, time, x to
//     dismiss), Clear all in the header, swipe up (or tap the handle) to close
//   - detail view with picture, Back and Dismiss
// Built on LVGL's top layer; sizes follow onx_geom so both panels scale.
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <functional>
#include <string>
#include "lvgl.h"
#include "esphome/core/hal.h"
#include "onx_geom.h"
#include "onx_display_settings.h"
#include "onx_notification.h"

namespace onx_notification_ui {
using onx_geom::S;

struct Fonts {
  const lv_font_t *title{nullptr}, *body{nullptr}, *small{nullptr}, *icon{nullptr}, *symbol{nullptr};
};
struct Hooks {
  std::function<void(const std::string &)> load_image;  // Start downloading a picture.
  std::function<void()> release_image;                  // Free the picture.
  std::function<void()> activity;                       // Keep the screen awake.
};

inline Fonts fonts;
inline Hooks hooks;
inline int width = 240, height = 240;
inline lv_obj_t *bell = nullptr, *badge = nullptr, *badge_label = nullptr;
inline lv_obj_t *toast = nullptr, *toast_title = nullptr, *toast_body = nullptr;
inline lv_obj_t *panel = nullptr, *list = nullptr, *empty_label = nullptr, *count_label = nullptr, *clear_button = nullptr,
                *handle = nullptr;
inline const void *bell_image = nullptr;  // Bell glyph (an MDI image), set before init().
inline lv_obj_t *detail = nullptr, *detail_title = nullptr, *detail_age = nullptr, *detail_image = nullptr,
                *detail_body = nullptr;
inline uint32_t detail_id = 0, built_revision = 0;
inline bool panel_open = false;
inline bool bell_wanted = false;  // Only the main carousel shows the bell.

constexpr uint32_t BG = 0x1A1D21, CARD = 0x272B30, TEXT = 0xFFFFFF, MUTED = 0xB8B8B8;
inline uint32_t accent() { return onx_display_settings::accent(); }
// The list follows the panel's saved palette.
inline uint32_t bg() { return onx_display_settings::background(); }
inline uint32_t surface() { return onx_display_settings::surface(); }
inline uint32_t control() { return onx_display_settings::control_surface(); }
inline uint32_t foreground() { return onx_display_settings::foreground(); }
inline uint32_t muted() { return onx_display_settings::muted_text(); }
// "now", "5 min", then the time of day (or hours before the clock is set).
inline std::string when(uint32_t received_ms, uint32_t now_ms) {
  const uint32_t seconds = (now_ms - received_ms) / 1000;
  if (seconds < 60) return "now";
  if (seconds < 3600) return std::to_string(seconds / 60) + " min";
  const time_t now = std::time(nullptr);
  if (now > 1600000000) {
    const time_t at = now - time_t(seconds);
    struct tm local{};
    localtime_r(&at, &local);
    char out[16];
    strftime(out, sizeof(out), seconds < 86400 ? onx_display_settings::time_pattern() : "%d %b", &local);
    return out;
  }
  return seconds < 86400 ? std::to_string(seconds / 3600) + " h" : std::to_string(seconds / 86400) + " d";
}

inline void hide(lv_obj_t *obj, bool hidden) {
  if (obj == nullptr) return;
  if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}
inline lv_obj_t *box(lv_obj_t *parent, uint32_t color, int radius) {
  lv_obj_t *obj = lv_obj_create(parent);
  lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_radius(obj, radius, 0);
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  return obj;
}
inline lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *value) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  lv_label_set_text(label, value);
  return label;
}
inline lv_obj_t *button(lv_obj_t *parent, const char *caption, uint32_t color, lv_event_cb_t cb, void *user) {
  lv_obj_t *btn = box(parent, color, S(18));
  lv_obj_set_height(btn, S(36));
  lv_obj_set_width(btn, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_hor(btn, S(14), 0);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *label = text(btn, fonts.body, TEXT, caption);
  lv_obj_center(label);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user);
  return btn;
}
inline void touched() {
  if (hooks.activity) hooks.activity();
}

// True while the centre covers the native UI or shows its banner; the native UI
// and the dashboard stream then give way.
inline bool owns_screen() {
  return panel_open || detail_id != 0 || onx_notification::center.toast_visible(esphome::millis());
}

inline void refresh();
inline void open_detail(uint32_t id);

inline void close_detail() {
  if (detail_id == 0) return;
  detail_id = 0;
  if (detail_image) {
    hide(detail_image, true);
    lv_image_set_src(detail_image, static_cast<const void *>(nullptr));
  }
  if (hooks.release_image) hooks.release_image();
  hide(detail, true);
  refresh();
}
inline void open_panel() {
  touched();
  onx_notification::center.hide_toast();
  panel_open = true;
  built_revision = 0;
  refresh();
}
inline void close_panel() {
  panel_open = false;
  refresh();
}

inline void on_bell(lv_event_t *) { panel_open ? close_panel() : open_panel(); }
inline void on_toast(lv_event_t *) {
  touched();
  const uint32_t id = onx_notification::center.toast_id;
  onx_notification::center.hide_toast();
  if (id) open_detail(id);
}
inline void on_close_panel(lv_event_t *) { touched(); close_panel(); }
// Swipe up anywhere on the centre (the list stops scrolling at its ends).
inline void on_gesture(lv_event_t *) {
  lv_indev_t *indev = lv_indev_active();
  if (indev == nullptr || lv_indev_get_gesture_dir(indev) != LV_DIR_TOP) return;
  touched();
  close_panel();
}
inline void on_clear(lv_event_t *) {
  touched();
  onx_notification::center.clear();
  close_panel();
}
inline void on_item(lv_event_t *event) {
  touched();
  open_detail(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event))));
}
inline void on_item_dismiss(lv_event_t *event) {
  touched();
  lv_event_stop_bubbling(event);
  // Dismissing rebuilds the list, which deletes this button: defer it until the
  // event has finished.
  lv_async_call([](void *user) {
    onx_notification::center.dismiss(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(user)));
  }, lv_event_get_user_data(event));
}
inline void on_back(lv_event_t *) { touched(); close_detail(); }
inline void on_detail_dismiss(lv_event_t *) {
  touched();
  const uint32_t id = detail_id;
  close_detail();
  onx_notification::center.dismiss(id);
}

inline void build_header(lv_obj_t *parent, const char *title, lv_obj_t **out_row) {
  lv_obj_t *row = box(parent, BG, 0);
  lv_obj_set_size(row, lv_pct(100), S(44));
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  text(row, fonts.title, TEXT, title);
  *out_row = row;
}

inline void init(int screen_width, int screen_height, Fonts f, Hooks h) {
  width = screen_width;
  height = screen_height;
  fonts = f;
  hooks = std::move(h);
  lv_obj_t *top = lv_layer_top();

  // Bell, top-right corner of the main carousel: a plain icon in the slim
  // header row (onx_panel_layout::MAIN_TOP), no circular background.
  bell = box(top, CARD, 0);
  lv_obj_set_style_bg_opa(bell, LV_OPA_TRANSP, 0);
  lv_obj_set_size(bell, 44, 36);
  lv_obj_align(bell, LV_ALIGN_TOP_RIGHT, -4, 2);
  lv_obj_add_flag(bell, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(bell, S(8));
  if (bell_image != nullptr) {
    lv_obj_t *glyph = lv_image_create(bell);
    lv_image_set_src(glyph, bell_image);
    lv_obj_set_style_image_recolor(glyph, lv_color_hex(TEXT), 0);
    lv_obj_set_style_image_recolor_opa(glyph, LV_OPA_COVER, 0);
    lv_obj_remove_flag(glyph, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(glyph);
  } else {
    lv_obj_center(text(bell, fonts.body, TEXT, "!"));
  }
  lv_obj_add_event_cb(bell, on_bell, LV_EVENT_CLICKED, nullptr);
  badge = box(top, 0xE5484D, S(9));
  lv_obj_set_size(badge, LV_SIZE_CONTENT, S(18));
  lv_obj_set_style_min_width(badge, S(18), 0);
  lv_obj_set_style_pad_hor(badge, S(4), 0);
  lv_obj_align_to(badge, bell, LV_ALIGN_TOP_RIGHT, 0, 0);
  badge_label = text(badge, fonts.small, TEXT, "");
  lv_obj_center(badge_label);
  lv_obj_remove_flag(badge, LV_OBJ_FLAG_CLICKABLE);

  // Arrival banner.
  toast = box(top, CARD, S(12));
  lv_obj_set_size(toast, width - 2 * S(8) - S(48), LV_SIZE_CONTENT);
  lv_obj_align(toast, LV_ALIGN_TOP_LEFT, S(8), S(6));
  lv_obj_set_style_pad_all(toast, S(8), 0);
  lv_obj_set_style_border_side(toast, LV_BORDER_SIDE_LEFT, 0);
  lv_obj_set_style_border_width(toast, S(4), 0);
  lv_obj_set_style_border_color(toast, lv_color_hex(accent()), 0);
  lv_obj_set_flex_flow(toast, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(toast, LV_OBJ_FLAG_CLICKABLE);
  toast_title = text(toast, fonts.title, TEXT, "");
  toast_body = text(toast, fonts.body, MUTED, "");
  for (auto *label : {toast_title, toast_body}) {
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  }
  lv_obj_add_event_cb(toast, on_toast, LV_EVENT_CLICKED, nullptr);

  // List panel: header with the count and Clear all, the cards, and a handle
  // along the bottom (swipe up or tap it to close).
  panel = box(top, BG, 0);
  lv_obj_set_size(panel, width, height);
  lv_obj_set_style_pad_hor(panel, S(10), 0);
  lv_obj_set_style_pad_top(panel, S(10), 0);
  lv_obj_set_style_pad_bottom(panel, S(4), 0);
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(panel, S(8), 0);
  lv_obj_add_event_cb(panel, on_gesture, LV_EVENT_GESTURE, nullptr);
  lv_obj_t *header = box(panel, BG, 0);
  lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
  lv_obj_set_size(header, lv_pct(100), S(36));
  lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(header, S(6), 0);
  lv_obj_set_style_pad_left(header, S(4), 0);
  text(header, fonts.title, TEXT, "Notifications");
  count_label = text(header, fonts.title, MUTED, "");
  lv_obj_t *spacer = box(header, BG, 0);
  lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
  lv_obj_set_size(spacer, 1, 1);
  lv_obj_set_flex_grow(spacer, 1);
  clear_button = button(header, "Clear all", CARD, on_clear, nullptr);
  lv_obj_set_height(clear_button, S(30));
  lv_obj_set_style_radius(clear_button, S(15), 0);
  lv_obj_set_style_pad_hor(clear_button, S(12), 0);
  lv_obj_set_style_text_font(lv_obj_get_child(clear_button, 0), fonts.small, 0);
  list = box(panel, BG, 0);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_width(list, lv_pct(100));
  lv_obj_set_flex_grow(list, 1);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list, S(8), 0);
  lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
  // Without elastic/chained scrolling a swipe past the end becomes a gesture.
  lv_obj_remove_flag(list, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN));
  empty_label = text(list, fonts.body, MUTED, "No notifications");
  lv_obj_set_width(empty_label, lv_pct(100));
  lv_obj_set_style_text_align(empty_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_pad_top(empty_label, S(40), 0);
  handle = box(panel, BG, 0);
  lv_obj_set_style_bg_opa(handle, LV_OPA_TRANSP, 0);
  lv_obj_set_size(handle, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(handle, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(handle, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(handle, S(4), 0);
  lv_obj_set_style_pad_ver(handle, S(4), 0);
  lv_obj_add_flag(handle, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(handle, on_close_panel, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *grip = box(handle, MUTED, S(2));
  lv_obj_set_size(grip, S(36), S(4));
  lv_obj_remove_flag(grip, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *hint = text(handle, fonts.small, MUTED, "Swipe up to close");
  lv_obj_remove_flag(hint, LV_OBJ_FLAG_CLICKABLE);

  // Detail view.
  detail = box(top, BG, 0);
  lv_obj_set_size(detail, width, height);
  lv_obj_set_style_pad_all(detail, S(10), 0);
  lv_obj_set_flex_flow(detail, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(detail, S(8), 0);
  lv_obj_add_flag(detail, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(detail, LV_DIR_VER);
  lv_obj_t *detail_header;
  build_header(detail, "Notification", &detail_header);
  button(detail_header, "Back", CARD, on_back, nullptr);
  detail_title = text(detail, fonts.title, TEXT, "");
  detail_age = text(detail, fonts.small, accent(), "");
  detail_image = lv_image_create(detail);
  lv_obj_align(detail_image, LV_ALIGN_TOP_MID, 0, 0);
  detail_body = text(detail, fonts.body, TEXT, "");
  for (auto *label : {detail_title, detail_body}) {
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  }
  lv_obj_t *dismiss = button(detail, "Dismiss", 0x3A3F45, on_detail_dismiss, nullptr);
  lv_obj_set_width(dismiss, lv_pct(100));

  for (auto *obj : {toast, panel, detail, detail_image}) hide(obj, true);
  onx_notification::center.enabled = true;
  onx_notification::center.changed = []() { refresh(); };
  refresh();
}

inline void rebuild_list() {
  // Keep the empty-state label (the first child); drop the cards.
  while (lv_obj_get_child_count(list) > 1) lv_obj_delete(lv_obj_get_child(list, 1));
  const uint32_t now = esphome::millis();
  const int pad_l = S(16), pad_r = S(6), thumb = S(44), close_w = S(26);
  for (const auto &item : onx_notification::center.items) {
    lv_obj_t *card = box(list, surface(), S(12));
    lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_left(card, pad_l, 0);
    lv_obj_set_style_pad_right(card, pad_r, 0);
    lv_obj_set_style_pad_ver(card, S(10), 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(card, S(10), 0);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(card, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE));
    void *user = reinterpret_cast<void *>(static_cast<uintptr_t>(item.id));
    lv_obj_add_event_cb(card, on_item, LV_EVENT_CLICKED, user);
    // Accent bar along the left edge while unread.
    lv_obj_t *bar = box(card, item.read ? control() : accent(), S(2));
    lv_obj_add_flag(bar, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_pos(bar, -pad_l + S(7), 0);
    lv_obj_set_size(bar, S(3), lv_pct(100));
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    const bool picture = !item.image_url.empty();
    if (picture) {
      lv_obj_t *t = box(card, control(), S(8));
      lv_obj_set_size(t, thumb, thumb);
      lv_obj_remove_flag(t, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_center(text(t, fonts.symbol != nullptr ? fonts.symbol : fonts.small, muted(),
                         fonts.symbol != nullptr ? LV_SYMBOL_IMAGE : "Image"));
    }
    lv_obj_t *column = box(card, surface(), 0);
    lv_obj_set_style_bg_opa(column, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_grow(column, 1);
    lv_obj_set_height(column, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(column, S(2), 0);
    lv_obj_remove_flag(column, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(column, LV_OBJ_FLAG_EVENT_BUBBLE);
    // Title and time on one line.
    lv_obj_t *top_row = box(column, surface(), 0);
    lv_obj_set_style_bg_opa(top_row, LV_OPA_TRANSP, 0);
    lv_obj_set_size(top_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(top_row, S(6), 0);
    lv_obj_remove_flag(top_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(top_row, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_t *title = text(top_row, fonts.title, item.read ? muted() : foreground(), item.title.c_str());
    lv_obj_set_flex_grow(title, 1);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_height(title, lv_font_get_line_height(fonts.title));
    lv_obj_t *age = text(top_row, fonts.small, muted(), when(item.received_ms, now).c_str());
    // Up to two lines of the message.
    const int text_w = width - 2 * S(10) - pad_l - pad_r - close_w - S(10) - (picture ? thumb + S(10) : 0);
    lv_point_t size;
    lv_text_get_size(&size, item.body.c_str(), fonts.body, 0, 0, std::max(40, text_w), LV_TEXT_FLAG_NONE);
    const int body_lines = std::clamp(int((size.y + lv_font_get_line_height(fonts.body) - 1) / lv_font_get_line_height(fonts.body)), 1, 2);
    lv_obj_t *body = text(column, fonts.body, muted(), item.body.c_str());
    lv_obj_set_size(body, lv_pct(100), body_lines * lv_font_get_line_height(fonts.body));
    lv_label_set_long_mode(body, LV_LABEL_LONG_DOT);
    hide(body, item.body.empty());
    for (auto *label : {title, age, body}) lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    // Dismiss.
    lv_obj_t *close = box(card, surface(), S(13));
    lv_obj_set_style_bg_opa(close, LV_OPA_TRANSP, 0);
    lv_obj_set_size(close, close_w, close_w);
    lv_obj_add_flag(close, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(close, S(10));
    lv_obj_center(text(close, fonts.body, muted(), "\xC3\x97"));  // ×
    lv_obj_add_event_cb(close, on_item_dismiss, LV_EVENT_CLICKED, user);
  }
}

inline void open_detail(uint32_t id) {
  auto *item = onx_notification::center.find(id);
  if (item == nullptr) return;
  touched();
  panel_open = false;
  if (detail_id != 0 && hooks.release_image) hooks.release_image();
  detail_id = id;
  lv_label_set_text(detail_title, item->title.c_str());
  lv_label_set_text(detail_age, onx_notification::age_text(item->received_ms, esphome::millis()).c_str());
  lv_label_set_text(detail_body, item->body.c_str());
  hide(detail_image, true);
  if (!item->image_url.empty() && hooks.load_image) hooks.load_image(item->image_url);
  lv_obj_scroll_to_y(detail, 0, LV_ANIM_OFF);
  onx_notification::center.mark_read(id);  // Refreshes.
  refresh();
}

// Called when the picture for the open notification has downloaded.
inline void image_ready(const lv_image_dsc_t *dsc) {
  if (detail_id == 0 || dsc == nullptr) return;
  lv_image_set_src(detail_image, dsc);
  hide(detail_image, false);
}
inline void image_failed() { hide(detail_image, true); }

inline void refresh() {
  if (bell == nullptr) return;
  auto &center = onx_notification::center;
  const uint32_t now = esphome::millis();
  if (detail_id != 0 && center.find(detail_id) == nullptr) close_detail();
  const size_t unread = center.unread();
  // Bell only on the main carousel (dimmed when empty); every screen can pull
  // down from the top edge to open the centre.
  const bool bell_shown = bell_wanted && !panel_open && detail_id == 0;
  hide(bell, !bell_shown);
  lv_obj_set_style_opa(bell, center.items.empty() ? LV_OPA_50 : LV_OPA_COVER, 0);
  hide(badge, unread == 0 || !bell_shown);
  char count[8];
  snprintf(count, sizeof(count), "%u", static_cast<unsigned>(unread > 99 ? 99 : unread));
  lv_label_set_text(badge_label, count);
  lv_obj_align_to(badge, bell, LV_ALIGN_TOP_RIGHT, 0, 0);
  // Accent colour follows the device setting.
  lv_obj_set_style_border_color(toast, lv_color_hex(accent()), 0);
  lv_obj_set_style_text_color(detail_age, lv_color_hex(accent()), 0);
  char count_text[16] = "";
  if (!center.items.empty()) snprintf(count_text, sizeof(count_text), "\xC2\xB7 %u", static_cast<unsigned>(center.items.size()));
  lv_label_set_text(count_label, count_text);
  hide(clear_button, center.items.empty());
  lv_obj_set_style_bg_color(panel, lv_color_hex(bg()), 0);
  lv_obj_set_style_bg_color(clear_button, lv_color_hex(surface()), 0);

  const bool show_toast = center.toast_visible(now) && !panel_open && detail_id == 0;
  if (show_toast) {
    if (auto *item = center.find(center.toast_id)) {
      lv_label_set_text(toast_title, item->title.c_str());
      lv_label_set_text(toast_body, item->body.c_str());
    }
  }
  hide(toast, !show_toast);

  hide(panel, !panel_open);
  // Rebuilt on changes, and once a minute while open so the times stay current.
  static uint32_t built_minute = 0;
  const uint32_t minute = now / 60000;
  if (panel_open && (built_revision != center.revision || built_minute != minute)) {
    built_revision = center.revision;
    built_minute = minute;
    rebuild_list();
  }
  hide(empty_label, !center.items.empty());
  hide(detail, detail_id == 0);
  if (panel_open || detail_id != 0 || show_toast) lv_obj_move_foreground(panel_open ? panel : detail_id ? detail : toast);
  lv_obj_move_foreground(bell);
  lv_obj_move_foreground(badge);
}

// Touches on the visible bell go to LVGL rather than the gesture router.
inline bool bell_hit(int x, int y) {
  if (bell == nullptr || lv_obj_has_flag(bell, LV_OBJ_FLAG_HIDDEN)) return false;
  lv_area_t a;
  lv_obj_get_coords(bell, &a);
  return x >= a.x1 - 8 && x <= a.x2 + 8 && y >= a.y1 && y <= a.y2 + 8;
}
// Called by the panel's UI tick: the bell belongs to the main carousel only.
inline void set_bell_visible(bool visible) {
  if (bell == nullptr || visible == bell_wanted) return;
  bell_wanted = visible;
  refresh();
}

// Periodic: banner timeout and list ages.
inline void tick() {
  static uint32_t minute = 0;
  if (panel_open && esphome::millis() / 60000 != minute) {
    minute = esphome::millis() / 60000;
    refresh();
  }
  static bool toast_was_visible = false;
  const bool visible = onx_notification::center.toast_visible(esphome::millis());
  if (visible != toast_was_visible) {
    toast_was_visible = visible;
    refresh();
  }
}

}  // namespace onx_notification_ui
