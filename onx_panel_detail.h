#pragma once
#include "onx_panel_artwork.h"
#include "onx_grid_cache.h"
// Redesigned rectangular detail screens (touch panels only).
//
// Every entity detail fills the whole screen: there is no header bar, the
// left-edge swipe goes back and a pull from the top edge opens notifications.
// Each domain gets a direct-manipulation control (sliders, arcs, chips, lists)
// next to the existing swipe/knob step. Controls reuse the shared HA state
// (custom_logic_entity_sub::entity_detail) and the shared command queue
// (request_control + onx_controls), so pending/confirmation handling, the
// subscriptions and the knob/swipe input path stay authoritative.
//
// The screen is rebuilt only when its displayed data changes (signature), never
// while a finger is dragging one of its controls.
#include <algorithm>
#include <array>
#include <cstring>
#include <cmath>
#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "lvgl.h"
#include "esphome/core/hal.h"
#include "esphome/core/preferences.h"
#include "esphome/core/helpers.h"
#include "onx_render.h"
#include <new>
#include "esp_heap_caps.h"
#include "onx_panel_ui.h"
#include "onx_display_settings.h"
#include "onx_detail_view.h"
#include "onx_sensor_graph.h"
#include "onx_entity_config.h"
#include "onx_lyrics.h"

namespace onx_panel_detail {
using custom_logic_entity_sub::EntityDetailState;
using custom_logic_entity_sub::has_prefix;

// ------------------------------------------------------------ palette & fonts
inline uint32_t BG() {return onx_display_settings::background();}
inline uint32_t CARD() {return onx_display_settings::surface();}
inline uint32_t CONTROL() {return onx_display_settings::control_surface();}
inline uint32_t TRACK() {return onx_display_settings::track_surface();}
inline uint32_t TEXT() {return onx_display_settings::foreground();}
inline uint32_t SOFT_TEXT() {return onx_display_settings::muted_text();}
inline uint32_t MUTED() {return onx_display_settings::muted_text();}
inline uint32_t accent() { return onx_display_settings::accent(); }
inline uint32_t strong() { return onx_display_settings::accent_strong(); }
inline uint32_t soft() { return onx_display_settings::accent_soft(); }

// Pixel sizes on the 3.5" panel (the 2.8" uses the same fonts at 3/4 size,
// matching P()): small 16, body 20, title 24, big 43, hero 85 (digits only).
struct Fonts {
  const lv_font_t *small{nullptr}, *body{nullptr}, *title{nullptr}, *big{nullptr}, *symbol{nullptr}, *hero{nullptr},
      *label{nullptr}, *header_clock{nullptr};
};
inline Fonts fonts;

// Hooks supplied by the panel package (panel.yaml).
inline std::function<bool()> blocked;    // A swipe is in progress: ignore clicks.
inline std::function<void()> activity;   // Keep the screen awake.
inline std::function<esphome::onx_storage::NormalLightState(const std::string &)> light_state;
inline std::function<int()> light_mode;
inline std::function<void(int)> set_light_mode;
inline std::function<void(lv_obj_t *, const std::string &, int)> draw_graph;
// Optional shortcut (the slot's script/automation action) shown as a button.
inline std::function<std::string(const std::string &)> shortcut;
inline std::function<std::string(const std::string &)> shortcut_title;
inline std::function<void()> run_shortcut;
inline std::function<const onx_playlist::Config *(const std::string &)> quick_actions;
inline std::function<void(const std::string &)> run_quick_action;
inline std::function<void(const std::string &)> request_history;
inline std::function<void()> refresh_camera;
inline std::function<void(const std::string &, const std::string &, const std::string &)> send_ptz;

// ------------------------------------------------------------ geometry
inline int W = 320, H = 480;
// Layout is drawn for the 320 px short side of the 3.5" panel and scaled.
inline int P(int v) { return v * std::min(W, H) / 320; }
inline bool wide() { return W > H; }
struct Rect { int x, y, w, h; int right() const { return x + w; } int bottom() const { return y + h; } };
struct Tile {
  std::string entity, name, value, context, badge;
  bool on{false};
  int ring{-1};
  Rect r{};
  bool compact{false}, key{false}, icon_only{false};
  bool chip{false};  // Stack chip strip: icon and reading in a pill.
  bool slot{false};  // Stack position: an empty one shows a faint placeholder.
  uint64_t artwork{0};
  uint8_t effect{0};  // Action-grid icon animation while on: 1 pulse, 2 blink, 3 spin.
};
struct Layout { Rect head, vis, ctl; };
// Portrait: name/context, a visual block, controls below.
// Landscape: the visual column on the left, name and controls on the right.
inline Layout layout() {
  const int pad = P(16);
  Layout l{};
  if (wide()) {
    const int vw = W * 48 / 100;
    l.vis = {pad, pad, vw - pad, H - 2 * pad};
    const int x = vw + P(12), cw = W - x - pad;
    l.head = {x, pad, cw, P(48)};
    l.ctl = {x, pad + P(60), cw, H - 2 * pad - P(60)};
  } else {
    l.head = {pad, pad, W - 2 * pad, P(48)};
    l.vis = {pad, P(76), W - 2 * pad, H * 50 / 100};
    const int y = l.vis.bottom() + P(12);
    l.ctl = {pad, y, W - 2 * pad, H - pad - y};
  }
  return l;
}

// ------------------------------------------------------------ state
inline lv_obj_t *root = nullptr;
inline bool active = false;
inline bool dragging = false;
inline std::string built_signature;
inline std::vector<lv_obj_t *> targets;
struct Action { std::function<void(lv_event_t *)> fn; };
inline std::vector<std::unique_ptr<Action>> actions;
// Optimistic value after a drag until HA reports back (or a few seconds pass).
struct Pending { std::string entity, key; float value{NAN}; uint32_t until{0}; };
inline Pending pending;
inline uint32_t ran_at = 0;  // Scene/script/button press feedback ring.

inline float pending_value(const std::string &entity, const std::string &key) {
  if (pending.entity != entity || pending.key != key || int32_t(esphome::millis() - pending.until) > 0) return NAN;
  return pending.value;
}
inline void set_pending(const std::string &entity, const std::string &key, float value, uint32_t ms = 4000) {
  pending = {entity, key, value, esphome::millis() + ms};
}
inline void clear_pending_if(const std::string &entity, const std::string &key, float actual) {
  if (pending.entity == entity && pending.key == key && std::isfinite(actual) && std::fabs(actual - pending.value) < 0.51f)
    pending.until = esphome::millis();
}

// ------------------------------------------------------------ binary history
// Recent state changes of configured binary sensors, recorded on the device
// (history is not fetched from HA). Fed once a second by panel.yaml.
struct Change { std::string state; time_t at{0}; };
struct Changes { std::string entity, last; std::vector<Change> items; };
inline std::vector<Changes> changes;
// Scripts, scenes and buttons log their runs the same way: a script turning
// on, or a scene/button's state (its last-activated time) changing.
inline bool runnable_entity(const std::string &e) {
  return has_prefix(e, "script.") || has_prefix(e, "scene.") || has_prefix(e, "button.") || has_prefix(e, "input_button.") ||
         has_prefix(e, "event.");
}
// The recent changes survive reboots: a compact copy is kept in flash and
// written at most every ten minutes while something new was recorded.
struct StoredChanges {
  struct Item { char state[24]; uint32_t at; };
  struct Entry { char entity[64], last[40]; uint8_t count; Item items[5]; };
  uint8_t count;
  Entry entries[12];
};
inline bool changes_dirty = false, changes_loaded = false;
inline uint32_t changes_saved_ms = 0;
inline esphome::ESPPreferenceObject changes_pref() {
  return esphome::global_preferences->make_preference<StoredChanges>(esphome::fnv1_hash("onx.changes.v1"));
}
inline void load_changes() {
  changes_loaded = true;
  auto stored = std::make_unique<StoredChanges>();
  auto pref = changes_pref();
  if (!pref.load(stored.get())) return;
  for (uint8_t i = 0; i < std::min<uint8_t>(stored->count, 12); ++i) {
    auto &e = stored->entries[i];
    e.entity[sizeof(e.entity) - 1] = e.last[sizeof(e.last) - 1] = 0;
    Changes c{e.entity, e.last, {}};
    for (uint8_t j = 0; j < std::min<uint8_t>(e.count, 5); ++j) {
      e.items[j].state[sizeof(e.items[j].state) - 1] = 0;
      c.items.push_back({e.items[j].state, time_t(e.items[j].at)});
    }
    if (!c.entity.empty()) changes.push_back(std::move(c));
  }
}
inline void save_changes() {
  auto stored = std::make_unique<StoredChanges>();
  std::memset(stored.get(), 0, sizeof(StoredChanges));
  // Keep the most recently added entities.
  const size_t first = changes.size() > 12 ? changes.size() - 12 : 0;
  for (size_t i = first; i < changes.size(); ++i) {
    auto &e = stored->entries[stored->count++];
    snprintf(e.entity, sizeof(e.entity), "%s", changes[i].entity.c_str());
    snprintf(e.last, sizeof(e.last), "%s", changes[i].last.c_str());
    for (const auto &item : changes[i].items) {
      if (e.count >= 5) break;
      snprintf(e.items[e.count].state, sizeof(e.items[e.count].state), "%s", item.state.c_str());
      e.items[e.count++].at = uint32_t(item.at);
    }
  }
  auto pref = changes_pref();
  pref.save(stored.get());
  changes_dirty = false;
  changes_saved_ms = esphome::millis();
}
inline void track(time_t now) {
  if (now < 1577836800) return;
  if (!changes_loaded) load_changes();
  if (changes_dirty && esphome::millis() - changes_saved_ms >= 600000) save_changes();
  for (const auto &d : custom_logic_entity_sub::entity_details) {
    const bool runs = runnable_entity(d.entity_id);
    if ((!runs && !has_prefix(d.entity_id, "binary_sensor.")) || !d.received || !d.available) continue;
    auto it = std::find_if(changes.begin(), changes.end(), [&](const Changes &c) { return c.entity == d.entity_id; });
    if (it == changes.end()) {
      if (changes.size() >= 20) changes.erase(changes.begin());
      changes.push_back({d.entity_id, d.state, {}});
      continue;  // The first sample is not a change.
    }
    if (it->last == d.state) continue;
    it->last = d.state;
    if (runs && has_prefix(d.entity_id, "script.") && d.state != "on") continue;  // Only starts count.
    time_t at = now;
    if (runs && !has_prefix(d.entity_id, "script.")) {
      time_t stamp = 0;
      if (custom_logic_entity_sub::parse_ha_datetime_epoch(d.state, &stamp) && stamp <= now && stamp > now - 3600) at = stamp;
    }
    if (has_prefix(d.entity_id, "event.")) {
      time_t stamp = 0;
      if (custom_logic_entity_sub::parse_ha_datetime_epoch(d.state, &stamp) && stamp <= now) at = stamp;
      it->items.insert(it->items.begin(), {d.event_type.empty() ? std::string("event") : d.event_type, at});
    } else
    it->items.insert(it->items.begin(), {d.state, at});
    if (it->items.size() > (runs ? 16U : 5U)) it->items.pop_back();
    changes_dirty = true;
  }
}
inline const Changes *changes_for(const std::string &entity) {
  for (const auto &c : changes) if (c.entity == entity) return &c;
  return nullptr;
}

// ------------------------------------------------------------ widgets
inline void on_event(lv_event_t *e) {
  auto *a = static_cast<Action *>(lv_event_get_user_data(e));
  if (a != nullptr && a->fn) a->fn(e);
}
inline void on(lv_obj_t *obj, lv_event_code_t code, std::function<void(lv_event_t *)> fn) {
  actions.push_back(std::make_unique<Action>(Action{std::move(fn)}));
  lv_obj_add_event_cb(obj, on_event, code, actions.back().get());
}
// Pressed look, shown on the frame after touch-down (before the action runs on
// release): the surface blends toward the text colour; transparent targets
// get a faint wash. Only the touched widget's area is redrawn.
inline void pressed_look(lv_obj_t *obj, lv_style_selector_t selector) {
  lv_style_value_t bg, opa;
  const bool has_bg = lv_obj_get_local_style_prop(obj, LV_STYLE_BG_COLOR, &bg, 0) == LV_STYLE_RES_FOUND;
  const bool opaque = lv_obj_get_local_style_prop(obj, LV_STYLE_BG_OPA, &opa, 0) == LV_STYLE_RES_FOUND && opa.num > LV_OPA_50;
  const lv_color_t ink = lv_color_hex(TEXT());
  if (has_bg && opaque) {
    lv_obj_set_style_bg_color(obj, lv_color_mix(ink, bg.color, 56), selector);
  } else {
    lv_obj_set_style_bg_color(obj, ink, selector);
    lv_obj_set_style_bg_opa(obj, 36, selector);
  }
}
inline void interactive(lv_obj_t *obj, bool feedback = true) {
  lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  if (feedback) pressed_look(obj, LV_STATE_PRESSED);
  targets.push_back(obj);
  onx_panel_ui::touch_targets.push_back(obj);
}
// Cards on the main page are driven by onx_touch_input, not LVGL clicks; it
// reports touch-down/up here so the card under the finger reacts at once.
inline lv_obj_t *pressed_card = nullptr;
inline bool main_mode = false;
inline bool grid_cached_mode = false;
inline onx_grid_cache::GridPage *active_grid_page = nullptr;
inline lv_obj_t *grid_image = nullptr;
inline lv_obj_t *grid_press_overlay = nullptr;
inline std::vector<Tile> grid_tiles;
inline std::array<const void *, 16> grid_bound_srcs{};
inline std::array<lv_obj_t *, 16> grid_overlay_objs{};
inline int key_radius(const Rect &r) { return std::min(P(14), std::min(r.w, r.h) / 4); }
inline void press_at(int x, int y, bool pressed) {
  if (grid_press_overlay != nullptr) {
    lv_obj_add_flag(grid_press_overlay, LV_OBJ_FLAG_HIDDEN);
  }
  if (pressed_card != nullptr) {
    lv_obj_remove_state(pressed_card, LV_STATE_USER_1);
    pressed_card = nullptr;
  }
  if (!pressed || root == nullptr || !active || !main_mode || lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) return;
  if (grid_cached_mode) {
    for (const auto &t : grid_tiles) {
      if (t.entity.empty()) continue;
      if (x < t.r.x || x >= t.r.x + t.r.w || y < t.r.y || y >= t.r.y + t.r.h) continue;
      if (grid_press_overlay == nullptr) {
        grid_press_overlay = lv_obj_create(root);
        lv_obj_remove_flag(grid_press_overlay, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        lv_obj_set_style_pad_all(grid_press_overlay, 0, 0);
        lv_obj_set_style_shadow_width(grid_press_overlay, 0, 0);
        lv_obj_set_style_outline_width(grid_press_overlay, 0, 0);
      }
      const int rad = key_radius(t.r);
      lv_obj_set_pos(grid_press_overlay, t.r.x, t.r.y);
      lv_obj_set_size(grid_press_overlay, t.r.w, t.r.h);
      lv_obj_set_style_radius(grid_press_overlay, rad, 0);
      lv_obj_set_style_bg_color(grid_press_overlay, lv_color_hex(onx_display_settings::light_background() ? 0x000000 : 0xFFFFFF), 0);
      lv_obj_set_style_bg_opa(grid_press_overlay, LV_OPA_20, 0);
      lv_obj_set_style_border_width(grid_press_overlay, std::max(2, P(2)), 0);
      lv_obj_set_style_border_color(grid_press_overlay, lv_color_hex(accent()), 0);
      lv_obj_remove_flag(grid_press_overlay, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(grid_press_overlay);
      return;
    }
    return;
  }
  const lv_point_t p{x, y};
  for (int i = static_cast<int>(lv_obj_get_child_count(root)) - 1; i >= 0; --i) {
    lv_obj_t *c = lv_obj_get_child(root, i);
    if (lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN)) continue;
    lv_area_t a;
    lv_obj_get_coords(c, &a);
    if (p.x < a.x1 || p.x > a.x2 || p.y < a.y1 || p.y > a.y2 || lv_area_get_width(&a) < 40 || lv_area_get_height(&a) < 40) continue;
    pressed_look(c, LV_STATE_USER_1);
    lv_obj_add_state(c, LV_STATE_USER_1);
    pressed_card = c;
    return;
  }
}
inline bool allowed() {
  onx_panel_ui::request_render();
  if (blocked && blocked()) return false;
  if (activity) activity();
  return true;
}
inline lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius, lv_opa_t opa = LV_OPA_COVER) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_flag(o, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(o, opa, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_set_style_shadow_width(o, 0, 0);
  lv_obj_set_style_outline_width(o, 0, 0);
  return o;
}
// One line of text. With a width it is truncated with an ellipsis: LVGL's
// LONG_DOT only truncates when the height is fixed, otherwise it wraps and the
// extra lines overlap whatever is drawn below.
inline lv_obj_t *text(lv_obj_t *parent, const std::string &s, const lv_font_t *font, uint32_t color, int x, int y,
                      int w = -1, lv_text_align_t align = LV_TEXT_ALIGN_LEFT, int lines = 1) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_style_text_line_space(l, 0, 0);
  lv_label_set_text(l, s.c_str());
  if (w > 0) {
    lv_obj_set_size(l, w, lv_font_get_line_height(font) * std::max(1, lines));
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(l, align, 0);
  }
  lv_obj_set_pos(l, x, y);
  lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
  return l;
}
inline int line(const lv_font_t *font) { return lv_font_get_line_height(font); }
inline int text_width(const std::string &s, const lv_font_t *font) {
  lv_point_t size;
  lv_text_get_size(&size, s.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return size.x;
}
// Lines `s` needs at width `w` (at most `max`).
inline int lines_for(const std::string &s, const lv_font_t *font, int w, int max) {
  lv_point_t size;
  lv_text_get_size(&size, s.c_str(), font, 0, 0, w, LV_TEXT_FLAG_NONE);
  return std::clamp(int((size.y + line(font) - 1) / std::max(1, int(line(font)))), 1, max);
}
// The largest of `options` in which `s` fits on one line of width `w`.
inline const lv_font_t *fit(const std::string &s, int w, std::initializer_list<const lv_font_t *> options) {
  const lv_font_t *last = nullptr;
  for (const auto *f : options) {
    if (f == nullptr) continue;
    last = f;
    if (text_width(s, f) <= w) return f;
  }
  return last;
}
inline lv_obj_t *button(lv_obj_t *parent, int x, int y, int w, int h, int radius, uint32_t bg,
                        const std::string &caption, const lv_font_t *font, uint32_t fg, std::function<void()> fn) {
  if(bg==accent()||bg==strong())fg=onx_display_settings::light_color(bg)?0x161B22:0xFFFFFF;
  lv_obj_t *b = box(parent, x, y, w, h, bg, radius);
  interactive(b);
  if (!caption.empty()) {
    lv_obj_t *l = text(b, caption, font, fg, 0, 0, w - P(8), LV_TEXT_ALIGN_CENTER);
    lv_obj_center(l);
  }
  on(b, LV_EVENT_CLICKED, [fn](lv_event_t *) { if (allowed()) fn(); });
  return b;
}
inline lv_obj_t *icon_button(lv_obj_t *parent, int x, int y, int w, int h, int radius, uint32_t bg,
                             const char *symbol, uint32_t fg, std::function<void()> fn) {
  lv_obj_t *b = button(parent, x, y, w, h, radius, bg, "", fonts.symbol, fg, std::move(fn));
  if (bg == accent() || bg == strong()) fg = onx_display_settings::light_color(bg) ? 0x161B22 : 0xFFFFFF;
  lv_obj_t *l = text(b, symbol, fonts.symbol, fg, 0, 0);
  lv_obj_center(l);
  return b;
}
// Short confirmation near the bottom of the screen (e.g. "Turning camera on"),
// hidden again after 1.5 s. Lives on the top layer so rebuilds keep it.
inline lv_obj_t *toast_box = nullptr;
inline lv_obj_t *toast_text = nullptr;
inline lv_timer_t *toast_timer = nullptr;
inline void toast(const std::string &message) {
  if (toast_box == nullptr) {
    toast_box = lv_obj_create(lv_layer_top());
    lv_obj_remove_flag(toast_box, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_style_bg_color(toast_box, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(toast_box, LV_OPA_80, 0);
    lv_obj_set_style_border_width(toast_box, 0, 0);
    lv_obj_set_style_radius(toast_box, P(14), 0);
    lv_obj_set_style_pad_hor(toast_box, P(14), 0);
    lv_obj_set_style_pad_ver(toast_box, P(8), 0);
    lv_obj_set_size(toast_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    toast_text = lv_label_create(toast_box);
    lv_obj_set_style_text_font(toast_text, fonts.small, 0);
    lv_obj_set_style_text_color(toast_text, lv_color_hex(0xFFFFFF), 0);
    toast_timer = lv_timer_create([](lv_timer_t *t) {
      if (toast_box != nullptr) lv_obj_add_flag(toast_box, LV_OBJ_FLAG_HIDDEN);
      lv_timer_pause(t);
    }, 1500, nullptr);
  }
  lv_label_set_text(toast_text, message.c_str());
  lv_obj_align(toast_box, LV_ALIGN_BOTTOM_MID, 0, -P(24));
  lv_obj_remove_flag(toast_box, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(toast_box);
  lv_timer_reset(toast_timer);
  lv_timer_resume(toast_timer);
}
inline lv_obj_t *chip(lv_obj_t *parent, int x, int y, int w, int h, const std::string &caption, bool selected,
                      std::function<void()> fn) {
  lv_obj_t *b = button(parent, x, y, w, h, h / 2, selected ? soft() : CONTROL(), caption, fonts.small,
                       selected ? accent() : SOFT_TEXT(), std::move(fn));
  if (selected) {
    lv_obj_set_style_border_width(b, P(2), 0);
    lv_obj_set_style_border_color(b, lv_color_hex(accent()), 0);
  }
  return b;
}
// Equal-width chips across a row.
inline void chips(lv_obj_t *parent, const Rect &r, const std::vector<std::string> &labels, int selected,
                  std::function<void(int)> fn) {
  if (labels.empty()) return;
  const int gap = P(8), n = labels.size(), w = (r.w - gap * (n - 1)) / n;
  for (int i = 0; i < n; ++i)
    chip(parent, r.x + i * (w + gap), r.y, w, r.h, labels[i], i == selected, [fn, i]() { fn(i); });
}
// A labelled row (name left, value right); clickable when fn is given.
inline lv_obj_t *row(lv_obj_t *parent, const Rect &r, const std::string &name, const std::string &value,
                     std::function<void()> fn = nullptr) {
  lv_obj_t *b = box(parent, r.x, r.y, r.w, r.h, CARD(), P(10));
  const int pad = P(12), lh = line(fonts.small);
  text(b, name, fonts.small, MUTED(), pad, (r.h - lh) / 2, r.w / 2 - pad);
  text(b, value, fonts.small, TEXT(), r.w / 2, (r.h - lh) / 2, r.w / 2 - pad, LV_TEXT_ALIGN_RIGHT);
  if (fn) {
    interactive(b);
    on(b, LV_EVENT_CLICKED, [fn](lv_event_t *) { if (allowed()) fn(); });
  }
  return b;
}
// A vertical value tile: small muted name above a value.
inline void tile(lv_obj_t *parent, const Rect &r, const std::string &name, const std::string &value, uint32_t bg = CARD()) {
  lv_obj_t *b = box(parent, r.x, r.y, r.w, r.h, bg, P(12));
  const int pad = P(10), iw = r.w - 2 * pad;
  const int gap = std::max(0, (r.h - line(fonts.label) - line(fonts.body)) / 3);
  text(b, name, fonts.label, MUTED(), pad, gap, iw);
  text(b, value, fit(value, iw, {fonts.body, fonts.small}), TEXT(), pad, r.h - gap - line(fonts.body), iw);
}
inline lv_obj_t *circle(lv_obj_t *parent, int cx, int cy, int size, uint32_t bg, const char *symbol, uint32_t fg,
                       std::function<void()> fn) {
  lv_obj_t *b = button(parent, cx - size / 2, cy - size / 2, size, size, LV_RADIUS_CIRCLE, bg, "", fonts.symbol, fg, std::move(fn));
  if(bg==accent()||bg==strong())fg=onx_display_settings::light_color(bg)?0x161B22:0xFFFFFF;
  lv_obj_t *l = text(b, symbol, fonts.symbol, fg, 0, 0);
  lv_obj_center(l);
  return b;
}
// Name (shrinks to fit before truncating) with a muted context line below.
inline int header_reserve = 0;  // Room kept free at the top right (quick actions).
inline void header(const Rect &r, const std::string &name, const std::string &context, int reserve = 0) {
  const int w = r.w - std::max(reserve, header_reserve);
  const lv_font_t *font = fit(name, w, {fonts.title, fonts.body});
  text(root, name, font, TEXT(), r.x, r.y, w);
  if (!context.empty()) text(root, context, fonts.small, MUTED(), r.x, r.y + line(font) + P(2), w);
}
inline lv_point_t point() {
  lv_point_t p{0, 0};
  if (lv_indev_t *indev = lv_indev_active()) lv_indev_get_point(indev, &p);
  return p;
}
// Shared drag plumbing for custom sliders: live preview, commit on release.
// Like LVGL's own slider and arc, the control must not chain the drag to its
// parents: the top layer is scrollable (elastic), so a vertical drag started
// scrolling it, the control lost the press and the value was never sent.
// A lost press (finger slid off, a screen change) still commits the last value.
inline void drag(lv_obj_t *obj, std::function<void(lv_point_t, bool commit)> fn) {
  interactive(obj, false);
  lv_obj_remove_flag(obj, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN));
  lv_obj_add_flag(obj, LV_OBJ_FLAG_PRESS_LOCK);
  auto last = std::make_shared<lv_point_t>(lv_point_t{0, 0});
  on(obj, LV_EVENT_PRESSED, [fn, last](lv_event_t *) {
    if (blocked && blocked()) return;
    dragging = true;
    *last = point();
    fn(*last, false);
  });
  on(obj, LV_EVENT_PRESSING, [fn, last](lv_event_t *) {
    if (!dragging) return;
    *last = point();
    fn(*last, false);
  });
  auto finish = [fn, last](lv_event_t *e) {
    if (!dragging) return;
    dragging = false;
    onx_panel_ui::request_render();
    if (activity) activity();
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) *last = point();
    fn(*last, true);
  };
  on(obj, LV_EVENT_RELEASED, finish);
  on(obj, LV_EVENT_PRESS_LOST, finish);
}

// ------------------------------------------------------------ text helpers
inline std::string fmt(float v, int decimals = -1) {
  if (!std::isfinite(v)) return "—";
  char out[24];
  if (decimals < 0) return onx_detail_view::compact(std::to_string(v));
  snprintf(out, sizeof(out), "%.*f", decimals, v);
  return out;
}
// In-place updates. A builder may leave `patch`, which refreshes its widgets'
// values without rebuilding the screen; it is used while the screen's
// structure (`structure()`) is unchanged and returns false to force a rebuild.
inline std::function<bool()> patch;
inline std::string built_structure;
inline std::string pct(float v) { return std::isfinite(v) ? std::to_string(int(std::lround(v))) + "%" : "—"; }
inline float num(const std::string &s) { return custom_logic_entity_sub::detail_number(s); }
inline std::string label(const std::string &s) { return onx_detail_view::label(s); }
inline std::string clock(time_t t) {
  struct tm local{};
  localtime_r(&t, &local);
  char out[16];
  strftime(out, sizeof(out), onx_display_settings::time_pattern(), &local);
  return out;
}
inline std::string ago(time_t then, time_t now) {
  if (then <= 0 || now < then) return "";
  const long m = (now - then) / 60;
  if (m < 1) return "for less than a minute";
  if (m < 60) return "for " + std::to_string(m) + " min";
  return "for " + std::to_string(m / 60) + " h " + std::to_string(m % 60) + " min";
}
inline void request(const std::string &entity, const std::string &service, const std::string &parameter,
                    const std::string &value, const std::string &field = "") {
  custom_logic_entity_sub::select_control_entity(entity);
  std::vector<onx_control::Value> values{{field.empty() ? "panel_" + parameter : field, parameter, value}};
  // Setting brightness or colour also switches the light on: show it on at
  // once instead of waiting for Home Assistant's state.
  if (service == "light.turn_on" && field != "state") values.push_back({"state", "", "on"});
  custom_logic_entity_sub::request_control(entity, service, std::move(values));
}
// The state just requested on the panel (shown at once, until Home Assistant
// confirms or the request expires), else the reported one.
inline std::string shown_state(const std::string &entity, const std::string &actual) {
  if (custom_logic_entity_sub::control_entity != entity) return actual;
  return custom_logic_entity_sub::requested_value("state", actual);
}

// ------------------------------------------------------------ domains
// Light: Home Assistant style. Name, large brightness and state, one large
// control for the selected mode (brightness slider, colour-temperature slider or
// colour wheel) and a pill bar along the bottom with power and the mode buttons.
// Values are previewed while dragging and sent on release.
inline const void *light_wheel = nullptr;                    // Colour wheel image (panel.yaml).
inline std::array<const void *, 3> light_mode_icons{};       // Brightness, white, colour.
inline void light_send_hs(const std::string &e, float hue, float saturation) {
  const int h = int(std::lround(hue)) % 360, sat = std::clamp(int(std::lround(saturation)), 0, 100);
  set_pending(e, "hs", float(h * 1000 + sat));
  // A list value: sent as a template so Home Assistant receives [hue, saturation].
  request(e, "light.turn_on", "hs_color", "[" + std::to_string(h) + ", " + std::to_string(sat) + "]", "panel_colour");
}
inline bool light_shown_on(const std::string &e) {
  const auto *d = custom_logic_entity_sub::entity_detail(e);
  if (d == nullptr || !d->available || !light_state) return false;
  return shown_state(e, light_state(e).is_on ? "on" : "off") == "on";
}
inline void light_toggle(const std::string &e) {
  const bool on = light_shown_on(e);
  request(e, "light.toggle", "", on ? "off" : "on", "state");
}
// What a light screen's layout depends on; values (on, brightness, colour)
// are patched in place while this stays the same.
inline std::string light_structure(const std::string &e, const EntityDetailState &d, const std::string &title) {
  if (!light_state) return "";
  const auto s = light_state(e);
  return "light|" + e + '|' + title + '|' + std::to_string(W) + 'x' + std::to_string(H) + '|' + std::to_string(accent()) + '|' +
         std::to_string(BG()) + '|' + std::to_string(d.available) + std::to_string(d.received) + '|' +
         std::to_string(s.supports_brightness()) + std::to_string(s.supports_color_temperature()) + std::to_string(s.supports_rgb()) + '|' +
         std::to_string(s.min_color_temperature_kelvin) + '-' + std::to_string(s.max_color_temperature_kelvin) + '|' +
         std::to_string(light_mode ? light_mode() : 0) + '|' + std::to_string(onx_playlist::browser.open);
}
// Brightness fill colour: the light's own colour (or white temperature) when
// Home Assistant reports one, otherwise the theme accent.
inline uint32_t light_fill_color(const std::string &e, const EntityDetailState &d, const esphome::onx_storage::NormalLightState &s) {
  float hue = std::isfinite(d.light_hue) ? d.light_hue : NAN, sat = std::isfinite(d.light_saturation) ? d.light_saturation : NAN;
  const float packed = pending_value(e, "hs");
  if (std::isfinite(packed)) { hue = std::floor(packed / 1000); sat = packed - hue * 1000; }
  const float k = pending_value(e, "kelvin");
  if (std::isfinite(k)) return onx_light::kelvin(int(k));
  if (std::isfinite(hue) && std::isfinite(sat) && sat >= 1) return onx_light::hsv(hue, std::max(sat, 35.0f));
  if (s.color_temperature_kelvin) return onx_light::kelvin(s.color_temperature_kelvin);
  return accent();
}
__attribute__((noinline)) inline void build_light(const Layout &l, const std::string &e, const EntityDetailState &d, const std::string &title) {
  const auto s = light_state ? light_state(e) : esphome::onx_storage::NormalLightState{};
  const bool on = d.available && (s.is_on ? shown_state(e, "on") : shown_state(e, "off")) == "on";
  struct LightWidgets { lv_obj_t *value{}, *power{}, *power_icon{}, *big{}, *big_icon{}, *fill{}, *knob{}, *handle{}, *marker{}; int sh{}, sw{}, lo{}, hi{}, radius{}, ms{}; };
  auto w = std::make_shared<LightWidgets>();
  const bool dimmable = s.supports_brightness();
  float level = on ? s.brightness_pct : 0;
  const float want = pending_value(e, "brightness");
  clear_pending_if(e, "brightness", level);
  if (std::isfinite(want)) level = want;
  // Modes this light offers: 0 brightness, 1 white (colour temperature), 2 colour.
  std::vector<int> modes;
  if (dimmable) modes.push_back(0);
  if (s.supports_color_temperature()) modes.push_back(1);
  if (s.supports_rgb()) modes.push_back(2);
  int mode = light_mode ? light_mode() : 0;
  if (std::find(modes.begin(), modes.end(), mode) == modes.end()) mode = modes.empty() ? -1 : modes.front();

  const int pad = P(16), bar_h = P(56);
  // Portrait: everything stacked. Landscape: the control on the left, the
  // name, value and mode bar on the right.
  const Rect info = wide() ? Rect{l.head.x, pad, l.head.w, H - 2 * pad} : Rect{pad, pad, W - 2 * pad, H - 2 * pad};
  text(root, title, fit(title, info.w, {fonts.title, fonts.body}), TEXT(), info.x, info.y, info.w);
  int y = info.y + line(fonts.title) + P(10);
  lv_obj_t *value = text(root, !d.available ? "—" : dimmable ? (on ? pct(level) : "Off") : (on ? "On" : "Off"), fonts.big, TEXT(),
                         info.x, y, info.w, wide() ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER);
  y += line(fonts.big) + P(12);

  // Mode bar: power | modes.
  const int bs = P(44), gap = P(6), n = modes.size() > 1 ? modes.size() : 0;
  const int bar_w = P(8) * 2 + bs + (n ? P(17) + n * bs + (n - 1) * gap : 0);
  // The bottom of the control column leaves room for a shortcut button (render()).
  const int bar_x = info.x + (info.w - bar_w) / 2, bar_y = l.ctl.bottom() - bar_h;
  lv_obj_t *bar = box(root, bar_x, bar_y, bar_w, bar_h, CARD(), bar_h / 2);
  const int by = (bar_h - bs) / 2;
  lv_obj_t *power = button(bar, P(8), by, bs, bs, LV_RADIUS_CIRCLE, on ? strong() : CONTROL(), "", fonts.small, TEXT(),
                           [e]() { light_toggle(e); });
  lv_obj_t *power_icon = text(power, LV_SYMBOL_POWER, fonts.symbol,
                              on ? (onx_display_settings::light_color(strong()) ? 0x161B22 : 0xFFFFFF) : SOFT_TEXT(), 0, 0);
  lv_obj_center(power_icon);
  w->value = value; w->power = power; w->power_icon = power_icon;
  // Values change in place: on/off, brightness, colour temperature and hue.
  patch = [e, w]() {
    const auto *dd = custom_logic_entity_sub::entity_detail(e);
    if (dd == nullptr || !light_state) return false;
    const auto st = light_state(e);
    const bool now_on = light_shown_on(e);
    float lv = now_on ? st.brightness_pct : 0;
    const float want_level = pending_value(e, "brightness");
    clear_pending_if(e, "brightness", lv);
    if (std::isfinite(want_level)) lv = want_level;
    const uint32_t on_ink = onx_display_settings::light_color(strong()) ? 0x161B22 : 0xFFFFFF;
    onx_render::label(w->value, !dd->available ? "—" : st.supports_brightness() ? (now_on ? pct(lv).c_str() : "Off") : (now_on ? "On" : "Off"));
    for (auto [btn, icon] : {std::pair{w->power, w->power_icon}, std::pair{w->big, w->big_icon}}) {
      if (btn == nullptr) continue;
      lv_obj_set_style_bg_color(btn, lv_color_hex(now_on ? strong() : CONTROL()), 0);
      pressed_look(btn, LV_STATE_PRESSED);
      if (icon) lv_obj_set_style_text_color(icon, lv_color_hex(now_on ? on_ink : SOFT_TEXT()), 0);
    }
    if (w->fill && !dragging) {
      const int h = int(w->sh * std::clamp(lv, 0.0f, 100.0f) / 100);
      lv_obj_set_pos(w->fill, 0, w->sh - h);
      lv_obj_set_style_bg_color(w->fill, lv_color_hex(light_fill_color(e, *dd, st)), 0);
      lv_obj_set_size(w->fill, w->sw, h);
      if (w->knob) lv_obj_set_y(w->knob, std::clamp(w->sh - h + P(12), P(8), w->sh - P(5) - P(8)));
    }
    if (w->handle && !dragging) {
      float k = st.color_temperature_kelvin ? st.color_temperature_kelvin : (w->lo + w->hi) / 2;
      const float wanted = pending_value(e, "kelvin");
      if (std::isfinite(wanted)) k = wanted;
      const int hh = P(6);
      lv_obj_set_y(w->handle, std::clamp(int(w->sh * (1 - (k - w->lo) / float(w->hi - w->lo))) - hh / 2, P(8), w->sh - hh - P(8)));
    }
    if (w->marker && !dragging) {
      float hue = std::isfinite(dd->light_hue) ? dd->light_hue : st.hue_degrees();
      float sat = std::isfinite(dd->light_saturation) ? dd->light_saturation : 100;
      const float packed = pending_value(e, "hs");
      if (std::isfinite(packed)) { hue = std::floor(packed / 1000); sat = packed - hue * 1000; }
      if (!now_on) sat = 0;
      const float a = hue * float(M_PI) / 180, r = std::clamp(sat, 0.0f, 100.0f) / 100 * (w->radius - w->ms / 4);
      lv_obj_set_pos(w->marker, w->radius + int(std::cos(a) * r) - w->ms / 2, w->radius + int(std::sin(a) * r) - w->ms / 2);
      lv_obj_set_style_bg_color(w->marker, lv_color_hex(onx_light::hsv(hue, sat)), 0);
    }
    return true;
  };
  if (n) {
    int x = P(8) + bs + P(8);
    box(bar, x, (bar_h - P(28)) / 2, 1, P(28), TRACK(), 0);
    x += P(9);
    for (int m : modes) {
      const bool selected = m == mode;
      lv_obj_t *b = button(bar, x, by, bs, bs, LV_RADIUS_CIRCLE, selected ? CONTROL() : CARD(), "", fonts.small, TEXT(),
                           [m]() { if (set_light_mode) set_light_mode(m); });
      if (selected) {
        lv_obj_set_style_border_width(b, P(2), 0);
        lv_obj_set_style_border_color(b, lv_color_hex(TEXT()), 0);
      }
      if (light_mode_icons[m]) {
        lv_obj_t *img = lv_image_create(b);
        lv_image_set_src(img, light_mode_icons[m]);
        lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_center(img);
      }
      x += bs + gap;
    }
  }

  // Colour presets: a scrollable row of swatches above the mode bar.
  struct Swatch { uint32_t rgb; int kelvin; };
  std::vector<Swatch> swatches;
  if (d.available && (s.supports_rgb() || s.supports_color_temperature())) {
    const int lo = s.min_color_temperature_kelvin ? s.min_color_temperature_kelvin : 2000;
    const int hi = s.max_color_temperature_kelvin > lo ? s.max_color_temperature_kelvin : 6500;
    for (int k : {2000, 3500, 5000, 6500}) {
      const int kk = std::clamp(k, lo, hi);
      swatches.push_back({onx_light::kelvin(kk), s.supports_color_temperature() ? kk : 0});
    }
    if (s.supports_rgb())
      for (uint32_t c : {0x7FACFFu, 0xD796FFu, 0xFF9EF3u, 0xFF6E54u, 0xFF3B30u, 0x34C759u, 0x0A84FFu, 0xFFD60Au})
        swatches.push_back({c, 0});
  }
  int control_bottom = bar_y - P(16);
  // Landscape: the row scrolls inside the right column, between the value and
  // the mode bar.
  const int row_x = wide() ? info.x : 0, row_w = wide() ? info.w : W;
  if (!swatches.empty() && bar_y - P(12) - (P(48) + (wide() ? 0 : P(60))) >= y) {
    const int ss = P(40), sg = P(12), rh = ss + P(8), ry = bar_y - P(12) - rh;
    lv_obj_t *row = box(root, row_x, ry, row_w, rh, BG(), 0, LV_OPA_TRANSP);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(row, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    const int total = int(swatches.size()) * (ss + sg) - sg;
    const int inset = wide() ? P(4) : pad;
    int x = std::max(inset, (row_w - total) / 2);
    for (const auto &sw : swatches) {
      const uint32_t rgb = sw.rgb; const int k = sw.kelvin;
      lv_obj_t *b = button(row, x, P(4), ss, ss, LV_RADIUS_CIRCLE, rgb, "", fonts.small, TEXT(), [e, rgb, k]() {
        // Show the chosen colour at once (the fill follows it) until HA reports back.
        if (k) { set_pending(e, "kelvin", k); request(e, "light.turn_on", "color_temp_kelvin", std::to_string(k), "panel_colour"); }
        else {
          const float r = (rgb >> 16 & 255) / 255.0f, g = (rgb >> 8 & 255) / 255.0f, bl = (rgb & 255) / 255.0f;
          const float mx = std::max({r, g, bl}), mn = std::min({r, g, bl}), c = mx - mn;
          float h = c <= 0 ? 0 : mx == r ? 60 * std::fmod((g - bl) / c + 6, 6.0f) : mx == g ? 60 * ((bl - r) / c + 2) : 60 * ((r - g) / c + 4);
          set_pending(e, "hs", float(int(std::lround(h)) % 360 * 1000 + int(std::lround(mx > 0 ? c / mx * 100 : 0))));
          char v[24];
          std::snprintf(v, sizeof(v), "[%u, %u, %u]", unsigned(rgb >> 16 & 255), unsigned(rgb >> 8 & 255), unsigned(rgb & 255));
          request(e, "light.turn_on", "rgb_color", v, "panel_colour");
        }
      });
      lv_obj_set_style_border_width(b, 1, 0);
      lv_obj_set_style_border_color(b, lv_color_hex(TRACK()), 0);
      x += ss + sg;
    }
    (void)x;  // Wider than the row: it scrolls.
    control_bottom = ry - P(12);
  }
  // The large control.
  const Rect vis = wide() ? l.vis : Rect{pad, y, W - 2 * pad, control_bottom - y};
  if (!d.available || vis.h < P(60)) return;
  if (mode < 0) {  // On/off only: one large power button.
    const int size = std::min({vis.w, vis.h, P(150)});
    w->big = circle(root, vis.x + vis.w / 2, vis.y + vis.h / 2, size, on ? strong() : CONTROL(), LV_SYMBOL_POWER,
                    on ? TEXT() : SOFT_TEXT(), [e]() { light_toggle(e); });
    w->big_icon = lv_obj_get_child(w->big, -1);
    return;
  }
  if (mode == 2) {
    const int size = std::min({vis.w, vis.h, P(220)});
    const int x0 = vis.x + (vis.w - size) / 2, y0 = vis.y + (vis.h - size) / 2;
    lv_obj_t *wheel = box(root, x0, y0, size, size, CONTROL(), LV_RADIUS_CIRCLE, light_wheel ? LV_OPA_TRANSP : LV_OPA_COVER);
    if (light_wheel) {
      lv_obj_t *img = lv_image_create(wheel);
      lv_image_set_src(img, light_wheel);
      lv_image_header_t h{};
      if (lv_image_decoder_get_info(light_wheel, &h) == LV_RESULT_OK && h.w && h.w != uint32_t(size)) {
        lv_image_set_pivot(img, h.w / 2, h.h / 2);
        lv_image_set_scale(img, size * 256 / h.w);
      }
      lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_center(img);
    }
    float hue = std::isfinite(d.light_hue) ? d.light_hue : s.hue_degrees();
    float sat = std::isfinite(d.light_saturation) ? d.light_saturation : 100;
    const float packed = pending_value(e, "hs");
    if (std::isfinite(packed)) { hue = std::floor(packed / 1000); sat = packed - hue * 1000; }
    const int ms = P(30), radius = size / 2;
    auto marker_at = [radius, ms](lv_obj_t *marker, float h, float sa) {
      const float a = h * float(M_PI) / 180, r = std::clamp(sa, 0.0f, 100.0f) / 100 * (radius - ms / 4);
      lv_obj_set_pos(marker, radius + int(std::cos(a) * r) - ms / 2, radius + int(std::sin(a) * r) - ms / 2);
      lv_obj_set_style_bg_color(marker, lv_color_hex(onx_light::hsv(h, sa)), 0);
    };
    lv_obj_t *marker = box(wheel, 0, 0, ms, ms, 0xFFFFFF, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(marker, P(3), 0);
    lv_obj_set_style_border_color(marker, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_shadow_width(marker, P(8), 0);
    lv_obj_set_style_shadow_opa(marker, LV_OPA_40, 0);
    marker_at(marker, hue, on ? sat : 0);
    w->marker = marker; w->radius = radius; w->ms = ms;
    drag(wheel, [e, wheel, marker, marker_at, radius, ms](lv_point_t p, bool commit) {
      lv_area_t a;
      lv_obj_get_coords(wheel, &a);
      const float dx = p.x - (a.x1 + radius), dy = p.y - (a.y1 + radius);
      float h = std::atan2(dy, dx) * 180 / float(M_PI);
      if (h < 0) h += 360;
      const float sa = std::clamp(std::sqrt(dx * dx + dy * dy) / float(radius - ms / 4), 0.0f, 1.0f) * 100;
      marker_at(marker, h, sa);
      if (commit) light_send_hs(e, h, sa);
    });
    return;
  }
  // Vertical sliders (brightness, colour temperature).
  const int sw = std::min(P(124), vis.w), sh = vis.h, sx = vis.x + (vis.w - sw) / 2, sy = vis.y;
  lv_obj_t *track = box(root, sx, sy, sw, sh, CONTROL(), P(30));
  lv_obj_set_style_clip_corner(track, true, 0);
  const uint32_t ink = onx_display_settings::light_color(accent()) ? 0x161B22 : 0xFFFFFF;
  if (mode == 1) {
    const int lo = s.min_color_temperature_kelvin ? s.min_color_temperature_kelvin : 2200;
    const int hi = s.max_color_temperature_kelvin > lo ? s.max_color_temperature_kelvin : 6500;
    // Cool at the top, warm at the bottom.
    lv_obj_set_style_bg_color(track, lv_color_hex(onx_light::kelvin(hi)), 0);
    lv_obj_set_style_bg_grad_color(track, lv_color_hex(onx_light::kelvin(lo)), 0);
    lv_obj_set_style_bg_grad_dir(track, LV_GRAD_DIR_VER, 0);
    float kelvin = s.color_temperature_kelvin ? s.color_temperature_kelvin : (lo + hi) / 2;
    const float wanted = pending_value(e, "kelvin");
    if (std::isfinite(wanted)) kelvin = wanted;
    const int hh = P(6);
    auto handle_y = [sh, hh, lo, hi](float k) { return std::clamp(int(sh * (1 - (k - lo) / float(hi - lo))) - hh / 2, P(8), sh - hh - P(8)); };
    lv_obj_t *handle = box(track, sw / 4, handle_y(kelvin), sw / 2, hh, 0x161B22, hh / 2);
    w->handle = handle; w->sh = sh; w->lo = lo; w->hi = hi;
    lv_obj_set_style_border_width(handle, P(2), 0);
    lv_obj_set_style_border_color(handle, lv_color_hex(0xFFFFFF), 0);
    drag(track, [e, track, handle, handle_y, sh, lo, hi](lv_point_t p, bool commit) {
      lv_area_t a;
      lv_obj_get_coords(track, &a);
      const float f = std::clamp((a.y2 - p.y) / float(sh), 0.0f, 1.0f);
      const int k = (lo + int(std::lround((hi - lo) * f))) / 50 * 50;
      lv_obj_set_y(handle, handle_y(k));
      if (!commit) return;
      set_pending(e, "kelvin", k);
      request(e, "light.turn_on", "color_temp_kelvin", std::to_string(k), "panel_colour");
    });
    return;
  }
  const int fill_h = int(sh * std::clamp(level, 0.0f, 100.0f) / 100);
  lv_obj_t *fill = box(track, 0, sh - fill_h, sw, fill_h, light_fill_color(e, d, s), 0);
  const int kh = P(5), kw = sw * 30 / 100;
  auto knob_y = [sh, kh](int h) { return std::clamp(sh - h + P(12), P(8), sh - kh - P(8)); };
  lv_obj_t *knob = box(track, (sw - kw) / 2, knob_y(fill_h), kw, kh, ink, kh / 2);
  lv_obj_set_style_bg_opa(knob, LV_OPA_70, 0);
  w->fill = fill; w->knob = knob; w->sh = sh; w->sw = sw;
  drag(track, [e, track, fill, knob, knob_y, value, sh, sw](lv_point_t p, bool commit) {
    lv_area_t a;
    lv_obj_get_coords(track, &a);
    const int v = std::clamp(int(std::lround((a.y2 - p.y) * 100.0f / sh)), 1, 100);
    const int h = sh * v / 100;
    lv_obj_set_pos(fill, 0, sh - h);
    lv_obj_set_size(fill, sw, h);
    lv_obj_set_y(knob, knob_y(h));
    lv_label_set_text(value, pct(v).c_str());
    if (!commit) return;
    set_pending(e, "brightness", v);
    request(e, "light.turn_on", "brightness_pct", std::to_string(v), "panel_brightness");
  });
}

inline std::string hvac_label(const std::string &m) {
  if (m == "heat_cool") return "Auto";
  if (m == "fan_only") return "Fan";
  return label(m);
}
__attribute__((noinline)) inline void build_climate(const Layout &l, const std::string &e, const EntityDetailState &d, const std::string &title) {
  using onx_controls::effective;
  const auto mode = effective(d, "state");
  const auto unit = d.unit.empty() ? std::string("°") : d.unit;
  const bool range = !std::isfinite(num(d.target)) && std::isfinite(num(d.low)) && std::isfinite(num(d.high));
  float target = num(effective(d, "temperature"));
  const float want = pending_value(e, "temperature");
  clear_pending_if(e, "temperature", target);
  if (std::isfinite(want)) target = want;
  const float lo = std::isfinite(d.minimum) ? d.minimum : 7, hi = std::isfinite(d.maximum) && d.maximum > lo ? d.maximum : 35;
  const float step = std::isfinite(d.step) && d.step > 0 ? d.step : 0.5f;
  std::string context = hvac_label(mode);
  if (!d.preset.empty() && d.preset != "None" && d.preset != "none") context += " · " + label(d.preset);
  header(l.head, title, context);

  const int size = std::min({l.vis.w, l.vis.h, P(260)});
  const int cx = l.vis.x + l.vis.w / 2, ay = l.vis.y + (l.vis.h - size) / 2;
  lv_obj_t *arc = lv_arc_create(root);
  lv_obj_set_size(arc, size, size);
  lv_obj_set_pos(arc, cx - size / 2, ay);
  lv_arc_set_bg_angles(arc, 135, 45);
  lv_arc_set_range(arc, int(lo * 10), int(hi * 10));
  lv_arc_set_value(arc, std::isfinite(target) ? int(std::lround(target * 10)) : int(lo * 10));
  lv_obj_set_style_arc_width(arc, P(16), LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, P(16), LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, lv_color_hex(CONTROL()), LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, lv_color_hex(mode == "off" ? TRACK() : accent()), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(arc, lv_color_hex(TEXT()), LV_PART_KNOB);
  lv_obj_set_style_pad_all(arc, P(4), LV_PART_KNOB);
  lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(arc, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);
  lv_obj_remove_flag(arc, LV_OBJ_FLAG_SCROLLABLE);
  const std::string big = range ? fmt(num(effective(d, "target_temp_low"))) + "–" + fmt(num(effective(d, "target_temp_high"))) + "°"
                                : fmt(target) + "°";
  lv_obj_t *value = text(root, big, fonts.big, TEXT(), cx - size / 2, ay + size / 2 - line(fonts.big) / 2 - P(8), size, LV_TEXT_ALIGN_CENTER);
  text(root, "Now " + fmt(num(d.current)) + " " + unit, fonts.small, SOFT_TEXT(), cx - size / 2,
       ay + size / 2 + line(fonts.big) / 2, size, LV_TEXT_ALIGN_CENTER);
  if (range || !std::isfinite(target) || !(d.features & 1)) {
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
  } else {
    interactive(arc, false);
    on(arc, LV_EVENT_PRESSED, [](lv_event_t *) { dragging = true; });
    on(arc, LV_EVENT_VALUE_CHANGED, [arc, value, step](lv_event_t *) {
      const float v = std::round(lv_arc_get_value(arc) / 10.0f / step) * step;
      lv_label_set_text(value, (fmt(v) + "°").c_str());
    });
    on(arc, LV_EVENT_RELEASED, [e, arc, step](lv_event_t *) {
      dragging = false;
      if (!allowed()) return;
      const float v = std::round(lv_arc_get_value(arc) / 10.0f / step) * step;
      set_pending(e, "temperature", v);
      request(e, "climate.set_temperature", "temperature", fmt(v), "temperature");
    });
    on(arc, LV_EVENT_PRESS_LOST, [](lv_event_t *) { dragging = false; });
  }
  // −/+ sit in the arc's open bottom.
  const int bs = P(56), by = ay + size - bs / 2 - P(6);
  auto stepper = [e, range](int direction) {
    custom_logic_entity_sub::select_control_entity(e);
    custom_logic_entity_sub::control_setting = range ? "range" : "temperature";
    onx_controls::rotate(e, direction);
  };
  circle(root, cx - size / 2 + bs / 2, by, bs, CONTROL(), LV_SYMBOL_MINUS, TEXT(), [stepper]() { stepper(-1); });
  circle(root, cx + size / 2 - bs / 2, by, bs, CONTROL(), LV_SYMBOL_PLUS, TEXT(), [stepper]() { stepper(1); });

  // Fit every available climate control under the dial in portrait.
  auto modes = onx_control::options(d.modes);
  if (modes.size() > 4) modes.resize(4);
  struct Option { const char *key, *name, *service; std::string list, value; uint32_t bit; };
  const Option options[] = {{"preset_mode", "Preset", "climate.set_preset_mode", d.presets, effective(d, "preset_mode"), 16},
                            {"swing_mode", "Swing", "climate.set_swing_mode", d.swing_modes, effective(d, "swing_mode"), 32},
                            {"swing_horizontal_mode", "Horizontal swing", "climate.set_swing_horizontal_mode", d.hswing_modes,
                             effective(d, "swing_horizontal_mode"), 512},
                            {"fan_mode", "Fan", "climate.set_fan_mode", d.fan_modes, effective(d, "fan_mode"), 8}};
  const auto available = [&d](const Option &o) {
    return (d.features & o.bit) && !onx_control::options(o.list).empty();
  };
  const int rows = int(modes.size() > 1) + std::count_if(std::begin(options), std::end(options), available);
  const int gap = rows >= 3 ? P(6) : P(10);
  const int ch = rows > 0 ? std::min(P(44), (l.ctl.h - (rows - 1) * gap) / rows) : P(44);
  int y = l.ctl.y;
  if (modes.size() > 1) {
    std::vector<std::string> labels;
    int sel = -1;
    for (size_t i = 0; i < modes.size(); ++i) { labels.push_back(hvac_label(modes[i])); if (modes[i] == mode) sel = i; }
    chips(root, {l.ctl.x, y, l.ctl.w, ch}, labels, sel, [e, modes](int i) {
      const auto *d = custom_logic_entity_sub::entity_detail(e);
      if (!d) return;
      custom_logic_entity_sub::select_control_entity(e);
      onx_controls::request(*d, "climate.set_hvac_mode", "state", "hvac_mode", modes[i]);
    });
    y += ch + gap;
  }
  for (const auto &o : options) {
    if (!available(o)) continue;
    const std::string key = o.key, service = o.service, list = o.list, current = o.value;
    row(root, {l.ctl.x, y, l.ctl.w, ch}, o.name, label(current) + "  ›", [e, key, service, list, current]() {
      const auto *d = custom_logic_entity_sub::entity_detail(e);
      if (!d) return;
      custom_logic_entity_sub::select_control_entity(e);
      onx_controls::request(*d, service, key, key, custom_logic_entity_sub::next_option(list, current));
    });
    y += ch + gap;
  }
}

__attribute__((noinline)) inline void build_fan(const Layout &l, const std::string &e, const EntityDetailState &d, const std::string &title) {
  using onx_controls::effective;
  const bool on = effective(d, "state") == "on";
  float level = num(effective(d, "percentage"));
  const bool speeds = (d.features & 1) && std::isfinite(d.step) && d.step > 0;
  const int n = speeds ? std::clamp(int(std::lround(100 / d.step)), 1, 8) : 0;
  const int filled = on && std::isfinite(level) && n ? std::clamp(int(std::ceil(level * n / 100 - 0.01f)), 0, n) : 0;
  std::string context = on ? "On" : "Off";
  if (on && n) context += " · Speed " + std::to_string(filled) + " of " + std::to_string(n);
  const int pw = P(52);
  header(l.head, title, context, pw + P(8));
  circle(root, l.head.right() - pw / 2, l.head.y + pw / 2 - P(2), pw, on ? strong() : CONTROL(), LV_SYMBOL_POWER,
        on ? TEXT() : SOFT_TEXT(), [e]() { onx_controls::press(e); });
  text(root, on && std::isfinite(level) ? pct(level) : (on ? "On" : "Off"), fonts.big, TEXT(), l.vis.x, l.vis.y + P(8), l.vis.w,
       LV_TEXT_ALIGN_CENTER);
  if (n) {
    const int gap = P(10), bw = (l.vis.w - gap * (n - 1)) / n, base = l.vis.bottom() - P(8);
    const int top = l.vis.y + line(fonts.big) + P(28), span = base - top;
    for (int i = 0; i < n; ++i) {
      const int h = span * (i + 2) / (n + 1);
      button(root, l.vis.x + i * (bw + gap), base - h, bw, h, P(10), i < filled ? accent() : CONTROL(), "",
                           fonts.small, TEXT(), [e, i, n]() {
                             const auto *d = custom_logic_entity_sub::entity_detail(e);
                             if (!d) return;
                             custom_logic_entity_sub::select_control_entity(e);
                             onx_controls::request(*d, "fan.set_percentage", "percentage", "percentage",
                                                   std::to_string((i + 1) * 100 / n));
                           });
    }
  }
  const int rh = P(48), gap = P(10);
  int y = l.ctl.y;
  auto add = [&](const std::string &name, const std::string &value, std::function<void()> fn) {
    if (y + rh > l.ctl.bottom() + P(4)) return;
    row(root, {l.ctl.x, y, l.ctl.w, rh}, name, value, std::move(fn));
    y += rh + gap;
  };
  if (d.features & 2) {
    const bool osc = effective(d, "oscillating") == "true";
    add("Oscillate", osc ? "On" : "Off", [e, osc]() {
      const auto *d = custom_logic_entity_sub::entity_detail(e);
      if (!d) return;
      custom_logic_entity_sub::select_control_entity(e);
      onx_controls::request(*d, "fan.oscillate", "oscillating", "oscillating", osc ? "false" : "true");
    });
  }
  if (d.features & 4) {
    const auto dir = effective(d, "direction");
    add("Direction", label(dir.empty() ? "forward" : dir) + "  ›", [e, dir]() {
      const auto *d = custom_logic_entity_sub::entity_detail(e);
      if (!d) return;
      custom_logic_entity_sub::select_control_entity(e);
      onx_controls::request(*d, "fan.set_direction", "direction", "direction", dir == "reverse" ? "forward" : "reverse");
    });
  }
  if ((d.features & 8) && !onx_control::options(d.presets).empty()) {
    const auto current = effective(d, "preset_mode");
    add("Preset", label(current.empty() || current == "None" ? "none" : current) + "  ›", [e, current]() {
      const auto *d = custom_logic_entity_sub::entity_detail(e);
      if (!d) return;
      custom_logic_entity_sub::select_control_entity(e);
      onx_controls::request(*d, "fan.set_preset_mode", "preset_mode", "preset_mode",
                            custom_logic_entity_sub::next_option(d->presets, current));
    });
  }
}

__attribute__((noinline)) inline void build_cover(const Layout &base, const std::string &e, const EntityDetailState &d, const std::string &title) {
  Layout l = base;
  const auto presets = onx_entity_config::cover_presets(e);
  const bool show_presets = (d.features & 4) && presets.enabled && presets.count;
  if (show_presets && !wide()) {
    const int extra = P(12) + line(fonts.small) + P(6) + P(40);
    l.vis.h -= extra; l.ctl.y -= extra; l.ctl.h += extra;
  }
  float position = num(d.position);
  clear_pending_if(e, "position", position);
  const float want = pending_value(e, "position");
  if (std::isfinite(want)) position = want;
  const bool known = std::isfinite(position);
  const bool moving = d.state == "opening" || d.state == "closing";
  header(l.head, title, label(d.state) + (known ? " · " + pct(position) : ""));
  // A window: the blind covers the closed fraction from the top.
  const int fw = std::min(P(200), l.vis.w), fh = l.vis.h - line(fonts.big) - P(12);
  const int fx = l.vis.x + (l.vis.w - fw) / 2, fy = l.vis.y, border = P(6);
  lv_obj_t *frame = box(root, fx, fy, fw, fh, 0x1B2733, P(10));
  lv_obj_set_style_border_width(frame, border, 0);
  lv_obj_set_style_border_color(frame, lv_color_hex(CONTROL()), 0);
  lv_obj_set_style_clip_corner(frame, true, 0);
  const int inner = fh - 2 * border, iw = fw - 2 * border;
  const int closed = known ? int(inner * (100 - std::clamp(position, 0.0f, 100.0f)) / 100) : inner / 2;
  lv_obj_t *blind = box(frame, 0, 0, iw, closed, 0x4A525B, 0);
  for (int y = P(10); y < closed - P(6); y += P(14)) box(blind, 0, y, iw, P(2), 0x3A4148, 0);
  lv_obj_t *edge = box(frame, 0, std::max(0, closed - P(4)), iw, P(4), accent(), 0);
  lv_obj_t *value = text(root, known ? pct(position) : label(d.state), fonts.big, TEXT(), l.vis.x, fy + fh + P(8), l.vis.w,
                         LV_TEXT_ALIGN_CENTER);
  if (known && (d.features & 4)) {
    drag(frame, [e, frame, blind, edge, value, inner, iw, border](lv_point_t p, bool commit) {
      lv_area_t a;
      lv_obj_get_coords(frame, &a);
      const int c = std::clamp(int(p.y - a.y1 - border), 0, inner);
      const int v = std::clamp(100 - int(std::lround(c * 100.0f / inner)), 0, 100);
      lv_obj_set_size(blind, iw, c);
      lv_obj_set_pos(edge, 0, std::max(0, c - P(4)));
      lv_label_set_text(value, pct(v).c_str());
      if (!commit) return;
      set_pending(e, "position", v, 8000);
      request(e, "cover.set_cover_position", "position", std::to_string(v), "panel_cover");
    });
  }
  struct Btn { const char *caption; const char *service; uint32_t bit; };
  const Btn all[] = {{"Open", "cover.open_cover", 1}, {"Stop", "cover.stop_cover", 8}, {"Close", "cover.close_cover", 2}};
  std::vector<Btn> shown;
  for (const auto &b : all) if (d.features == 0 || (d.features & b.bit)) shown.push_back(b);
  if (shown.empty()) return;
  const int gap = P(10), n = shown.size(), bw = (l.ctl.w - gap * (n - 1)) / n, bh = std::min(P(56), l.ctl.h);
  for (int i = 0; i < n; ++i) {
    const auto b = shown[i];
    const bool hot = std::string(b.caption) == "Stop" && moving;
    button(root, l.ctl.x + i * (bw + gap), l.ctl.y, bw, bh, P(14), hot ? strong() : CONTROL(), b.caption, fonts.body, TEXT(),
           [e, b]() { request(e, b.service, "", "", "panel_cover"); });
  }
  if (show_presets) {
      const int cols = std::min<int>(wide() && W < 400 ? 2 : 4,presets.count), pg = P(8), ph = P(40);
      int y = l.ctl.y + bh + P(12);
      text(root,"Positions",fonts.small,MUTED(),l.ctl.x,y,l.ctl.w);
      y += line(fonts.small) + P(6);
      const int pw = (l.ctl.w - (cols-1)*pg)/cols;
      for (int i=0;i<presets.count;++i) {
        const int target = presets.positions[i], py = y+(i/cols)*(ph+pg);
        if (py+ph > l.ctl.bottom()) break;
        chip(root,l.ctl.x+(i%cols)*(pw+pg),py,pw,ph,std::to_string(target)+"%",
             known && std::abs(position-target)<0.51f,[e,target]() {
               set_pending(e,"position",target,8000);
               request(e,"cover.set_cover_position","position",std::to_string(target),"panel_cover");
             });
      }
  }
}

// Siren: power plus the turn_on options Home Assistant advertises through
// supported_features (TONES 4, VOLUME_SET 8, DURATION 16). The choices stay on
// the panel and are sent with the next turn_on.
struct SirenChoice { std::string entity, tone; int volume{-1}, duration{0}; };
inline std::vector<SirenChoice> siren_choices;
// Each siren's choices persist in flash under a key derived from its entity id.
struct SirenStored { char tone[40]; int16_t volume; int16_t duration; };
inline esphome::ESPPreferenceObject siren_pref(const std::string &e) {
  return esphome::global_preferences->make_preference<SirenStored>(esphome::fnv1_hash("onx.siren.v1." + e));
}
inline SirenChoice &siren_choice(const std::string &e) {
  for (auto &c : siren_choices) if (c.entity == e) return c;
  if (siren_choices.size() >= 8) siren_choices.erase(siren_choices.begin());
  SirenChoice c{e};
  SirenStored st{};
  auto pref = siren_pref(e);
  if (pref.load(&st)) {
    st.tone[sizeof(st.tone) - 1] = 0;
    c.tone = st.tone; c.volume = std::clamp<int>(st.volume, -1, 100); c.duration = std::clamp<int>(st.duration, 0, 300);
  }
  siren_choices.push_back(std::move(c));
  return siren_choices.back();
}
inline void siren_save(const SirenChoice &c) {
  SirenStored st{};
  snprintf(st.tone, sizeof(st.tone), "%s", c.tone.c_str());
  st.volume = c.volume; st.duration = c.duration;
  auto pref = siren_pref(c.entity);
  pref.save(&st);
}
inline void siren_turn_on(const std::string &e, uint32_t features) {
  const auto &c = siren_choice(e);
  std::vector<onx_control::Value> values{{"panel_power", "", "on"}};
  // Options are not reported back as state; mark them confirmed up front.
  if ((features & 4) && !c.tone.empty()) values.push_back({"siren_tone", "tone", c.tone, true});
  if ((features & 8) && c.volume >= 0) values.push_back({"siren_volume", "volume_level", fmt(c.volume / 100.0f, 2), true});
  if ((features & 16) && c.duration > 0) values.push_back({"siren_duration", "duration", std::to_string(c.duration), true});
  custom_logic_entity_sub::select_control_entity(e);
  custom_logic_entity_sub::request_control(e, "siren.turn_on", std::move(values));
}
__attribute__((noinline)) inline void build_siren(const Layout &l, const std::string &e, const EntityDetailState &d, const std::string &title) {
  const bool on = d.state == "on";
  const uint32_t f = d.features;
  header(l.head, title, on ? "Sounding" : "Off");
  const int rows = ((f & 4) && !onx_control::options(d.tones).empty()) + bool(f & 8) + bool(f & 16);
  const int size = std::min({P(150), l.vis.w, l.vis.h - line(fonts.big) - P(16)});
  const int cx = l.vis.x + l.vis.w / 2, cy = l.vis.y + (l.vis.h - line(fonts.big) - P(12)) / 2;
  circle(root, cx, cy, size, on ? strong() : CONTROL(), LV_SYMBOL_POWER, on ? TEXT() : SOFT_TEXT(), [e, on, f]() {
    if (on) request(e, "siren.turn_off", "", "off", "panel_power");
    else siren_turn_on(e, f);
  });
  text(root, on ? "Sounding" : "Off", fonts.big, TEXT(), l.vis.x, cy + size / 2 + P(10), l.vis.w, LV_TEXT_ALIGN_CENTER);
  if (!rows) return;
  auto &c = siren_choice(e);
  const int gap = P(8), bh = std::min(P(48), (l.ctl.h - gap * (rows - 1)) / rows), sw = P(56);
  int y = wide() ? l.ctl.y : std::max(l.ctl.y, cy + size / 2 + P(16) + line(fonts.big));
  auto stepper = [&](const std::string &caption, std::function<void(int)> step) {
    button(root, l.ctl.x, y, sw, bh, P(14), CONTROL(), "-", fonts.body, TEXT(), [step]() { step(-1); });
    lv_obj_t *mid = row(root, {l.ctl.x + sw + gap, y, l.ctl.w - 2 * (sw + gap), bh}, caption, "");
    lv_obj_set_style_bg_opa(mid, LV_OPA_TRANSP, 0);
    button(root, l.ctl.x + l.ctl.w - sw, y, sw, bh, P(14), CONTROL(), "+", fonts.body, TEXT(), [step]() { step(1); });
    y += bh + gap;
  };
  const auto tones = onx_control::options(d.tones);
  if ((f & 4) && !tones.empty()) {
    button(root, l.ctl.x, y, l.ctl.w, bh, P(14), CONTROL(), "Tone: " + (c.tone.empty() ? std::string("Default") : label(c.tone)),
           fonts.body, TEXT(), [e, tones]() {
             auto &s = siren_choice(e);
             auto it = std::find(tones.begin(), tones.end(), s.tone);
             s.tone = it == tones.end() ? tones.front() : (it + 1 == tones.end() ? std::string() : *(it + 1));
             siren_save(s);
             built_signature.clear();
           });
    y += bh + gap;
  }
  if (f & 8)
    stepper("Volume " + (c.volume < 0 ? std::string("default") : std::to_string(c.volume) + "%"), [e](int dir) {
      auto &s = siren_choice(e);
      s.volume = s.volume < 0 ? (dir > 0 ? 50 : -1) : s.volume + dir * 10;
      if (s.volume > 100) s.volume = 100;
      if (s.volume < 0) s.volume = -1;
      siren_save(s);
      built_signature.clear();
    });
  if (f & 16)
    stepper("Duration " + (c.duration <= 0 ? std::string("default") : std::to_string(c.duration) + " s"), [e](int dir) {
      auto &s = siren_choice(e);
      static const int steps[] = {0, 5, 10, 30, 60, 120, 300};
      int i = 0;
      while (i < 6 && steps[i] < s.duration) ++i;
      s.duration = steps[std::clamp(i + dir, 0, 6)];
      siren_save(s);
      built_signature.clear();
    });
}

__attribute__((noinline)) inline void build_vacuum(const Layout &l, const std::string &e, const EntityDetailState &d, const std::string &title) {
  using onx_controls::effective;
  const auto state = effective(d, "state");
  const bool active = state == "cleaning" || state == "returning" || state == "on";
  const bool cleaning = state == "cleaning";
  const float bat = num(d.battery_level);
  const bool has_bat = std::isfinite(bat);
  const auto speeds = (d.features & 32) ? onx_control::options(d.fan_modes) : std::vector<std::string>{};
  const auto speed = effective(d, "fan_speed");
  std::string context = !d.status.empty() && d.status != "None" ? d.status : label(state.empty() ? "idle" : state);
  if (has_bat) context += " · " + pct(bat);
  if (!speed.empty() && speed != "None") context += " · " + label(speed);
  header(l.head, title, context);
  const int size = std::min({P(176), l.vis.w, l.vis.h - P(8)});
  const int cx = l.vis.x + l.vis.w / 2, cy = l.vis.y + l.vis.h / 2;
  if (has_bat) {
    lv_obj_t *arc = lv_arc_create(root);
    lv_obj_set_size(arc, size, size);
    lv_obj_set_pos(arc, cx - size / 2, cy - size / 2);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_range(arc, 0, 1000);
    lv_arc_set_value(arc, int(std::clamp(bat, 0.0f, 100.0f) * 10));
    const int aw = P(12);
    lv_obj_set_style_arc_width(arc, aw, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, aw, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(CONTROL()), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(active ? accent() : SOFT_TEXT()), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(arc, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);
    lv_obj_remove_flag(arc, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  } else {
    lv_obj_t *badge = box(root, cx - size / 2, cy - size / 2, size, size, active ? soft() : CARD(), LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(badge, P(3), 0);
    lv_obj_set_style_border_color(badge, lv_color_hex(active ? accent() : CONTROL()), 0);
  }
  const int iw = size - P(36);
  const std::string st_label = label(state.empty() ? "idle" : state);
  const lv_font_t *sf = fit(st_label, iw, {fonts.title, fonts.body, fonts.small});
  const int bh_txt = line(sf) + (has_bat ? P(2) + line(fonts.small) : 0);
  text(root, st_label, sf, active ? accent() : TEXT(), cx - iw / 2, cy - bh_txt / 2, iw, LV_TEXT_ALIGN_CENTER);
  if (has_bat) text(root, pct(bat), fonts.small, MUTED(), cx - iw / 2, cy - bh_txt / 2 + line(sf) + P(2), iw, LV_TEXT_ALIGN_CENTER);

  struct Act { const char *symbol; const char *service; const char *target; bool hot; };
  std::vector<Act> primary, secondary;
  if (cleaning && (d.features & 4)) primary.push_back({LV_SYMBOL_PAUSE, "vacuum.pause", "paused", true});
  else if (d.features == 0 || (d.features & 8192)) primary.push_back({LV_SYMBOL_PLAY, "vacuum.start", "cleaning", !active});
  if (d.features & 8) primary.push_back({LV_SYMBOL_STOP, "vacuum.stop", "idle", cleaning && !(d.features & 4)});
  if (d.features == 0 || (d.features & 16)) primary.push_back({LV_SYMBOL_HOME, "vacuum.return_to_base", "returning", state == "returning"});
  if (d.features & 512) secondary.push_back({LV_SYMBOL_GPS, "vacuum.locate", "", false});
  if (d.features & 1024) secondary.push_back({LV_SYMBOL_LOOP, "vacuum.clean_spot", "cleaning", false});

  const int rows = (!primary.empty() ? 1 : 0) + (!secondary.empty() ? 1 : 0) + (!speeds.empty() ? 1 : 0);
  if (!rows) return;
  const int gap = P(8), rh = std::min(P(48), (l.ctl.h - gap * (rows - 1)) / rows);
  int y = l.ctl.y;
  auto draw_row = [&](const std::vector<Act> &list) {
    if (list.empty() || y + rh > l.ctl.bottom() + P(4)) return;
    const int n = list.size(), bw = (l.ctl.w - gap * (n - 1)) / n;
    for (int i = 0; i < n; ++i) {
      const auto a = list[i];
      const int w = i == n - 1 ? l.ctl.w - i * (bw + gap) : bw;
      icon_button(root, l.ctl.x + i * (bw + gap), y, w, rh, P(14), a.hot ? strong() : CONTROL(), a.symbol, TEXT(),
                  [e, a]() { request(e, a.service, "", a.target, a.target[0] ? "state" : "panel_vacuum"); });
    }
    y += rh + gap;
  };
  draw_row(primary);
  draw_row(secondary);
  if (!speeds.empty() && y + rh <= l.ctl.bottom() + P(4)) {
    const auto cur = speed.empty() || speed == "None" ? speeds.front() : speed;
    row(root, {l.ctl.x, y, l.ctl.w, rh}, "Fan speed", label(cur) + "  ›", [e, cur]() {
      const auto *dd = custom_logic_entity_sub::entity_detail(e);
      if (!dd) return;
      custom_logic_entity_sub::select_control_entity(e);
      onx_controls::request(*dd, "vacuum.set_fan_speed", "fan_speed", "fan_speed",
                            custom_logic_entity_sub::next_option(dd->fan_modes, cur));
    });
  }
}

inline std::string pin_entity, pin_code;
inline void draw_keypad(const Rect &area, const std::string &e) {
  if (pin_entity != e) { pin_entity = e; pin_code.clear(); }
  const int gap = P(6), disp_h = P(32);
  std::string dots;
  for (size_t i = 0; i < pin_code.size(); ++i) dots += "•";
  box(root, area.x, area.y, area.w, disp_h, CARD(), P(10));
  text(root, dots.empty() ? "Enter code" : dots, dots.empty() ? fonts.small : fonts.title,
       dots.empty() ? MUTED() : TEXT(), area.x + P(8),
       area.y + (disp_h - line(dots.empty() ? fonts.small : fonts.title)) / 2, area.w - P(16), LV_TEXT_ALIGN_CENTER);
  const int top = area.y + disp_h + gap, kh = area.h - disp_h - gap;
  const int rows = 4, cols = 3;
  const int bw = (area.w - gap * (cols - 1)) / cols, bh = (kh - gap * (rows - 1)) / rows;
  if (bh < P(18)) return;
  static constexpr const char *keys[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "C", "0", "<"};
  for (int i = 0; i < 12; ++i) {
    const int r = i / cols, c = i % cols;
    const int x = area.x + c * (bw + gap), y = top + r * (bh + gap);
    const int w = c == cols - 1 ? area.w - c * (bw + gap) : bw;
    const char *k = keys[i];
    const bool special = k[0] == 'C' || k[0] == '<';
    button(root, x, y, w, bh, P(10), special ? CARD() : CONTROL(), k, fonts.body, special ? SOFT_TEXT() : TEXT(), [e, k]() {
      if (pin_entity != e) { pin_entity = e; pin_code.clear(); }
      if (k[0] == 'C') pin_code.clear();
      else if (k[0] == '<') { if (!pin_code.empty()) pin_code.pop_back(); }
      else if (pin_code.size() < 8) pin_code.push_back(k[0]);
      built_signature.clear();
    });
  }
}

__attribute__((noinline)) inline void build_lock(const Layout &l, const std::string &e, const EntityDetailState &d, const std::string &title) {
  const auto state = onx_controls::effective(d, "state");
  const bool locked = state == "locked";
  const bool needs_code = !d.code_format.empty() && d.code_format != "None";
  std::string context = label(state.empty() ? "locked" : state);
  if (!d.changed_by.empty() && d.changed_by != "None") context += " · " + d.changed_by;
  header(l.head, title, context);
  if (needs_code) {
    draw_keypad(l.vis, e);
  } else {
    const int size = std::min({P(176), l.vis.w, l.vis.h - P(8)});
    const int cx = l.vis.x + l.vis.w / 2, cy = l.vis.y + l.vis.h / 2;
    lv_obj_t *badge = box(root, cx - size / 2, cy - size / 2, size, size, locked ? CARD() : soft(), LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(badge, P(3), 0);
    lv_obj_set_style_border_color(badge, lv_color_hex(locked ? CONTROL() : accent()), 0);
    const std::string st = label(state.empty() ? "locked" : state);
    const int iw = size - P(28);
    const lv_font_t *sf = fit(st, iw, {fonts.title, fonts.body, fonts.small});
    text(root, st, sf, locked ? TEXT() : accent(), cx - iw / 2, cy - line(sf) / 2, iw, LV_TEXT_ALIGN_CENTER);
  }
  struct Act { const char *caption; const char *service; const char *target; bool hot; bool send_code; };
  std::vector<Act> btns;
  btns.push_back({"Lock", "lock.lock", "locked", locked, false});
  btns.push_back({"Unlock", "lock.unlock", "unlocked", !locked, needs_code});
  if (d.features & 1) btns.push_back({"Open", "lock.open", "open", state == "open", needs_code});
  const int gap = P(8), n = btns.size(), bw = (l.ctl.w - gap * (n - 1)) / n, bh = std::min(P(52), l.ctl.h);
  for (int i = 0; i < n; ++i) {
    const auto a = btns[i];
    const int w = i == n - 1 ? l.ctl.w - i * (bw + gap) : bw;
    button(root, l.ctl.x + i * (bw + gap), l.ctl.y, w, bh, P(14), a.hot ? strong() : CONTROL(), a.caption,
           n >= 3 ? fonts.small : fonts.body, TEXT(), [e, a]() {
             std::vector<onx_control::Value> vals{{"state", "", a.target}};
             if (a.send_code && pin_entity == e && !pin_code.empty()) vals.push_back({"code", "code", pin_code, true});
             custom_logic_entity_sub::select_control_entity(e);
             custom_logic_entity_sub::request_control(e, a.service, std::move(vals));
             pin_code.clear();
             built_signature.clear();
           });
  }
}

__attribute__((noinline)) inline void build_alarm(const Layout &l, const std::string &e, const EntityDetailState &d, const std::string &title) {
  const auto state = d.state.empty() ? "disarmed" : d.state;
  const bool disarmed = state == "disarmed";
  const bool needs_code = d.code_format == "number" || (!d.code_format.empty() && d.code_format != "None");
  const bool arm_code = needs_code && (d.code_arm_required.empty() || d.code_arm_required == "True" || d.code_arm_required == "true");
  const std::string st = state == "armed_custom_bypass" ? "Armed bypass" : label(state);
  std::string context = st;
  if (!d.changed_by.empty() && d.changed_by != "None") context += " · " + d.changed_by;
  header(l.head, title, context);
  if (needs_code) {
    draw_keypad(l.vis, e);
  } else {
    const int size = std::min({P(176), l.vis.w, l.vis.h - P(8)});
    const int cx = l.vis.x + l.vis.w / 2, cy = l.vis.y + l.vis.h / 2;
    lv_obj_t *badge = box(root, cx - size / 2, cy - size / 2, size, size, disarmed ? CARD() : soft(), LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(badge, P(3), 0);
    lv_obj_set_style_border_color(badge, lv_color_hex(disarmed ? CONTROL() : accent()), 0);
    const int iw = size - P(28);
    const lv_font_t *sf = fit(st, iw, {fonts.title, fonts.body, fonts.small});
    text(root, st, sf, disarmed ? TEXT() : accent(), cx - iw / 2, cy - line(sf) / 2, iw, LV_TEXT_ALIGN_CENTER);
  }
  struct Act { const char *caption; const char *service; const char *target; bool hot; bool send_code; };
  std::vector<Act> btns;
  if (!disarmed) {
    btns.push_back({"Disarm", "alarm_control_panel.alarm_disarm", "disarmed", true, needs_code});
  } else {
    if (d.features & 1) btns.push_back({"Home", "alarm_control_panel.alarm_arm_home", "armed_home", false, arm_code});
    if (d.features & 2) btns.push_back({"Away", "alarm_control_panel.alarm_arm_away", "armed_away", true, arm_code});
    if (d.features & 4) btns.push_back({"Night", "alarm_control_panel.alarm_arm_night", "armed_night", false, arm_code});
    if (btns.empty()) {
      btns.push_back({"Home", "alarm_control_panel.alarm_arm_home", "armed_home", false, arm_code});
      btns.push_back({"Away", "alarm_control_panel.alarm_arm_away", "armed_away", true, arm_code});
    }
  }
  const int gap = P(8), n = btns.size(), bw = (l.ctl.w - gap * (n - 1)) / n, bh = std::min(P(52), l.ctl.h);
  for (int i = 0; i < n; ++i) {
    const auto a = btns[i];
    const int w = i == n - 1 ? l.ctl.w - i * (bw + gap) : bw;
    button(root, l.ctl.x + i * (bw + gap), l.ctl.y, w, bh, P(14), a.hot ? strong() : CONTROL(), a.caption,
           n >= 3 ? fonts.small : fonts.body, TEXT(), [e, a]() {
             std::vector<onx_control::Value> vals{{"state", "", a.target}};
             if (a.send_code && pin_entity == e && !pin_code.empty()) vals.push_back({"code", "code", pin_code, true});
             custom_logic_entity_sub::select_control_entity(e);
             custom_logic_entity_sub::request_control(e, a.service, std::move(vals));
             pin_code.clear();
             built_signature.clear();
           });
  }
}

__attribute__((noinline)) inline void build_toggle(const Layout &l, const std::string &e, const EntityDetailState &d, const std::string &title) {
  const bool on = shown_state(e, d.state) == "on";
  const auto domain = e.substr(0, e.find('.'));
  header(l.head, title, d.available ? (on ? "On" : "Off") : "Unavailable");
  const int size = std::min({P(150), l.vis.w, l.vis.h - line(fonts.big) - P(16)});
  const int cx = l.vis.x + l.vis.w / 2, cy = l.vis.y + (l.vis.h - line(fonts.big) - P(12)) / 2;
  circle(root, cx, cy, size, on ? strong() : CONTROL(), LV_SYMBOL_POWER, on ? TEXT() : SOFT_TEXT(),
        [e, domain, on]() { request(e, domain + ".toggle", "", on ? "off" : "on", "state"); });
  text(root, on ? "On" : "Off", fonts.big, TEXT(), l.vis.x, cy + size / 2 + P(10), l.vis.w, LV_TEXT_ALIGN_CENTER);
  // Smart plugs: power reading and graph from the configured secondary sensor.
  const int i = onx_sensor_view::index(e);
  if (i < 0 || !onx_sensor_view::configs[i].secondary[0]) return;
  const auto &s = onx_sensor_view::states[i];
  // The reading right under On/Off, and the graph in all the space below it,
  // ending clear of the bottom edge.
  const int th = P(56), ty = wide() ? l.ctl.y : std::min(l.ctl.y, cy + size / 2 + P(10) + line(fonts.big) + P(10));
  tile(root, {l.ctl.x, ty, l.ctl.w, th}, s.unit == "kWh" ? "Energy" : "Power",
       s.received && s.available ? onx_detail_view::compact(s.value) + " " + s.unit : "—");
  const int gy = ty + th + P(10), gb = H - P(24);
  if (onx_sensor_view::configs[i].graph && draw_graph && gb - gy >= P(60)) {
    // draw_graph sizes the canvas to the frame less P(24) wide and P(40) tall.
    lv_obj_t *frame = box(root, l.ctl.x - P(12), gy - P(4), l.ctl.w + P(24), gb - gy + P(40), BG(), 0, LV_OPA_TRANSP);
    draw_graph(frame, e, P(4));
  }
}

__attribute__((noinline)) inline void build_action(const Layout &layout_in, const std::string &e, const EntityDetailState &d, const std::string &title) {
  const bool script = has_prefix(e, "script."), scene = has_prefix(e, "scene."), automation = has_prefix(e, "automation.");
  const bool running = script && d.state == "on";
  const std::string kind = script ? "Script" : automation ? "Automation" : scene ? "Scene" : "Button";
  header(layout_in.head, title, kind + (running ? " · Running…" : ""));
  // The 2.8" panel uses a smaller button without the "Tap to run" label and
  // gives the space to the controls and run history below.
  const bool compact = std::min(W, H) < 320 && !wide();
  Layout l = layout_in;
  if (compact) {
    l.vis.h = std::min(P(120), l.vis.w) + P(8);
    l.ctl.y = l.vis.bottom() + P(12);
    l.ctl.h = H - P(16) - l.ctl.y;
  }
  const int size = compact ? std::min(P(120), l.vis.w) : std::min({P(170), l.vis.w, l.vis.h - P(40)});
  const int cx = l.vis.x + l.vis.w / 2, cy = compact ? l.vis.y + l.vis.h / 2 : l.vis.y + (l.vis.h - P(30)) / 2;
  const bool ring = esphome::millis() - ran_at < 1500 || running;
  lv_obj_t *b = circle(root, cx, cy, size, strong(), LV_SYMBOL_PLAY, TEXT(), [e, script, automation]() {
    ran_at = esphome::millis();
    if (script) request(e, "script.turn_on", "", "on", "panel_run");
    else if (automation && run_quick_action) run_quick_action(e);
    else onx_controls::press(e);
  });
  if (ring) {
    lv_obj_set_style_border_width(b, P(6), 0);
    lv_obj_set_style_border_color(b, lv_color_hex(accent()), 0);
  }
  if (!compact || running) {
    if (compact) l.ctl.y += line(fonts.body);
    text(root, running ? "Running…" : "Tap to run", fonts.body, SOFT_TEXT(), l.vis.x, cy + size / 2 + (compact ? P(4) : P(12)),
         l.vis.w, LV_TEXT_ALIGN_CENTER);
  }
  if (automation) {
    const bool enabled = d.state == "on";
    button(root, l.ctl.x, l.ctl.y, l.ctl.w, P(48), P(16), CONTROL(),
           enabled ? "Enabled · Tap to disable" : "Disabled · Tap to enable", fonts.body, TEXT(),
           [e, enabled]() { request(e, enabled ? "automation.turn_off" : "automation.turn_on", "", enabled ? "off" : "on", "panel_automation"); });
  } else if (running) {
    button(root, l.ctl.x, l.ctl.y, l.ctl.w, P(48), P(16), CONTROL(), "Stop script", fonts.body, TEXT(),
           [e]() { request(e, "script.turn_off", "", "off", "panel_stop"); });
  }
  // Run history (the same data as the page card): last run, a 24 h strip and
  // the most recent runs. Fills the right column in landscape and the space
  // below the button in portrait.
  if (automation) return;  // Automation triggers are not recorded on the panel.
  const time_t now = std::time(nullptr);
  const auto *log = changes_for(e);
  time_t last = log != nullptr && !log->items.empty() ? log->items.front().at : 0;
  if (!last && !script) custom_logic_entity_sub::parse_ha_datetime_epoch(d.state, &last);
  int y = l.ctl.y + ((automation || running) ? P(48) + P(14) : 0);
  const int x = l.ctl.x, w = l.ctl.w, bottom = l.ctl.bottom();
  if (y + line(fonts.body) > bottom) return;
  text(root, last ? "Last run " + onx_sensor_view::relative(last, now) : std::string("No runs recorded yet"), fonts.body, TEXT(), x, y, w);
  y += line(fonts.body) + P(12);
  const int sh = P(16);
  if (now > 1577836800 && y + line(fonts.small) * 2 + sh + P(8) <= bottom) {
    text(root, "Runs, last 24 h", fonts.small, MUTED(), x, y, w);
    y += line(fonts.small) + P(2);
    lv_obj_t *strip = box(root, x, y, w, sh, CARD(), P(4));
    if (log != nullptr)
      for (const auto &run : log->items) {
        if (run.at > now || now - run.at >= 86400) continue;
        box(strip, int(int64_t(86400 - (now - run.at)) * (w - P(4)) / 86400), 0, P(4), sh, accent(), P(2));
      }
    y += sh + P(4);
    text(root, "-24 h", fonts.small, MUTED(), x, y, w / 2);
    text(root, "now", fonts.small, MUTED(), x + w / 2, y, w / 2, LV_TEXT_ALIGN_RIGHT);
    y += line(fonts.small) + P(14);
  }
  // Recent runs as rows while they fit.
  if (log == nullptr || log->items.empty()) return;
  const int rh = line(fonts.body) + P(6);
  if (y + line(fonts.small) + rh > bottom) return;
  text(root, "Recent runs", fonts.small, MUTED(), x, y, w);
  y += line(fonts.small) + P(4);
  for (const auto &run : log->items) {
    if (y + rh > bottom) break;
    text(root, clock(run.at), fonts.body, TEXT(), x, y, w / 2);
    text(root, onx_sensor_view::relative(run.at, now), fonts.body, SOFT_TEXT(), x + w / 2, y, w / 2, LV_TEXT_ALIGN_RIGHT);
    y += rh;
  }
}

// ------------------------------------------------------------ timers
inline bool hero_text(const std::string &s);
// One screen for every timer source: HA timer helpers (cancel / pause / +1 min
// through their services), voice-timer sensors and Google timers (shown, but
// controlled on their own device), and this panel's own Assist timers. Other
// running timers are listed below the focused one; tap a row to focus it.
struct LocalTimer { std::string id, name; uint32_t total{0}, left{0}; bool active{false}; };
struct TimerState {
  std::vector<LocalTimer> local;  // Copied from voice_assistant each second.
  uint32_t synced_ms{0};
  bool ringing{false};            // A local timer finished and is sounding.
  std::string ringing_name;
  uint32_t ringing_total{0}, ringing_since{0};
  bool overlay{false};            // Local timer screen shown above everything.
  bool dismissed{false};
  std::string message_key, message;
  uint32_t message_until{0};
  bool command_pending{false};
  std::string focus;              // Key of the focused timer ("" = default).
};
inline TimerState timers;
inline void retire_local_timer(const std::string &id) {
  timers.local.erase(std::remove_if(timers.local.begin(), timers.local.end(),
      [&](const LocalTimer &t) { return t.id == id; }), timers.local.end());
  if (timers.message_key == "local:" + id) { timers.message.clear(); timers.command_pending = false; }
}
inline bool timer_screen_active{false};
struct TimerVisual {
  std::string key;
  lv_obj_t *value{nullptr}, *arc{nullptr}, *bar{nullptr};
  int bar_width{0}, last_progress{-1};
  std::string last_value;
  lv_obj_t *context{nullptr};
  std::string base_context, last_context;
};
inline std::vector<TimerVisual> timer_visuals;
inline std::function<void(const std::string &)> stop_local_timer;
inline std::function<void(const std::string &)> extend_local_timer, extend_helper_timer;
inline std::function<void()> stop_ringing;  // Supplied by assist.yaml.
inline std::function<bool()> timer_interaction_pending;
inline std::function<void(const std::string &, bool)> set_local_timer_active;
inline std::function<void()> close_timers;  // Closes the local timer screen.
struct TimerItem {
  std::string key, entity, name, kind, state;  // state: active, paused, ringing, idle
  double left{0}, total{0};
  bool ready{false}, helper{false};
  bool voice{false};          // One timer of a voice satellite's "Voice timer" sensor.
  std::string voice_name;     // Its Assist name ("" for unnamed timers).
};
inline TimerItem entity_timer(const std::string &e, const EntityDetailState &d, const std::string &title) {
  TimerItem t;
  t.key = e; t.entity = e; t.state = d.state;
  t.name = !d.timer_name.empty() ? d.timer_name : title;
  t.helper = has_prefix(e, "timer.");
  t.kind = t.helper ? "Timer" : d.google_timer ? "Google timer" : "Voice timer";
  onx_timer::View view;
  if (d.voice_timer) {
    const uint32_t age = esphome::millis() - d.timer_received_ms;
    // Extrapolate between HA updates; marking stale data "not ready" toggled
    // the screen signature and rebuilt (flickered) the timer screen.
    view = onx_timer::view(d.state, d.voice_duration, d.voice_remaining, d.voice_remaining + 1, 1.0 + age / 1000.0);
    t.total = d.voice_duration;
  } else {
    time_t finish = 0;
    const bool known = custom_logic_entity_sub::parse_ha_datetime_epoch(d.timer_finishes_at, &finish) ||
                       (d.state == "idle" && custom_logic_entity_sub::parse_ha_datetime_epoch(d.timer_last_finish, &finish));
    view = onx_timer::view(d.state, onx_timer::duration_seconds(d.timer_duration), onx_timer::duration_seconds(d.timer_remaining),
                           onx_timer::deadline_epoch(known, finish), std::time(nullptr));
    t.total = onx_timer::duration_seconds(d.timer_duration);
  }
  t.ready = view.ready && d.available && d.received;
  t.left = view.seconds_left;
  if (!std::isfinite(t.total)) t.total = 0;
  if (t.ready && d.state == "active" && t.left <= 0) t.state = "ringing";
  return t;
}
// Voice satellite timers are owned by Home Assistant: they are controlled with
// Assist sentences (features/assist.yaml), and a satellite's ringing alarm with
// its "Stop timer alarm" button.
inline std::function<void(const std::string &key, const std::string &sentence, const std::string &pending)> send_timer_sentence;
inline std::function<void(const std::string &button)> press_button;
inline std::function<void()> voice_sentence_for(const TimerItem &t, onx_timer::Action action) {
  const std::string key = t.key, sentence = onx_timer::control_sentence(t.voice_name, uint32_t(t.total), action);
  const std::string pending = action == onx_timer::Action::ADD_MINUTE ? "Adding 1 min..." : action == onx_timer::Action::STOP ? "Stopping..." :
                              action == onx_timer::Action::RESUME ? "Resuming..." : "Pausing...";
  return [key, sentence, pending]() { if (send_timer_sentence) send_timer_sentence(key, sentence, pending); };
}
inline std::function<void()> voice_alarm_stop(const TimerItem &t) {
  const auto button = onx_timer::stop_alarm_button(t.entity);
  if (button.empty()) return nullptr;
  return [button]() { if (press_button) press_button(button); };
}
inline std::vector<TimerItem> local_items() {
  std::vector<TimerItem> out;
  const double elapsed = (esphome::millis() - timers.synced_ms) / 1000.0;
  if (timers.ringing)
    out.push_back({"local:ringing", "", timers.ringing_name.empty() ? "Timer" : timers.ringing_name, "Voice timer", "ringing", 0,
                   double(timers.ringing_total), true, false});
  for (const auto &l : timers.local) {
    TimerItem t{"local:" + l.id, "", l.name.empty() ? "Timer" : l.name, "Voice timer", l.active ? "active" : "paused",
                std::max(0.0, l.left - (l.active ? elapsed : 0.0)), double(l.total), true, false};
    out.push_back(t);
  }
  return out;
}
// Every timer the panel knows about: configured timer slots and local ones.
inline std::function<std::vector<std::string>()> timer_entities;
inline std::vector<TimerItem> all_timers() {
  std::vector<TimerItem> out = local_items();
  if (timer_entities)
    for (const auto &e : timer_entities()) {
      const auto *d = custom_logic_entity_sub::entity_detail(e);
      if (d == nullptr || !d->received || !d->available) continue;
      auto title = custom_logic::ha_entity_title_from_registry_cache(e);
      title = title.empty() ? e : onx_media::display_text(title);
      if (!d->voice_timer || d->voice_timers.empty()) { out.push_back(entity_timer(e, *d, title)); continue; }
      // A voice satellite: its ringing alarm, then each of its timers.
      if (d->state == "ringing") {
        TimerItem t{e + "#ringing", e, d->timer_name.empty() ? title : d->timer_name, "Voice timer", "ringing", 0, 0, true, false};
        t.voice = true;
        out.push_back(t);
      }
      const double age = (esphome::millis() - d->timer_received_ms) / 1000.0;
      std::vector<std::string> seen;
      for (const auto &v : d->voice_timers) {
        std::string key = e + '#' + v.name + '/' + std::to_string(v.duration);
        const std::string base = key;
        for (int n = 2; std::find(seen.begin(), seen.end(), key) != seen.end(); ++n) key = base + '/' + std::to_string(n);
        seen.push_back(key);
        TimerItem t{key, e, v.name.empty() ? "Timer" : v.name, "Voice timer", v.active ? "active" : "paused",
                    std::max(0.0, v.remaining - (v.active ? age : 0.0)), double(v.duration), true, false};
        t.voice = true;
        t.voice_name = v.name;
        out.push_back(t);
      }
    }
  return out;
}
inline bool running(const TimerItem &t) { return t.state == "active" || t.state == "paused" || t.state == "ringing"; }
// Ringing first, then the soonest running timer, then paused ones.
inline int rank(const TimerItem &t) { return t.state == "ringing" ? 0 : t.state == "active" ? 1 : t.state == "paused" ? 2 : 3; }
inline std::string finish_text(double left) {
  const time_t now = std::time(nullptr);
  return now > 1600000000 ? clock(now + time_t(std::lround(left))) : "";
}
inline void timer_button(lv_obj_t *parent, const Rect &r, const char *symbol, const std::string &caption, bool primary,
                         std::function<void()> fn) {
  lv_obj_t *b = button(parent, r.x, r.y, r.w, r.h, P(14), primary ? accent() : CONTROL(), "", fonts.small, TEXT(), std::move(fn));
  const uint32_t fg = primary ? 0x101418 : TEXT();
  const int gap = P(2), total = line(fonts.symbol) * 3 / 4 + gap + line(fonts.small);
  if (r.h >= total + P(8)) {
    lv_obj_t *s = text(b, symbol, fonts.symbol, fg, 0, (r.h - total) / 2 - P(4), r.w, LV_TEXT_ALIGN_CENTER);
    (void) s;
    text(b, caption, fonts.small, fg, 0, (r.h - total) / 2 + line(fonts.symbol) * 3 / 4 + gap, r.w, LV_TEXT_ALIGN_CENTER);
  } else {
    lv_obj_center(text(b, std::string(symbol) + "  " + caption, fonts.small, fg, 0, 0, r.w - P(8), LV_TEXT_ALIGN_CENTER));
  }
}
// The one action a timer row offers: Stop for a sounding panel timer, pause /
// resume for local Assist timers, voice satellite timers and HA timer helpers.
// Google timers are controlled on their own device. Returns false when there is none.
inline bool timer_quick_action(const TimerItem &t, const char **symbol, std::function<void()> *fn) {
  const bool local = t.key.rfind("local:", 0) == 0;
  if (local && t.state == "ringing") {
    *symbol = LV_SYMBOL_STOP; *fn = []() { if (stop_ringing) stop_ringing(); }; return true;
  }
  if (local && t.ready && set_local_timer_active && (t.state == "active" || t.state == "paused")) {
    const auto key = t.key.substr(6);
    const bool resume = t.state == "paused";
    *symbol = resume ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE;
    *fn = [key, resume]() { if (set_local_timer_active) set_local_timer_active(key, resume); };
    return true;
  }
  if (t.voice) {
    if (t.state == "ringing") { *fn = voice_alarm_stop(t); *symbol = LV_SYMBOL_STOP; return bool(*fn); }
    if (t.state != "active" && t.state != "paused") return false;
    const bool resume = t.state == "paused";
    *symbol = resume ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE;
    *fn = voice_sentence_for(t, resume ? onx_timer::Action::RESUME : onx_timer::Action::PAUSE);
    return true;
  }
  if (!t.helper || !t.ready) return false;
  const std::string e = t.entity;
  if (t.state == "active") { *symbol = LV_SYMBOL_PAUSE; *fn = [e]() { request(e, "timer.pause", "", "paused", "state"); }; return true; }
  if (t.state == "paused") { *symbol = LV_SYMBOL_PLAY; *fn = [e]() { request(e, "timer.start", "", "active", "state"); }; return true; }
  if (t.state == "ringing") { *symbol = LV_SYMBOL_CLOSE; *fn = [e]() { request(e, "timer.cancel", "", "idle", "state"); }; return true; }
  return false;
}
// Several running timers: one card each, finished first, then by time left.
// Portrait stacks rows; landscape uses a two-column grid. Tap a card for its
// full screen (ring, Cancel / Pause / +1 min).
__attribute__((noinline)) inline void build_timer_list(const std::vector<TimerItem> &items) {
  std::vector<const TimerItem *> list;
  for (const auto &t : items)
    if (running(t)) list.push_back(&t);
  const int pad = P(16), gap = P(10);
  int active = 0;
  for (const auto *t : list) active += t->state == "active";
  text(root, "Timers", fonts.title, TEXT(), pad, pad, W / 2);
  const std::string summary = std::to_string(active) + " running" +
                              (int(list.size()) > active ? " \xC2\xB7 " + std::to_string(list.size() - active) + " more" : "");
  text(root, summary, fonts.small, MUTED(), W / 2, pad + (line(fonts.title) - line(fonts.small)) / 2, W / 2 - pad, LV_TEXT_ALIGN_RIGHT);
  const int top = pad + line(fonts.title) + P(10), bottom = H - pad;
  const bool landscape = wide();
  const int cols = landscape ? 2 : 1;
  const int card_h = landscape ? P(96) : P(76);
  const int fit_rows = std::max(1, (bottom - top + gap) / (card_h + gap));
  int shown = std::min<int>(list.size(), fit_rows * cols);
  const int hidden = int(list.size()) - shown;
  if (hidden > 0) shown = std::max(1, shown - 1);  // Leave a slot for the "+N more" note.
  const int cw = (W - 2 * pad - (cols - 1) * gap) / cols;
  for (int i = 0; i < shown; ++i) {
    const TimerItem &t = *list[i];
    const bool ringing = t.state == "ringing", paused = t.state == "paused";
    const int x = pad + (i % cols) * (cw + gap), y = top + (i / cols) * (card_h + gap);
    lv_obj_t *card = box(root, x, y, cw, card_h, CARD(), P(16));
    if (ringing) {
      lv_obj_set_style_border_width(card, P(2), 0);
      lv_obj_set_style_border_color(card, lv_color_hex(accent()), 0);
    }
    const double fraction = ringing ? 1.0 : t.total > 0 ? std::clamp(t.left / t.total, 0.0, 1.0) : 0.0;
    const uint32_t tone = paused ? SOFT_TEXT() : accent();
    const std::string left = ringing ? "0:00" : !t.ready ? "--:--" : onx_timer::countdown(t.left);
    std::string sub = t.name;
    if (ringing) sub += " \xC2\xB7 time is up";
    else if (paused) sub += " \xC2\xB7 paused";
    else if (t.total > 0) sub += " \xC2\xB7 of " + onx_timer::countdown(t.total);
    const char *symbol = nullptr;
    std::function<void()> action;
    const bool has_action = timer_quick_action(t, &symbol, &action);
    const int bs = P(44), cp = P(14);
    TimerVisual visual; visual.key = t.key;
    if (!landscape) {
      // Ring, countdown with name, action button.
      const int ring = P(46);
      lv_obj_t *arc = lv_arc_create(card);
      visual.arc = arc;
      lv_obj_set_size(arc, ring, ring);
      lv_obj_set_pos(arc, cp, (card_h - ring) / 2);
      lv_arc_set_rotation(arc, 270);
      lv_arc_set_bg_angles(arc, 0, 360);
      lv_arc_set_range(arc, 0, 1000);
      lv_arc_set_value(arc, int(std::lround(fraction * 1000)));
      lv_obj_set_style_arc_width(arc, P(5), LV_PART_MAIN);
      lv_obj_set_style_arc_width(arc, P(5), LV_PART_INDICATOR);
      lv_obj_set_style_arc_color(arc, lv_color_hex(TRACK()), LV_PART_MAIN);
      lv_obj_set_style_arc_color(arc, lv_color_hex(tone), LV_PART_INDICATOR);
      lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
      lv_obj_set_style_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
      lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
      lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);
      lv_obj_remove_flag(arc, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
      const int tx = cp + ring + P(14), tw = cw - tx - cp - (has_action ? bs + P(10) : 0);
      const int block = line(fonts.title) + line(fonts.small);
      visual.value = text(card, left, fonts.title, ringing ? accent() : TEXT(), tx, (card_h - block) / 2, tw);
      text(card, sub, fonts.small, MUTED(), tx, (card_h - block) / 2 + line(fonts.title), tw);
    } else {
      // Countdown and action on top, name below, progress bar along the bottom.
      const int tw = cw - 2 * cp - (has_action ? bs + P(8) : 0);
      visual.value = text(card, left, fonts.title, ringing ? accent() : TEXT(), cp, cp - P(2), tw);
      text(card, sub, fonts.small, MUTED(), cp, cp + line(fonts.title), cw - 2 * cp);
      lv_obj_t *track = box(card, cp, card_h - cp - P(4), cw - 2 * cp, P(4), TRACK(), P(2));
      const int fill = int((cw - 2 * cp) * fraction);
      visual.bar_width = cw - 2 * cp;
      visual.bar = box(track, 0, 0, std::max(1, fill), P(4), tone, P(2));
      if (!fill) lv_obj_add_flag(visual.bar, LV_OBJ_FLAG_HIDDEN);
    }
    if (has_action) {
      const int by = landscape ? cp - P(6) : (card_h - bs) / 2;
      lv_obj_t *b = button(card, cw - cp - bs, by, bs, bs, bs / 2, ringing ? accent() : CONTROL(), symbol, fonts.symbol,
                           TEXT(), action);
      (void) b;
    }
    visual.last_value = left;
    timer_visuals.push_back(std::move(visual));
    interactive(card);
    const std::string key = t.key;
    on(card, LV_EVENT_CLICKED, [key](lv_event_t *) { if (allowed()) { timers.focus = key; built_signature.clear(); } });
  }
  if (hidden > 0) {
    const int i = shown;
    const int x = pad + (i % cols) * (cw + gap), y = top + (i / cols) * (card_h + gap);
    text(root, "+" + std::to_string(list.size() - shown) + " more timers", fonts.small, MUTED(), x, y + (card_h - line(fonts.small)) / 2, cw,
         LV_TEXT_ALIGN_CENTER);
  }
}
// The focused timer, controls and the other timers. `preferred` is the
// detail's own entity (or "" on the local timer screen).
__attribute__((noinline)) inline void build_timers(const std::string &preferred, const std::string &fallback_title) {
  timer_screen_active = true;
  auto items = all_timers();
  std::stable_sort(items.begin(), items.end(), [](const TimerItem &a, const TimerItem &b) {
    return rank(a) != rank(b) ? rank(a) < rank(b) : a.left < b.left;
  });
  int running_count = 0;
  for (const auto &t : items) running_count += running(t);
  // A focused timer that has gone away returns to the list.
  if (!timers.focus.empty() &&
      std::none_of(items.begin(), items.end(), [](const TimerItem &t) { return t.key == timers.focus && running(t); }))
    timers.focus.clear();
  if (timers.focus.empty() && running_count >= 2) { build_timer_list(items); return; }
  int focus = -1;
  for (int i = 0; i < int(items.size()); ++i)
    if (!timers.focus.empty() && items[i].key == timers.focus) focus = i;
  if (focus < 0)
    for (int i = 0; i < int(items.size()); ++i)
      if (focus < 0 && !preferred.empty() && (items[i].key == preferred || items[i].entity == preferred)) focus = i;
  if (focus < 0 && !items.empty() && (preferred.empty() || running(items[0]))) focus = 0;
  TimerItem t;
  if (focus >= 0) t = items[focus];
  else { t.name = fallback_title; t.kind = "Timer"; t.state = "idle"; }
  const bool ringing = t.state == "ringing";
  const bool local = t.key.rfind("local:", 0) == 0;
  // Header.
  std::string context = t.kind;
  if (!t.ready && !local) context += " · syncing";
  else if (ringing) context = "Time is up";
  else if (t.state == "active") { const auto f = finish_text(t.left); if (!f.empty()) context += " · finishes " + f; }
  else if (t.state == "paused") context += " · paused";
  else context += " · idle";
  const auto base_context = context;
  if (timers.message_key == t.key && !timers.message.empty() && int32_t(timers.message_until - esphome::millis()) > 0)
    context = timers.message;
  else if (t.helper) {
    const auto feedback = onx_controls::feedback_text(t.entity);
    if (!feedback.empty()) context = feedback;
  }
  const int pad = P(16);
  const bool landscape = wide();
  const Rect col = landscape ? Rect{W * 48 / 100 + P(8), pad, W - W * 48 / 100 - P(8) - pad, H - 2 * pad} : Rect{pad, pad, W - 2 * pad, H - 2 * pad};
  // With several running timers, a button returns to the list.
  const int list_w = running_count >= 2 ? P(44) : 0;
  if (list_w)
    button(root, col.right() - list_w, col.y, list_w, list_w, list_w / 2, CONTROL(), LV_SYMBOL_LIST, fonts.symbol, TEXT(),
           []() { timers.focus.clear(); built_signature.clear(); });
  const int name_w = col.w - (list_w ? list_w + P(8) : 0);
  const lv_font_t *nf = fit(t.name, name_w, {fonts.title, fonts.body});
  text(root, t.name, nf, TEXT(), col.x, col.y, name_w);
  auto *context_label = text(root, context, fonts.small, ringing ? accent() : MUTED(), col.x, col.y + line(nf), name_w);
  int y = col.y + line(nf) + line(fonts.small) + P(14);
  // Controls: only what the timer's own system supports.
  struct Control { const char *symbol; std::string caption; bool primary; std::function<void()> fn; };
  std::vector<Control> controls;
  const std::string e = t.entity;
  if (local && ringing) {
    controls.push_back({LV_SYMBOL_STOP, "Stop", true, []() { if (stop_ringing) stop_ringing(); }});
  } else if (local) {
    const auto key = t.key.substr(6);
    if (stop_local_timer && (t.state == "active" || t.state == "paused"))
      controls.push_back({LV_SYMBOL_STOP, "Stop", false, [key]() { if (stop_local_timer) stop_local_timer(key); }});
    const char *symbol = nullptr;
    std::function<void()> action;
    if (timer_quick_action(t, &symbol, &action))
      controls.push_back({symbol, t.state == "paused" ? "Resume" : "Pause", true, std::move(action)});
    if (extend_local_timer && t.ready && (t.state == "active" || t.state == "paused"))
      controls.push_back({LV_SYMBOL_PLUS, "1 min", false, [key]() { if (extend_local_timer) extend_local_timer(key); }});
  } else if (t.voice) {
    if (ringing) {
      if (auto stop = voice_alarm_stop(t)) controls.push_back({LV_SYMBOL_STOP, "Stop", true, std::move(stop)});
    } else if (t.state == "active" || t.state == "paused") {
      controls.push_back({LV_SYMBOL_STOP, "Stop", false, voice_sentence_for(t, onx_timer::Action::STOP)});
      const char *symbol = nullptr;
      std::function<void()> action;
      if (timer_quick_action(t, &symbol, &action))
        controls.push_back({symbol, t.state == "paused" ? "Resume" : "Pause", true, std::move(action)});
      controls.push_back({LV_SYMBOL_PLUS, "1 min", false, voice_sentence_for(t, onx_timer::Action::ADD_MINUTE)});
    }
  } else if (t.helper && t.ready) {
    const bool active = t.state == "active", paused = t.state == "paused";
    if (active || paused) {
      controls.push_back({LV_SYMBOL_STOP, "Stop", false, [e]() { request(e, "timer.cancel", "", "idle", "state"); }});
      controls.push_back({active ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY, active ? "Pause" : "Resume", true,
                          [e, active]() { request(e, active ? "timer.pause" : "timer.start", "", active ? "paused" : "active", "state"); }});
      controls.push_back({LV_SYMBOL_PLUS, "1 min", false, [e]() { if (extend_helper_timer) extend_helper_timer(e); }});
    } else {
      controls.push_back({LV_SYMBOL_PLAY, ringing ? "Restart" : "Start", true, [e]() { request(e, "timer.start", "", "active", "state"); }});
    }
  }
  // Other timers below (tap to focus).
  std::vector<TimerItem> others;
  for (int i = 0; i < int(items.size()); ++i)
    if (i != focus && running(items[i])) others.push_back(items[i]);
  const int bh = P(64), rh = P(52), gap = P(8);
  const int bottom = col.bottom();
  int controls_y = bottom, rows = 0;
  const int min_ring = P(150);
  if (!landscape) {
    rows = std::min<int>(others.size(), 2);
    auto ring_space = [&](int n) {
      return bottom - y - (controls.empty() ? 0 : bh + P(14)) - n * (rh + gap) - P(6);
    };
    while (rows > 0 && ring_space(rows) < min_ring) --rows;
    controls_y = bottom - rows * (rh + gap) - (controls.empty() ? 0 : bh);
  } else {
    rows = std::min<int>(others.size(), std::max(0, (bottom - y - (controls.empty() ? 0 : bh + P(12))) / (rh + gap)));
    controls_y = bottom - rows * (rh + gap) - (controls.empty() ? 0 : bh);
  }
  // Ring.
  const Rect ring_area = landscape ? Rect{pad, pad, W * 48 / 100 - pad, H - 2 * pad}
                                   : Rect{pad, y, W - 2 * pad, (controls.empty() ? controls_y : controls_y - P(14)) - y};
  const int size = std::max(P(100), std::min({ring_area.w, ring_area.h, P(250)}));
  const int cx = ring_area.x + ring_area.w / 2, ay = ring_area.y + (ring_area.h - size) / 2;
  lv_obj_t *arc = lv_arc_create(root);
  lv_obj_set_size(arc, size, size);
  lv_obj_set_pos(arc, cx - size / 2, ay);
  lv_arc_set_rotation(arc, 270);
  lv_arc_set_bg_angles(arc, 0, 360);
  lv_arc_set_range(arc, 0, 1000);
  const double fraction = ringing ? 1.0 : t.total > 0 ? std::clamp(t.left / t.total, 0.0, 1.0) : 0.0;
  lv_arc_set_value(arc, int(std::lround(fraction * 1000)));
  lv_obj_set_style_arc_width(arc, P(14), LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, P(14), LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, lv_color_hex(CONTROL()), LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, lv_color_hex(t.state == "paused" ? SOFT_TEXT() : accent()), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
  lv_obj_set_style_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);
  lv_obj_remove_flag(arc, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  const std::string value = !t.ready && !local ? "--:--" : ringing ? "0:00" : onx_timer::countdown(t.left);
  const int inner = size - 2 * P(14) - P(16);
  // Keep countdowns at the normal reading size. The digit-only hero font can
  // fit some second values but not others, causing a size jump on each tick.
  const lv_font_t *vf = fit(value, inner, {fonts.big, fonts.title});
  const std::string sub = ringing ? "Time is up" : t.total > 0 ? "of " + onx_timer::countdown(t.total) : "";
  const int block = line(vf) + (sub.empty() ? 0 : line(fonts.small));
  const int vy = ay + (size - block) / 2;
  auto *countdown_label = text(root, value, vf, TEXT(), cx - size / 2, vy, size, LV_TEXT_ALIGN_CENTER);
  timer_visuals.push_back({t.key, countdown_label, arc, nullptr, 0, -1, value, context_label, base_context, context});
  if (!sub.empty()) text(root, sub, fonts.small, ringing ? accent() : MUTED(), cx - size / 2, vy + line(vf) - P(6), size, LV_TEXT_ALIGN_CENTER);
  // Buttons.
  if (!controls.empty()) {
    const int n = controls.size(), bw = (col.w - gap * (n - 1)) / n;
    for (int i = 0; i < n; ++i)
      timer_button(root, {col.x + i * (bw + gap), controls_y, i == n - 1 ? col.w - (n - 1) * (bw + gap) : bw, bh},
                   controls[i].symbol, controls[i].caption, controls[i].primary, controls[i].fn);
  }
  // Rows.
  int ry = controls_y + (controls.empty() ? 0 : bh + gap);
  for (int i = 0; i < rows; ++i, ry += rh + gap) {
    const auto &o = others[i];
    lv_obj_t *row = box(root, col.x, ry, col.w, rh, CARD(), P(14));
    const int rp = P(14), dot = P(22);
    lv_obj_t *mini = lv_arc_create(row);
    lv_obj_set_size(mini, dot, dot);
    lv_obj_set_pos(mini, rp, (rh - dot) / 2);
    lv_arc_set_rotation(mini, 270);
    lv_arc_set_bg_angles(mini, 0, 360);
    lv_arc_set_range(mini, 0, 1000);
    lv_arc_set_value(mini, o.total > 0 ? int(std::clamp(o.left / o.total, 0.0, 1.0) * 1000) : 1000);
    lv_obj_set_style_arc_width(mini, P(3), LV_PART_MAIN);
    lv_obj_set_style_arc_width(mini, P(3), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(mini, lv_color_hex(TRACK()), LV_PART_MAIN);
    lv_obj_set_style_arc_color(mini, lv_color_hex(o.state == "paused" ? SOFT_TEXT() : accent()), LV_PART_INDICATOR);
    lv_obj_set_style_opa(mini, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(mini, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(mini, 0, LV_PART_MAIN);
    lv_obj_remove_flag(mini, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    const std::string left = o.state == "ringing" ? "0:00" : onx_timer::countdown(o.left);
    const int lw = text_width(left, fonts.body) + P(2);
    text(row, o.name, fonts.small, SOFT_TEXT(), rp + dot + P(12), (rh - line(fonts.small)) / 2, col.w - rp * 2 - dot - P(20) - lw);
    auto *value_label = text(row, left, fonts.body, o.state == "ringing" ? accent() : TEXT(), col.w - rp - lw, (rh - line(fonts.body)) / 2, lw);
    timer_visuals.push_back({o.key, value_label, mini, nullptr, 0, -1, left});
    interactive(row);
    const std::string key = o.key;
    on(row, LV_EVENT_CLICKED, [key](lv_event_t *) { if (allowed()) { timers.focus = key; built_signature.clear(); } });
  }
}
// Countdown updates preserve the object tree, especially the list and action
// buttons. Only changes to timer identity/state/layout rebuild the screen.
inline void tick_timers() {
  if (!timer_screen_active) return;
  const auto items = all_timers();
  for (auto &visual : timer_visuals) {
    const auto it = std::find_if(items.begin(), items.end(), [&](const TimerItem &t) { return t.key == visual.key; });
    if (it == items.end()) continue;
    const auto &t = *it;
    if (visual.context) {
      auto context = visual.base_context;
      if (timers.message_key == t.key && !timers.message.empty() && int32_t(timers.message_until - esphome::millis()) > 0)
        context = timers.message;
      else if (t.helper) {
        const auto feedback = onx_controls::feedback_text(t.entity);
        if (!feedback.empty()) context = feedback;
      }
      if (context != visual.last_context) { lv_label_set_text(visual.context, context.c_str()); visual.last_context = context; }
    }
    const auto value = !t.ready ? "--:--" : t.state == "ringing" ? "0:00" : onx_timer::countdown(t.left);
    if (visual.value && value != visual.last_value) {
      lv_label_set_text(visual.value, value.c_str()); visual.last_value = value;
    }
    const double fraction = t.state == "ringing" ? 1.0 : t.total > 0 ? std::clamp(t.left / t.total, 0.0, 1.0) : 0.0;
    const int progress = int(std::lround(fraction * 1000));
    if (progress == visual.last_progress) continue;
    visual.last_progress = progress;
    if (visual.arc) lv_arc_set_value(visual.arc, progress);
    if (visual.bar) {
      const int width = int(visual.bar_width * fraction);
      lv_obj_set_width(visual.bar, std::max(1, width));
      if (width) lv_obj_remove_flag(visual.bar, LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(visual.bar, LV_OBJ_FLAG_HIDDEN);
    }
  }
}
inline std::string timer_signature() {
  std::string s = timers.focus + '|' + std::to_string(timers.ringing);
  for (const auto &t : all_timers()) {
    s += '|' + t.key + t.name + t.state + ',' + std::to_string(long(t.total)) + std::to_string(t.ready);
  }
  return s;
}

// The hero font carries only digits, separators (. , : -), % and the degree sign.
inline bool hero_safe(const std::string &s) {
  if (fonts.hero == nullptr || s.empty()) return false;
  for (const char c : s)
    if (!((c >= '0' && c <= '9') || c == '.' || c == '-' || c == ',' || c == ':' || c == ' ')) return false;
  return true;
}
inline bool hero_text(const std::string &s) {  // Also allows the degree sign.
  const std::string degree = "°";
  std::string rest = s;
  for (size_t at; (at = rest.find(degree)) != std::string::npos;) rest.erase(at, degree.size());
  return hero_safe(rest);
}
// A large reading with its unit set smaller on the same baseline. Returns the
// height used.
inline int reading(lv_obj_t *parent, const std::string &value, const std::string &unit, int x, int y, int w,
                   bool hero = true) {
  const std::string u = unit.empty() ? "" : (unit == "%" || unit == "°" ? unit : " " + unit);
  const int uw = u.empty() ? 0 : text_width(u, fonts.title);
  const lv_font_t *font = hero && hero_safe(value) && text_width(value, fonts.hero) + uw <= w ? fonts.hero
                          : text_width(value, fonts.big) + uw <= w                             ? fonts.big
                                                                                                : fonts.title;
  const int vw = std::min(text_width(value, font), w - uw);
  text(parent, value, font, TEXT(), x, y, vw + 1);
  const int base = y + font->line_height - font->base_line;  // Baseline of the value.
  if (!u.empty())
    text(parent, u, fonts.title, SOFT_TEXT(), x + vw + P(2), base - (fonts.title->line_height - fonts.title->base_line),
         w - vw - P(2));
  return line(font);
}
// A rounded level bar (0..1) with an accent fill.
inline void level(lv_obj_t *parent, int x, int y, int w, int h, float fraction) {
  box(parent, x, y, w, h, CONTROL(), h / 2);
  if (!std::isfinite(fraction)) return;
  fraction = std::clamp(fraction, 0.0f, 1.0f);
  if (fraction > 0) box(parent, x, y, std::max(h, int(w * fraction)), h, accent(), h / 2);
}

// ------------------------------------------------------------ card charts
// A history line drawn straight into the frame buffer by one plain object per
// chart (no canvas buffer, no extra internal RAM). Points come from the same
// HA statistics request the sensor detail graph uses.
struct Chart {
  std::vector<onx_sensor_view::Point> points;
  float lo{0}, hi{0};
  uint32_t start{0}, end{0}, gap{1200}, color{0};
  bool fill{false}, bars{false};
};
inline std::vector<std::unique_ptr<Chart>> charts;
inline void draw_chart(lv_event_t *ev) {
  const auto *c = static_cast<const Chart *>(lv_event_get_user_data(ev));
  auto *o = static_cast<lv_obj_t *>(lv_event_get_current_target(ev));
  if (c == nullptr || o == nullptr || c->points.size() < 2) return;
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  const int w = a.x2 - a.x1, h = a.y2 - a.y1, inset = std::max(2, h / 12);
  const float span = std::max(c->hi - c->lo, 1e-3f);
  const uint32_t range = std::max<uint32_t>(1, c->end - c->start);
  auto px = [&](const onx_sensor_view::Point &p) {
    const uint32_t at = std::clamp(p.epoch, c->start, c->end);
    return int32_t(a.x1 + int64_t(at - c->start) * w / range);
  };
  auto py = [&](float v) { return int32_t(a.y1 + inset + (c->hi - v) / span * (h - 2 * inset)); };
  lv_layer_t *layer = lv_event_get_layer(ev);
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  if (c->bars) {  // Averages in equal time buckets, drawn as columns from the bottom.
    constexpr int N = 24;
    float sum[N] = {}; int n[N] = {};
    for (const auto &p : c->points) {
      if (p.epoch < c->start || p.epoch > c->end) continue;
      const int b = std::min<int>(N - 1, int(int64_t(p.epoch - c->start) * N / range));
      sum[b] += p.value; ++n[b];
    }
    const float top = std::max(c->hi, 1e-3f);
    const int slot = std::max(1, w / N), bw = std::max(2, slot * 2 / 3);
    d.width = bw;
    d.color = lv_color_hex(c->color);
    d.opa = LV_OPA_COVER;
    for (int b = 0; b < N; ++b) {
      if (!n[b]) continue;
      const float v = std::max(0.0f, sum[b] / n[b]);
      const int32_t bh = std::max<int32_t>(2, int32_t(v / top * h));
      d.p1.x = d.p2.x = a.x1 + b * w / N + slot / 2;
      d.p1.y = a.y2;
      d.p2.y = a.y2 - bh;
      lv_draw_line(layer, &d);
    }
    return;
  }
  if (c->fill) {  // Soft area under the line, one thin column every 2 px.
    d.width = 2;
    d.color = lv_color_hex(c->color);
    d.opa = LV_OPA_20;
    for (size_t i = 1; i < c->points.size(); ++i) {
      const auto &p0 = c->points[i - 1], &p1 = c->points[i];
      if (p1.epoch - p0.epoch > c->gap) continue;
      const int32_t x0 = px(p0), x1 = px(p1), y0 = py(p0.value), y1 = py(p1.value);
      for (int32_t x = x0; x < x1; x += 2) {
        d.p1.x = d.p2.x = x;
        d.p1.y = y0 + (y1 - y0) * (x - x0) / std::max<int32_t>(1, x1 - x0);
        d.p2.y = a.y2;
        lv_draw_line(layer, &d);
      }
    }
  }
  d.opa = LV_OPA_COVER;
  d.width = std::max<int32_t>(2, P(3));
  d.round_start = d.round_end = 1;
  d.color = lv_color_hex(c->color);
  for (size_t i = 1; i < c->points.size(); ++i) {
    const auto &p0 = c->points[i - 1], &p1 = c->points[i];
    if (p1.epoch - p0.epoch > c->gap) continue;
    d.p1.x = px(p0); d.p1.y = py(p0.value);
    d.p2.x = px(p1); d.p2.y = py(p1.value);
    lv_draw_line(layer, &d);
  }
}
// History for a card when the entity's graph option is on; asks HA for it
// (rate-limited by the request script) while there is none yet.
inline std::vector<onx_sensor_view::Point> card_history(const std::string &e) {
  const int i = onx_sensor_view::index(e);
  if (i < 0 || !onx_sensor_view::configs[i].graph) return {};
  auto &st = onx_sensor_view::states[i];
  auto h = st.history(std::time(nullptr));
  // Live samples alone (collected before Home Assistant was reachable) are
  // not history: ask until a request has gone out for this range.
  if (st.request_due(esphome::millis()) && request_history) request_history(e);
  return h;
}
inline const Chart *chart(lv_obj_t *parent, int x, int y, int w, int h, const std::string &e,
                          std::vector<onx_sensor_view::Point> points, uint32_t color, bool fill, bool bars = false) {
  const int i = onx_sensor_view::index(e);
  if (i < 0 || points.size() < 2 || w < P(24) || h < P(12)) return nullptr;
  auto c = std::make_unique<Chart>();
  c->lo = c->hi = points.front().value;
  for (const auto &p : points) { c->lo = std::min(c->lo, p.value); c->hi = std::max(c->hi, p.value); }
  const uint32_t range = onx_sensor_view::states[i].range_seconds;
  c->end = uint32_t(std::time(nullptr));
  c->start = c->end - range;
  c->gap = std::max<uint32_t>(1200, range / 24);
  c->color = color;
  c->fill = fill;
  c->bars = bars;
  c->points = std::move(points);
  lv_obj_t *o = box(parent, x, y, w, h, CARD(), 0, LV_OPA_TRANSP);
  lv_obj_add_event_cb(o, draw_chart, LV_EVENT_DRAW_MAIN, c.get());
  charts.push_back(std::move(c));
  return charts.back().get();
}

__attribute__((noinline)) inline void build_sensor(const Layout &l, const std::string &e, const EntityDetailState &d, const std::string &title,
                         const onx_detail_view::View &v) {
  const auto kind = onx_detail_view::sensor_class(e);
  std::string context = kind.empty() ? "Sensor" : label(kind);
  const int i = onx_sensor_view::index(e);
  if (i >= 0 && onx_sensor_view::configs[i].secondary[0]) context = v.name.text;
  // Long names get two lines instead of overlapping the context line.
  const Rect head = l.head;
  const int name_lines = lines_for(title, fonts.title, head.w, 2);
  text(root, title, fonts.title, TEXT(), head.x, head.y, head.w, LV_TEXT_ALIGN_LEFT, name_lines);
  int y = head.y + name_lines * line(fonts.title) + P(2);
  text(root, context, fonts.small, MUTED(), head.x, y, head.w);
  y += line(fonts.small) + P(10);

  const float value = num(d.state);
  const bool numeric = std::isfinite(value);
  const Rect area{l.vis.x, wide() ? l.vis.y : y, l.vis.w, wide() ? l.vis.h : l.ctl.bottom() - y};
  y = area.y;
  if (numeric) y += reading(root, onx_detail_view::compact(d.state), d.unit, area.x, y, area.w);
  else {
    const int n = lines_for(v.value.text, fonts.big, area.w, 3);
    text(root, v.value.text, fonts.big, TEXT(), area.x, y, area.w, LV_TEXT_ALIGN_LEFT, n);
    y += n * line(fonts.big);
  }
  const bool graph = i >= 0 && onx_sensor_view::configs[i].graph;
  std::vector<onx_sensor_view::Point> history;
  if (i >= 0) history = onx_sensor_view::states[i].history(std::time(nullptr));
  if (graph && !onx_sensor_view::states[i].requested && !onx_sensor_view::states[i].pending && request_history) request_history(e);
  if (history.size() < 2 && graph) {
    const auto &state = onx_sensor_view::states[i];
    text(root, state.waiting(esphome::millis()) ? "Loading history…" : state.pending || state.failed ? "History unavailable" : state.requested ? "No recorded statistics" : "Collecting history…", fonts.small, MUTED(), area.x, y, area.w);
    y += line(fonts.small);
  }
  // Percentages also get a level bar.
  if (!graph && numeric && d.unit == "%" && value >= 0 && value <= 100) {
    y += P(14);
    level(root, area.x, y, area.w, P(14), value / 100.0f);
    y += P(14) + P(6);
    text(root, "0", fonts.small, MUTED(), area.x, y);
    text(root, "100%", fonts.small, MUTED(), area.x, y, area.w, LV_TEXT_ALIGN_RIGHT);
    y += line(fonts.small);
  }
  y += P(10);
  if (graph && i >= 0) {
    const auto range = onx_sensor_view::states[i].range_seconds;
    chips(root, {area.x, y, area.w, P(36)}, {"1 h", "12 h", "24 h", "7 d"}, range == 3600 ? 0 : range == 86400 ? 2 : range == 604800 ? 3 : 1,
          [e, i](int selected) {
            auto &state = onx_sensor_view::states[i];
            if (state.pending) return;
            state.range_seconds = selected == 0 ? 3600 : selected == 1 ? 43200 : selected == 2 ? 86400 : 604800;
            state.points = {}; state.requested = false;
            built_signature.clear();
            if (request_history) request_history(e);
          });
    y += P(46);
  }
  // Min / avg / max of the history along the bottom; the graph takes the rest.
  const bool stats = history.size() >= 2;
  const int th = P(52);
  const Rect bottom = wide() ? Rect{l.ctl.x, l.ctl.bottom() - th, l.ctl.w, th} : Rect{area.x, area.bottom() - th, area.w, th};
  if (graph && draw_graph) {
    const Rect g = wide() ? Rect{l.ctl.x, l.ctl.y, l.ctl.w, (stats ? bottom.y - P(12) : l.ctl.bottom()) - l.ctl.y}
                          : Rect{area.x, y, area.w, (stats ? bottom.y - P(8) : area.bottom()) - y};
    if (g.h >= P(80)) {
      lv_obj_t *frame = box(root, g.x, g.y, g.w, g.h, CARD(), P(12));
      draw_graph(frame, e, P(12));
      text(frame, i >= 0 ? onx_sensor_view::states[i].range_label() : "12 h", fonts.small, MUTED(), P(10), g.h - line(fonts.small) - P(6));
      text(frame, "now", fonts.small, MUTED(), P(10), g.h - line(fonts.small) - P(6), g.w - P(20), LV_TEXT_ALIGN_RIGHT);
    }
  }
  if (stats) {
    float lo = history.front().value, hi = lo, sum = 0;
    for (const auto &p : history) { lo = std::min(lo, p.value); hi = std::max(hi, p.value); sum += p.value; }
    const int gap = P(10), tw = (bottom.w - 2 * gap) / 3;
    tile(root, {bottom.x, bottom.y, tw, th}, "Min", fmt(lo, 1));
    tile(root, {bottom.x + tw + gap, bottom.y, tw, th}, "Avg", fmt(sum / history.size(), 1));
    tile(root, {bottom.x + 2 * (tw + gap), bottom.y, bottom.w - 2 * (tw + gap), th}, "Max", fmt(hi, 1));
  }
}

inline std::string binary_text(const std::string &kind, const std::string &state) {
  const bool on = state == "on";
  if (state != "on" && state != "off") return label(state);
  // Home Assistant binary_sensor device-class wording.
  struct Pair { const char *kind, *on, *off; };
  static const Pair pairs[] = {
      {"battery", "Low", "Normal"}, {"battery_charging", "Charging", "Not charging"},
      {"co", "Detected", "Clear"}, {"cold", "Cold", "Normal"}, {"connectivity", "Connected", "Disconnected"},
      {"door", "Open", "Closed"}, {"garage_door", "Open", "Closed"}, {"opening", "Open", "Closed"},
      {"window", "Open", "Closed"}, {"gas", "Detected", "Clear"}, {"glass_break", "Detected", "Clear"},
      {"heat", "Hot", "Normal"}, {"light", "Light detected", "No light"}, {"lock", "Unlocked", "Locked"},
      {"moisture", "Wet", "Dry"}, {"motion", "Detected", "Clear"}, {"moving", "Moving", "Not moving"},
      {"occupancy", "Detected", "Clear"}, {"plug", "Plugged in", "Unplugged"}, {"power", "Power", "No power"},
      {"presence", "Home", "Away"}, {"problem", "Problem", "OK"}, {"running", "Running", "Not running"},
      {"safety", "Unsafe", "Safe"}, {"smoke", "Detected", "Clear"}, {"sound", "Detected", "Clear"},
      {"tamper", "Tampering", "Clear"}, {"update", "Update available", "Up-to-date"},
      {"vibration", "Detected", "Clear"}};
  for (const auto &p : pairs)
    if (kind == p.kind) return on ? p.on : p.off;
  return on ? "On" : "Off";
}
__attribute__((noinline)) inline void build_binary(const Layout &l, const std::string &e, const EntityDetailState &d, const std::string &title) {
  const auto kind = onx_detail_view::sensor_class(e);
  const bool on = d.state == "on";
  header(l.head, title, kind.empty() ? "Binary sensor" : label(kind) + " sensor");
  const int size = std::min({P(170), l.vis.w, l.vis.h - P(40)});
  const int cx = l.vis.x + l.vis.w / 2, cy = l.vis.y + (l.vis.h - P(30)) / 2;
  lv_obj_t *disc = box(root, cx - size / 2, cy - size / 2, size, size, on ? soft() : CARD(), LV_RADIUS_CIRCLE);
  lv_obj_set_style_border_width(disc, P(3), 0);
  lv_obj_set_style_border_color(disc, lv_color_hex(on ? accent() : CONTROL()), 0);
  lv_obj_t *state = text(disc, binary_text(kind, d.state), fonts.body, on ? accent() : TEXT(), 0, 0, size - P(16), LV_TEXT_ALIGN_CENTER);
  lv_obj_center(state);
  const auto *c = changes_for(e);
  const time_t now = std::time(nullptr);
  if (c && !c->items.empty())
    text(root, ago(c->items.front().at, now), fonts.small, MUTED(), l.vis.x, cy + size / 2 + P(10), l.vis.w, LV_TEXT_ALIGN_CENTER);
  if (!c || c->items.empty()) {
    text(root, "Changes appear here while the panel is running", fonts.small, MUTED(), l.ctl.x, l.ctl.y, l.ctl.w);
    return;
  }
  const int rh = P(36), gap = P(6);
  int y = l.ctl.y;
  for (const auto &item : c->items) {
    if (y + rh > l.ctl.bottom() + P(4)) break;
    lv_obj_t *r = row(root, {l.ctl.x, y, l.ctl.w, rh}, binary_text(kind, item.state), clock(item.at));
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    y += rh + gap;
  }
}

__attribute__((noinline)) inline void build_event(const Layout &l, const std::string &e, const EntityDetailState &d, const std::string &title) {
  const auto kind = onx_detail_view::sensor_class(e);
  const bool never = d.state.empty() || d.state == "unknown";
  header(l.head, title, kind == "doorbell" ? "Doorbell" : kind == "motion" ? "Motion events" : kind == "button" ? "Remote button" : "Events");
  const int size = std::min({P(170), l.vis.w, l.vis.h - P(40)});
  const int cx = l.vis.x + l.vis.w / 2, cy = l.vis.y + (l.vis.h - P(30)) / 2;
  lv_obj_t *disc = box(root, cx - size / 2, cy - size / 2, size, size, never ? CARD() : soft(), LV_RADIUS_CIRCLE);
  lv_obj_set_style_border_width(disc, P(3), 0);
  lv_obj_set_style_border_color(disc, lv_color_hex(never ? CONTROL() : accent()), 0);
  lv_obj_t *state = text(disc, never ? "No events" : label(d.event_type.empty() ? "event" : d.event_type), fonts.body,
                         never ? TEXT() : accent(), 0, 0, size - P(16), LV_TEXT_ALIGN_CENTER, 2);
  lv_obj_center(state);
  if (!never)
    text(root, onx_detail_view::event_ago(d.state), fonts.small, MUTED(), l.vis.x, cy + size / 2 + P(10), l.vis.w, LV_TEXT_ALIGN_CENTER);
  const auto *c = changes_for(e);
  if (!c || c->items.empty()) {
    text(root, "New events appear here while the panel is running", fonts.small, MUTED(), l.ctl.x, l.ctl.y, l.ctl.w);
    return;
  }
  const int rh = P(36), gap = P(6);
  int y = l.ctl.y;
  for (const auto &item : c->items) {
    if (y + rh > l.ctl.bottom() + P(4)) break;
    lv_obj_t *r = row(root, {l.ctl.x, y, l.ctl.w, rh}, label(item.state), clock(item.at));
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    y += rh + gap;
  }
}

__attribute__((noinline)) inline void build_number(const Layout &l, const std::string &e, const EntityDetailState &d, const std::string &title) {
  float value = num(onx_controls::effective(d, "state"));
  clear_pending_if(e, "value", value);
  const float want = pending_value(e, "value");
  if (std::isfinite(want)) value = want;
  const float lo = std::isfinite(d.minimum) ? d.minimum : 0, hi = std::isfinite(d.maximum) && d.maximum > lo ? d.maximum : 100;
  const float step = std::isfinite(d.step) && d.step > 0 ? d.step : 1;
  header(l.head, title, fmt(lo) + " – " + fmt(hi) + " · step " + fmt(step));
  lv_obj_t *big = text(root, fmt(value), fonts.big, TEXT(), l.vis.x, l.vis.y + l.vis.h / 5, l.vis.w, LV_TEXT_ALIGN_CENTER);
  if (!d.unit.empty())
    text(root, d.unit, fonts.body, SOFT_TEXT(), l.vis.x, l.vis.y + l.vis.h / 5 + line(fonts.big), l.vis.w, LV_TEXT_ALIGN_CENTER);
  // Horizontal slider: a tall invisible hit area around an 8 px track.
  const int sy = l.vis.y + l.vis.h * 64 / 100, hit = P(48), kw = P(28);
  lv_obj_t *area = box(root, l.vis.x, sy - hit / 2, l.vis.w, hit, BG(), 0, LV_OPA_TRANSP);
  const int tw = l.vis.w - kw;
  box(area, kw / 2, (hit - P(8)) / 2, tw, P(8), TRACK(), P(4));
  const float frac = std::isfinite(value) ? std::clamp((value - lo) / (hi - lo), 0.0f, 1.0f) : 0;
  lv_obj_t *fill = box(area, kw / 2, (hit - P(8)) / 2, int(tw * frac), P(8), accent(), P(4));
  lv_obj_t *knob = box(area, int(tw * frac), (hit - kw) / 2, kw, kw, TEXT(), LV_RADIUS_CIRCLE);
  text(root, fmt(lo), fonts.small, MUTED(), l.vis.x, sy + hit / 2);
  text(root, fmt(hi), fonts.small, MUTED(), l.vis.x, sy + hit / 2, l.vis.w, LV_TEXT_ALIGN_RIGHT);
  if (d.available) {
    drag(area, [e, area, fill, knob, big, tw, kw, hit, lo, hi, step](lv_point_t p, bool commit) {
      lv_area_t a;
      lv_obj_get_coords(area, &a);
      const float f = std::clamp(float(p.x - a.x1 - kw / 2) / tw, 0.0f, 1.0f);
      const float v = std::clamp(lo + std::round(f * (hi - lo) / step) * step, lo, hi);
      const int x = int(tw * (v - lo) / (hi - lo));
      lv_obj_set_size(fill, x, P(8));
      lv_obj_set_pos(knob, x, (hit - kw) / 2);
      lv_label_set_text(big, fmt(v).c_str());
      if (!commit) return;
      set_pending(e, "value", v);
      const auto *d = custom_logic_entity_sub::entity_detail(e);
      if (!d) return;
      custom_logic_entity_sub::select_control_entity(e);
      onx_controls::request(*d, onx_controls::action(*d, "state"), "state", "value", std::to_string(v));
    });
  }
  const int bs = P(56), cy = l.ctl.y + bs / 2;
  auto stepper = [e](int direction) {
    custom_logic_entity_sub::select_control_entity(e);
    custom_logic_entity_sub::control_setting = "state";
    onx_controls::rotate(e, direction);
  };
  circle(root, l.ctl.x + bs / 2, cy, bs, CONTROL(), LV_SYMBOL_MINUS, TEXT(), [stepper]() { stepper(-1); });
  circle(root, l.ctl.right() - bs / 2, cy, bs, CONTROL(), LV_SYMBOL_PLUS, TEXT(), [stepper]() { stepper(1); });
  if (l.ctl.h >= bs + P(28))
    text(root, "Swipe up / down also steps by " + fmt(step), fonts.small, MUTED(), l.ctl.x, l.ctl.y + bs + P(8), l.ctl.w,
         LV_TEXT_ALIGN_CENTER);
}

__attribute__((noinline)) inline void build_select(const Layout &l, const std::string &e, const EntityDetailState &d, const std::string &title) {
  const auto options = onx_control::options(d.options);
  const auto current = onx_controls::effective(d, "state");
  header(l.head, title, std::to_string(options.size()) + " options");
  // In landscape the list uses the right column; in portrait everything below the name.
  const Rect r = wide() ? Rect{l.ctl.x, l.ctl.y, l.ctl.w, l.ctl.h} : Rect{l.vis.x, l.vis.y, l.vis.w, H - P(16) - l.vis.y};
  lv_obj_t *list = box(root, r.x, r.y, r.w, r.h, BG(), 0, LV_OPA_TRANSP);
  lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
  interactive(list, false);  // Scroll gestures go to LVGL.
  const int bh = P(52), gap = P(8);
  for (size_t i = 0; i < options.size(); ++i) {
    const auto option = options[i];
    const bool selected = option == current;
    lv_obj_t *b = button(list, 0, i * (bh + gap), r.w, bh, P(12), selected ? soft() : CARD(), "", fonts.body, TEXT(),
                         [e, option]() {
                           const auto *d = custom_logic_entity_sub::entity_detail(e);
                           if (!d) return;
                           custom_logic_entity_sub::select_control_entity(e);
                           onx_controls::request(*d, onx_controls::action(*d, "state"), "state", "option", option);
                         });
    lv_obj_add_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);
    if (selected) {
      lv_obj_set_style_border_width(b, P(2), 0);
      lv_obj_set_style_border_color(b, lv_color_hex(accent()), 0);
      lv_obj_t *check = text(b, LV_SYMBOL_OK, fonts.symbol, accent(), 0, 0);
      lv_obj_align(check, LV_ALIGN_RIGHT_MID, -P(14), 0);
    }
    text(b, option, fonts.body, selected ? accent() : TEXT(), P(16), (bh - line(fonts.body)) / 2, r.w - P(64));
  }
  auto it = std::find(options.begin(), options.end(), current);
  if (it != options.end()) {
    const int top = (it - options.begin()) * (bh + gap);
    if (top + bh > r.h) lv_obj_scroll_to_y(list, top + bh - r.h, LV_ANIM_OFF);
  }
}

inline std::string weekday(const std::string &iso, const std::string &today) {
  if (iso.size() < 10) return iso;
  if (iso.compare(0, 10, today) == 0) return "Today";
  struct tm t{};
  t.tm_year = atoi(iso.substr(0, 4).c_str()) - 1900;
  t.tm_mon = atoi(iso.substr(5, 2).c_str()) - 1;
  t.tm_mday = atoi(iso.substr(8, 2).c_str());
  t.tm_hour = 12;
  mktime(&t);
  char out[8];
  strftime(out, sizeof(out), "%a", &t);
  return out;
}
inline std::string condition(const std::string &s) {
  if (s == "partlycloudy") return "Partly cloudy";
  if (s == "clear-night") return "Clear";
  if (s == "lightning-rainy") return "Storm";
  if (s == "snowy-rainy") return "Sleet";
  return label(s);
}
inline void icon(lv_obj_t *parent, const std::string &e, int cx, int cy, int size, uint32_t color);
__attribute__((noinline)) inline void build_weather(const std::string &e, const EntityDetailState &d, const std::string &title) {
  auto reading_of = [](const std::string &v, const std::string &unit) {
    return v.empty() || v == "None" || v == "unknown" || v == "unavailable" ? std::string("—") : onx_detail_view::compact(v) + unit;
  };
  const int pad = P(16);
  const Rect left = wide() ? Rect{pad, pad, W * 46 / 100 - pad, H - 2 * pad} : Rect{pad, pad, W - 2 * pad, H - 2 * pad};
  // Place name, then the temperature with the conditions beside it; the
  // condition icon sits in the free top-right corner above the conditions.
  const int is = P(46);
  text(root, title, fonts.body, SOFT_TEXT(), left.x, left.y, left.w - is - P(8));
  int y = left.y + line(fonts.body);
  const std::string temp = reading_of(d.target, "°");
  const std::string feels = "Feels " + reading_of(d.apparent_temperature, "°");
  const std::string range =
      d.forecast_count ? "H " + reading_of(d.forecast[0].high, "°") + "  L " + reading_of(d.forecast[0].low, "°") : "";
  // Portrait has room for the large figure; the side column takes the rest.
  const bool hero = !wide() && fonts.hero != nullptr && hero_text(temp);
  const lv_font_t *tf = hero ? fonts.hero : fonts.big;
  const int tw = std::min(text_width(temp, tf), left.w * 55 / 100);
  text(root, temp, tf, TEXT(), left.x - (hero ? P(3) : 0), y, tw + P(4));
  const int sx = left.x + tw + P(14), sw = left.right() - sx;
  const int lines = range.empty() ? 2 : 3;
  int sy = y + (line(tf) - line(fonts.body) - (lines - 1) * line(fonts.small)) / 2;
  {
    // Between the top edge and the conditions, right-aligned.
    const int top = std::max(P(4), left.y - P(8)), size = std::clamp(sy - P(4) - top, P(24), is);
    icon(root, e, left.right() - size / 2, top + size / 2, size, TEXT());
  }
  text(root, condition(d.state), fonts.body, TEXT(), sx, sy, sw);
  sy += line(fonts.body);
  text(root, feels, fonts.small, MUTED(), sx, sy, sw);
  if (!range.empty()) text(root, range, fonts.small, MUTED(), sx, sy + line(fonts.small), sw);
  y += line(tf) + P(6);
  const std::string rain = d.forecast_count ? reading_of(d.forecast[0].rain, "%") : "—";
  const std::pair<std::string, std::string> stats[] = {
      {"Humidity", reading_of(d.humidity, "%")},
      {"Wind", reading_of(d.wind_speed, d.wind_unit.empty() ? "" : " " + d.wind_unit)},
      {"Rain", rain}};
  const int gap = P(8), th = P(64);
  if (wide()) {
    const int rh = P(40);
    for (const auto &s : stats) {
      if (y + rh > left.bottom()) break;
      row(root, {left.x, y, left.w, rh}, s.first, s.second);
      y += rh + P(6);
    }
  } else {
    const int w3 = (left.w - 2 * gap) / 3;
    for (int i = 0; i < 3; ++i)
      tile(root, {left.x + i * (w3 + gap), y, i == 2 ? left.w - 2 * (w3 + gap) : w3, th}, stats[i].first, stats[i].second);
    y += th + P(12);
  }
  const Rect list = wide() ? Rect{W * 46 / 100 + P(12), pad, W - W * 46 / 100 - P(12) - pad, H - 2 * pad}
                           : Rect{pad, y, W - 2 * pad, H - pad - y};
  lv_obj_t *panel = box(root, list.x, list.y, list.w, list.h, CARD(), P(14));
  if (!d.forecast_count) {
    text(panel, d.forecast_waiting(esphome::millis()) ? "Loading forecast…" : "Forecast not available", fonts.small, MUTED(), P(14),
         P(14), list.w - P(28));
    return;
  }
  float lo = NAN, hi = NAN;
  for (int i = 0; i < d.forecast_count; ++i) {
    const float a = num(d.forecast[i].low), b = num(d.forecast[i].high);
    if (std::isfinite(a)) lo = std::isfinite(lo) ? std::min(lo, a) : a;
    if (std::isfinite(b)) hi = std::isfinite(hi) ? std::max(hi, b) : b;
  }
  char today[11] = "";
  const time_t now = std::time(nullptr);
  struct tm local{};
  localtime_r(&now, &local);
  strftime(today, sizeof(today), "%Y-%m-%d", &local);
  // As many days as fit at a comfortable row height.
  const int inner = list.h - P(8), min_row = line(fonts.small) + P(12);
  const int n = std::max(1, std::min({d.forecast_count, 5, inner / min_row})), rh = inner / n;
  // Columns sized to their content: day, condition, low, range bar, high.
  const int ip = P(14);
  int dw = 0, nw = 0, cw = 0;
  for (int i = 0; i < n; ++i) {
    const auto &f = d.forecast[i];
    dw = std::max(dw, text_width(weekday(f.date, today), fonts.small));
    nw = std::max({nw, text_width(reading_of(f.low, "°"), fonts.small), text_width(reading_of(f.high, "°"), fonts.small)});
    cw = std::max(cw, text_width(condition(f.condition), fonts.small));
  }
  dw += P(10);
  nw += P(2);
  const int min_bar = P(44), spare = list.w - 2 * ip - dw - 2 * nw - 2 * P(8) - min_bar;
  cw = wide() ? 0 : std::clamp(cw + P(8), 0, std::max(0, spare));
  if (cw < P(48)) cw = 0;  // Too narrow to read: show only the range.
  const int bx = ip + dw + cw + nw + P(8), bw = list.w - ip - nw - P(8) - bx;
  const int bar = P(6);
  for (int i = 0; i < n; ++i) {
    const auto &f = d.forecast[i];
    const int top = P(4) + i * rh, ry = top + (rh - line(fonts.small)) / 2;
    if (i > 0) box(panel, ip, top, list.w - 2 * ip, 1, CONTROL(), 0);
    text(panel, weekday(f.date, today), fonts.small, TEXT(), ip, ry, dw);
    if (cw) text(panel, condition(f.condition), fonts.small, MUTED(), ip + dw, ry, cw - P(6));
    text(panel, reading_of(f.low, "°"), fonts.small, MUTED(), ip + dw + cw, ry, nw, LV_TEXT_ALIGN_RIGHT);
    text(panel, reading_of(f.high, "°"), fonts.small, TEXT(), list.w - ip - nw, ry, nw, LV_TEXT_ALIGN_RIGHT);
    const int by = top + rh / 2 - bar / 2;
    box(panel, bx, by, bw, bar, CONTROL(), bar / 2);
    const float a = num(f.low), b = num(f.high);
    if (std::isfinite(a) && std::isfinite(b) && hi > lo) {
      const int x0 = int(bw * (a - lo) / (hi - lo)), x1 = int(bw * (b - lo) / (hi - lo));
      box(panel, bx + x0, by, std::max(bar, x1 - x0), bar, accent(), bar / 2);
    }
  }
}


// ------------------------------------------------------------ main pages
// The main carousel pages (one control, two controls, or a control with three
// sensors) are drawn with the same widgets and rebuild rules as the details.
// Cards carry direct controls (toggles, brightness, target, transport); a tap
// elsewhere on a card opens its detail (onx_panel_pages::tap).
inline uint32_t INSET() {return CONTROL();}  // Surfaces inside a card (same as CONTROL()).
// Entity icons are bound by the icon downloader, which keeps one binding per
// widget: a fixed pool of image objects is re-parented on every rebuild
// instead of creating new widgets.
inline std::function<void(lv_obj_t *, const std::string &)> bind_icon;
inline std::function<void(lv_obj_t *)> unbind_icon;
struct IconUse { lv_obj_t *image; std::string entity; int cx, cy, size; const void *src; bool custom, on; lv_obj_t *view{nullptr}; uint64_t artwork_id{0}; };
// Icons arrive at about 107 px and are shown at 20-60 px. Drawing them with
// lv_image_set_scale makes LVGL transform the bitmap on every redraw, so each
// icon is downscaled once into its own small white+alpha bitmap (`view`); the
// pooled image only receives the source from the icon downloader (hidden).
struct ScaledIcon { lv_image_dsc_t dsc{}; uint8_t *pixels{nullptr}; int size{0}; };
inline bool icon_alpha(const lv_image_dsc_t *d, int x, int y, uint8_t &a) {
  const int w = d->header.w, h = d->header.h;
  if (x < 0 || y < 0 || x >= w || y >= h || d->data == nullptr) return false;
  const uint32_t stride = d->header.stride;
  switch (d->header.cf) {
    case LV_COLOR_FORMAT_ARGB8888: a = d->data[y * (stride ? stride : w * 4) + x * 4 + 3]; return true;
    case LV_COLOR_FORMAT_RGB565A8: a = d->data[(stride ? stride : w * 2) * h + y * w + x]; return true;
    case LV_COLOR_FORMAT_A8: a = d->data[y * (stride ? stride : w) + x]; return true;
    default: return false;
  }
}
inline bool scale_icon(lv_obj_t *view, const void *src, int size) {
  if (view == nullptr || src == nullptr || size <= 0) return false;
  const auto *sa = onx_grid_cache::get_scaled_alpha(src, size);
  if (sa == nullptr || sa->alpha == nullptr || sa->w <= 0 || sa->h <= 0) return false;
  auto *s = static_cast<ScaledIcon *>(lv_obj_get_user_data(view));
  if (s == nullptr) {
    s = new (std::nothrow) ScaledIcon();
    if (s == nullptr) return false;
    lv_obj_set_user_data(view, s);
    lv_obj_add_event_cb(view, [](lv_event_t *e) {
      auto *si = static_cast<ScaledIcon *>(lv_obj_get_user_data(static_cast<lv_obj_t *>(lv_event_get_target(e))));
      if (si) { heap_caps_free(si->pixels); delete si; }
    }, LV_EVENT_DELETE, nullptr);
  }
  const int ow = sa->w, oh = sa->h;
  if (s->pixels == nullptr || s->size < ow * oh) {
    heap_caps_free(s->pixels);
    s->pixels = static_cast<uint8_t *>(heap_caps_malloc(size_t(ow) * oh * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    s->size = s->pixels ? ow * oh : 0;
    if (s->pixels == nullptr) return false;
  }
  const size_t total = size_t(ow) * oh;
  for (size_t i = 0; i < total; ++i) {
    uint8_t *px = s->pixels + i * 4;
    px[0] = px[1] = px[2] = 255;
    px[3] = sa->alpha[i];
  }
  s->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  s->dsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
  s->dsc.header.w = ow;
  s->dsc.header.h = oh;
  s->dsc.header.stride = ow * 4;
  s->dsc.data = s->pixels;
  s->dsc.data_size = total * 4;
  lv_image_cache_drop(&s->dsc);
  lv_image_set_src(view, &s->dsc);
  lv_image_set_pivot(view, ow / 2, oh / 2);
  lv_image_set_scale(view, LV_SCALE_NONE);
  lv_obj_invalidate(view);
  return true;
}
inline std::array<lv_obj_t *, 16> icon_pool{};
inline uint64_t card_artwork=0;
inline bool card_on=false;
inline std::string animated_key;
inline uint32_t animated_at=0;
inline void animate_key(const std::string &entity) {
  animated_key=entity;
  animated_at=esphome::millis();
}
inline unsigned animation_elapsed(const std::string &entity) {
  return entity==animated_key?esphome::millis()-animated_at:600;
}
inline std::vector<IconUse> icon_uses;
inline lv_obj_t *main_artwork = nullptr;
inline std::function<const void *(const std::string &)> artwork_source;
inline void detach_artwork() {
  if (!main_artwork) return;
  lv_image_set_src(main_artwork, static_cast<const void *>(nullptr));
  lv_obj_set_parent(main_artwork, lv_layer_top());
  lv_obj_add_flag(main_artwork, LV_OBJ_FLAG_HIDDEN);
}
inline void detach_icons() {
  for (auto *img : icon_pool) {
    if (img == nullptr) continue;
    lv_anim_delete(img, nullptr);  // Stop any key animation and restore the image.
    lv_obj_set_style_opa(img, LV_OPA_COVER, 0);
    lv_image_set_rotation(img, 0);
    if (lv_obj_get_parent(img) != lv_layer_top()) lv_obj_set_parent(img, lv_layer_top());
    lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
  }
  icon_uses.clear();
}
inline void place_icon(IconUse &u) {
  const void *src = lv_image_get_src(u.image);
  u.src = src;
  lv_image_header_t h{};
  if (src == nullptr || lv_image_decoder_get_info(src, &h) != LV_RESULT_OK || h.w == 0 || h.h == 0) return;
  if (u.view != nullptr) {
    if (scale_icon(u.view, src, u.size)) {
      lv_obj_add_flag(u.image, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(u.view, LV_OBJ_FLAG_HIDDEN);
      const auto *sd = &static_cast<ScaledIcon *>(lv_obj_get_user_data(u.view))->dsc;
      lv_obj_set_pos(u.view, u.cx - int(sd->header.w) / 2, u.cy - int(sd->header.h) / 2);
      return;
    }
    lv_obj_add_flag(u.view, LV_OBJ_FLAG_HIDDEN);  // Unsupported format: scale as before.
    lv_obj_remove_flag(u.image, LV_OBJ_FLAG_HIDDEN);
  }
  lv_image_set_pivot(u.image, h.w / 2, h.h / 2);
  lv_image_set_scale(u.image, std::max<int>(16, u.size * 256 / std::max<int>(h.w, h.h)));
  lv_obj_set_pos(u.image, u.cx - int(h.w) / 2, u.cy - int(h.h) / 2);
}
// The widget animations and effects act on: the scaled copy when there is one.
inline lv_obj_t *shown_icon(const IconUse &u) {
  return u.view != nullptr && !lv_obj_has_flag(u.view, LV_OBJ_FLAG_HIDDEN) ? u.view : u.image;
}
inline void icon(lv_obj_t *parent, const std::string &e, int cx, int cy, int size, uint32_t color) {
  if (!bind_icon || icon_uses.size() >= icon_pool.size()) return;
  const auto index=icon_uses.size();
  auto *&img = icon_pool[index];
  if (img == nullptr) {
    img = lv_image_create(lv_layer_top());
    lv_obj_remove_flag(img, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  }
  lv_obj_set_parent(img, parent);
  lv_obj_remove_flag(img, LV_OBJ_FLAG_HIDDEN);
  bool custom=false;
  if(card_artwork) {
    if (auto *art = onx_grid_cache::get_artwork(card_artwork)) {
      custom = true;
      lv_image_set_src(img, art->frame(card_on, animation_elapsed(e)));
    }
  }
  if(!custom) {
    bind_icon(img, e);
    lv_obj_set_style_image_recolor(img, lv_color_hex(color), 0);
  }
  lv_obj_set_style_image_recolor_opa(img, custom?LV_OPA_TRANSP:LV_OPA_COVER, 0);
  lv_obj_t *view = nullptr;
  if (!custom) {
    view = lv_image_create(parent);
    lv_obj_remove_flag(view, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_add_flag(view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_image_recolor(view, lv_color_hex(color), 0);
    lv_obj_set_style_image_recolor_opa(view, LV_OPA_COVER, 0);
  }
  icon_uses.push_back({img, e, cx, cy, size, nullptr, custom, card_on, view, custom ? card_artwork : 0});
  place_icon(icon_uses.back());
}
// Forward declaration for grid-cache single-tile refresh when async icons arrive.
inline void refresh_grid_icons();
// Grid-key icon animations (onx_panel_pages::Effect), run by LVGL on the icon
// image alone so only that area redraws.
inline void animate_icon(lv_obj_t *img, uint8_t effect) {
  if (img == nullptr || !effect) return;
  lv_anim_delete(img, nullptr);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, img);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  if (effect == 3) {  // Spin: one turn every 1.6 s.
    lv_anim_set_exec_cb(&a, [](void *o, int32_t v) { lv_image_set_rotation(static_cast<lv_obj_t *>(o), v); });
    lv_anim_set_values(&a, 0, 3600);
    lv_anim_set_duration(&a, 1600);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
  } else if (effect == 2) {  // Blink: fade out and back twice a second.
    lv_anim_set_exec_cb(&a, [](void *o, int32_t v) { lv_obj_set_style_opa(static_cast<lv_obj_t *>(o), v, 0); });
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_20);
    lv_anim_set_duration(&a, 250);
    lv_anim_set_reverse_duration(&a, 250);
    lv_anim_set_repeat_delay(&a, 250);
    lv_anim_set_path_cb(&a, lv_anim_path_step);
  } else {  // Pulse: grow by 15 % and back.
    const int32_t base = lv_image_get_scale(img);
    lv_anim_set_exec_cb(&a, [](void *o, int32_t v) { lv_image_set_scale(static_cast<lv_obj_t *>(o), v); });
    lv_anim_set_values(&a, base, base * 115 / 100);
    lv_anim_set_duration(&a, 700);
    lv_anim_set_reverse_duration(&a, 700);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  }
  lv_anim_start(&a);
}
// Re-binds each frame so downloaded icons replace the fallbacks.
inline void refresh_icons() {
  if (grid_cached_mode) { refresh_grid_icons(); return; }
  if (!bind_icon) return;
  for (size_t i=0;i<icon_uses.size();++i) {
    auto &u=icon_uses[i];
    if(u.custom) {
      if (auto *art = onx_grid_cache::get_artwork(u.artwork_id)) {
        const auto *next = art->frame(u.on, animation_elapsed(u.entity));
        if (next != u.src) { lv_image_set_src(u.image, next); place_icon(u); }
      }
      continue;
    }
    bind_icon(u.image, u.entity);
    if (lv_image_get_src(u.image) != u.src) place_icon(u);
  }
}
// Round icon chip, tinted while the entity is active.
inline lv_obj_t *chip_icon(lv_obj_t *parent, const std::string &e, int x, int y, int size, bool on) {
  lv_obj_t *chip = box(parent, x, y, size, size, on ? soft() : INSET(), size / 2);
  icon(parent, e, x + size / 2, y + size / 2, size * 56 / 100, on ? accent() : SOFT_TEXT());
  return chip;
}
// In-place updates of a main-page card (see render_main). `card_patch` is set
// by the card builder; it receives the new tile and returns false when the
// card has to be rebuilt.
using CardPatch = std::function<bool(const struct Tile &)>;
inline CardPatch card_patch;
inline std::vector<CardPatch> card_patches;
inline void patch_chip(lv_obj_t *chip, lv_obj_t *icon_view, bool on) {
  if (chip) lv_obj_set_style_bg_color(chip, lv_color_hex(on ? soft() : INSET()), 0);
  if (icon_view) lv_obj_set_style_image_recolor(icon_view, lv_color_hex(on ? accent() : SOFT_TEXT()), 0);
}
inline void patch_toggle(lv_obj_t *b, bool on) {
  if (b == nullptr) return;
  const int w = P(54), h = P(30), k = h - P(8);
  lv_obj_set_style_bg_color(b, lv_color_hex(on ? accent() : TRACK()), 0);
  pressed_look(b, LV_STATE_PRESSED);
  if (lv_obj_t *knob = lv_obj_get_child(b, 0)) {
    lv_obj_set_x(knob, on ? w - k - P(4) : P(4));
    lv_obj_set_style_bg_color(knob, lv_color_hex(on ? 0x101418 : SOFT_TEXT()), 0);
  }
}
// Pill switch.
inline lv_obj_t *toggle(lv_obj_t *parent, int x, int y, bool on, std::function<void()> fn) {
  const int w = P(54), h = P(30), k = h - P(8);
  lv_obj_t *b = button(parent, x, y, w, h, h / 2, on ? accent() : TRACK(), "", fonts.small, TEXT(), std::move(fn));
  box(b, on ? w - k - P(4) : P(4), P(4), k, k, on ? 0x101418 : SOFT_TEXT(), k / 2);
  return b;
}
inline std::string compass(const std::string &bearing) {
  const float b = num(bearing);
  if (!std::isfinite(b)) return "";
  static const char *names[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  return names[int(std::lround(std::fmod(std::fmod(b, 360.0f) + 360.0f, 360.0f) / 45.0f)) % 8];
}
inline std::string present(const std::string &v, const std::string &unit) {
  if (v.empty() || v == "None" || v == "unknown" || v == "unavailable") return "";
  return onx_detail_view::compact(v) + (unit.empty() ? "" : (unit == "%" || unit == "°" ? unit : " " + unit));
}
// Name and context to the right of a chip; returns the bottom.
inline int titled(lv_obj_t *c, const Tile &t, int x, int y, int w, uint32_t context_color = MUTED()) {
  const lv_font_t *f = fit(t.name, w, {fonts.body, fonts.small});
  text(c, t.name, f, TEXT(), x, y, w);
  int bottom = y + line(f);
  if (!t.context.empty()) {
    text(c, t.context, fonts.small, context_color, x, bottom, w);
    bottom += line(fonts.small);
  }
  return bottom;
}

inline void card_weather(lv_obj_t *c, const Tile &t, const EntityDetailState &d) {
  const Rect r = t.r;
  // The 2.8" panel trades the hero temperature for more readings: a smaller
  // reading and icon, shorter tiles, then the forecast in the space left.
  const bool compact = std::min(W, H) < 320;
  const int pad = compact ? P(14) : P(16), is = compact ? std::min(P(64), r.h / 4) : std::min(P(84), r.h / 3);
  // Wide cards (landscape): the reading on the left half, the tiles on the right.
  const bool split = r.w >= r.h * 3 / 2 && r.w >= P(360);
  const int lw = split ? r.w * 46 / 100 : r.w;
  text(c, t.name, fonts.small, SOFT_TEXT(), pad, pad, lw - 2 * pad - is);
  int y = pad + line(fonts.small);
  const std::string temp = present(d.target, "°").empty() ? "—" : present(d.target, "°");
  const bool hero = !compact && r.h >= P(240) && hero_text(temp);
  const lv_font_t *tf = hero ? fonts.hero : fonts.big;
  text(c, temp, tf, TEXT(), pad - (hero ? P(3) : 0), y - (hero ? P(6) : 0), lw - 2 * pad - is);
  y += line(tf) - (hero ? P(14) : P(2));
  text(c, condition(d.state), fonts.body, TEXT(), pad, y, lw - 2 * pad - (split ? 0 : is));
  y += line(fonts.body) + (compact ? P(10) : P(14));
  if (split) {  // The icon under the reading, using the left column's height.
    const int iy = y, ih = r.h - pad - iy, s2 = std::min({P(96), ih, lw - 2 * pad});
    if (s2 >= P(40)) icon(c, t.entity, pad + s2 / 2, iy + ih / 2, s2, TEXT());
    else icon(c, t.entity, lw - pad - is / 2, pad + is / 2 + P(8), is, TEXT());
  } else icon(c, t.entity, r.w - pad - is / 2, pad + is / 2 + P(8), is, TEXT());
  std::vector<std::pair<std::string, std::string>> stats;
  const std::string bearing = compass(d.wind_bearing);
  auto add = [&](const std::string &name, const std::string &value) { if (!value.empty()) stats.push_back({name, value}); };
  add("Humidity", present(d.humidity, "%"));
  add(bearing.empty() ? "Wind" : "Wind · " + bearing, present(d.wind_speed, d.wind_unit));
  add("Pressure", present(d.pressure, d.pressure_unit));
  add("Visibility", present(d.visibility, d.visibility_unit));
  add("UV index", present(d.uv_index, ""));
  add("Feels like", present(d.apparent_temperature, "°"));
  const int gap = compact ? P(6) : P(8), cols = 2, gx = split ? lw : pad, gw = split ? r.w - lw - pad : r.w - 2 * pad;
  if (split) y = pad;
  const int tw = (gw - gap) / cols, need = (int(stats.size()) + 1) / 2;
  // Split cards share their height between the rows (up to 3).
  const int th = split && need ? std::clamp((r.h - 2 * pad - (need - 1) * gap) / need, P(48), P(72)) : compact ? P(50) : P(56);
  const int rows = std::min<int>(need, (r.h - pad - y + gap) / (th + gap));
  for (int i = 0; i < std::min<int>(stats.size(), rows * cols); ++i)
    tile(c, {gx + (i % cols) * (tw + gap), y + (i / cols) * (th + gap), tw, th}, stats[i].first, stats[i].second, INSET());
  // Short cards: the first readings on one line instead of tiles.
  const bool summary_line = rows == 0 && !stats.empty() && r.h - pad - y + P(14) >= line(fonts.small);
  if (summary_line) {
    std::string summary;
    for (size_t i = 0; i < std::min<size_t>(stats.size(), 2); ++i)
      summary += (i ? "  ·  " : "") + stats[i].first + " " + stats[i].second;
    text(c, summary, fonts.small, MUTED(), pad, y - P(8), r.w - 2 * pad);
  }
  // Compact cards: the next days in the space below the readings.
  if (compact && !split && d.forecast_count) {
    char today[11] = "";
    const time_t now = std::time(nullptr);
    struct tm local{};
    localtime_r(&now, &local);
    strftime(today, sizeof(today), "%Y-%m-%d", &local);
    // Below the tiles, or below the summary line when it replaced them.
    int fy = summary_line ? y - P(8) + line(fonts.small) + P(4) : y + rows * (th + gap) + (rows ? P(2) : 0);
    const int rh = line(fonts.small) + P(4), dw = r.w * 22 / 100, vw = r.w * 30 / 100;
    for (int i = 0; i < d.forecast_count && fy + rh <= r.h - pad + P(4); ++i, fy += rh) {
      const auto &f = d.forecast[i];
      text(c, weekday(f.date, today), fonts.small, TEXT(), pad, fy, dw);
      text(c, condition(f.condition), fonts.small, MUTED(), pad + dw, fy, r.w - 2 * pad - dw - vw);
      auto deg = [](const std::string &v) { const auto p = present(v, "°"); return p.empty() ? std::string("—") : p; };
      text(c, deg(f.low) + " / " + deg(f.high), fonts.small, TEXT(), r.w - pad - vw, fy, vw,
           LV_TEXT_ALIGN_RIGHT);
    }
  }
}

inline void card_light(lv_obj_t *c, const Tile &t, const EntityDetailState &d) {
  const Rect r = t.r;
  const auto s = light_state ? light_state(t.entity) : esphome::onx_storage::NormalLightState{};
  const bool on = d.available && shown_state(t.entity, s.is_on ? "on" : "off") == "on", dimmable = s.supports_brightness();
  float level = on ? s.brightness_pct : 0;
  const float want = pending_value(t.entity, "brightness");
  clear_pending_if(t.entity, "brightness", level);
  if (std::isfinite(want)) level = want;
  Tile head = t;
  head.context = !d.available ? "Unavailable" : on ? "On" : "Off";
  if (on && s.color_temperature_kelvin > 0 && s.supports_color_temperature())
    head.context += " · " + std::to_string(s.color_temperature_kelvin) + " K";
  const int pad = P(14), cs = P(44), tx = pad + cs + P(12);
  lv_obj_t *chip = chip_icon(c, t.entity, pad, pad, cs, on);
  lv_obj_t *chip_view = !icon_uses.empty() ? icon_uses.back().view : nullptr;
  const int before = lv_obj_get_child_count(c);
  titled(c, head, tx, pad + P(2), r.w - tx - pad - P(62));
  lv_obj_t *context = !head.context.empty() && int(lv_obj_get_child_count(c)) > before ? lv_obj_get_child(c, -1) : nullptr;
  const std::string e = t.entity;
  lv_obj_t *sw_toggle = nullptr;
  if (d.available) sw_toggle = toggle(c, r.w - pad - P(54), pad + P(7), on, [e]() { light_toggle(e); });
  const int sh = P(40), sy = r.h - pad - sh;
  const int vy = pad + cs + P(8);
  // Compact card (the 2.8" split page): no room for the large value, so the
  // status line carries the brightness while the light is on ("On · 60%").
  const bool compact = dimmable && sy - vy < line(fonts.big);
  auto status = [](bool now_on, const esphome::onx_storage::NormalLightState &st, float lv, bool available) {
    if (!available) return std::string("Unavailable");
    std::string out = now_on ? "On" : "Off";
    if (now_on && lv > 0) out += " · " + pct(lv);
    return out;
  };
  if (compact && context) onx_render::label(context, status(on, s, level, d.available).c_str());
  lv_obj_t *value = nullptr, *fill = nullptr, *knob = nullptr;
  const int sw = r.w - 2 * pad;
  if (!dimmable) {
    if (sy - vy >= line(fonts.big)) value = text(c, on ? "On" : "Off", fonts.big, TEXT(), pad, vy, r.w - 2 * pad);
  } else {
    if (sy - vy >= line(fonts.big)) value = text(c, pct(level), fonts.big, TEXT(), pad, vy, r.w - 2 * pad);
  }
  if (!dimmable) {
    card_patch = [e, chip, chip_view, context, sw_toggle, value](const Tile &) {
      const bool now_on = light_shown_on(e);
      patch_chip(chip, chip_view, now_on);
      patch_toggle(sw_toggle, now_on);
      if (context) onx_render::label(context, now_on ? "On" : "Off");
      if (value) onx_render::label(value, now_on ? "On" : "Off");
      return true;
    };
    return;
  }
  lv_obj_t *track = box(c, pad, sy, sw, sh, INSET(), P(12));
  lv_obj_set_style_clip_corner(track, true, 0);
  const int fw = int(sw * std::clamp(level, 0.0f, 100.0f) / 100);
  fill = box(track, 0, 0, fw, sh, light_fill_color(t.entity, d, s), 0);  // The light's colour, as the detail slider.
  knob = box(track, std::clamp(fw - P(9), P(4), sw - P(8)), P(9), P(4), sh - P(18), 0x101418, P(2));
  card_patch = [e, chip, chip_view, context, sw_toggle, value, fill, knob, sw, sh, compact, status](const Tile &) {
    if (dragging || !light_state) return true;  // The finger owns the slider.
    const auto st = light_state(e);
    const bool now_on = light_shown_on(e);
    float lv = now_on ? st.brightness_pct : 0;
    const float want = pending_value(e, "brightness");
    clear_pending_if(e, "brightness", lv);
    if (std::isfinite(want)) lv = want;
    patch_chip(chip, chip_view, now_on);
    patch_toggle(sw_toggle, now_on);
    if (context && compact) onx_render::label(context, status(now_on, st, lv, true).c_str());
    else if (context) {
      std::string ctx = now_on ? "On" : "Off";
      if (now_on && st.color_temperature_kelvin > 0 && st.supports_color_temperature())
        ctx += " · " + std::to_string(st.color_temperature_kelvin) + " K";
      onx_render::label(context, ctx.c_str());
    }
    if (value) onx_render::label(value, pct(lv).c_str());
    const int w = int(sw * std::clamp(lv, 0.0f, 100.0f) / 100);
    lv_obj_set_size(fill, w, sh);
    if (const auto *dd = custom_logic_entity_sub::entity_detail(e)) lv_obj_set_style_bg_color(fill, lv_color_hex(light_fill_color(e, *dd, st)), 0);
    lv_obj_set_x(knob, std::clamp(w - P(9), P(4), sw - P(8)));
    return true;
  };
  if (!d.available) return;
  drag(track, [e, track, fill, knob, value, context, compact, sw, sh](lv_point_t p, bool commit) {
    lv_area_t a;
    lv_obj_get_coords(track, &a);
    const int v = std::clamp(int(std::lround((p.x - a.x1) * 100.0f / sw)), 1, 100);
    const int w = sw * v / 100;
    lv_obj_set_size(fill, w, sh);
    lv_obj_set_x(knob, std::clamp(w - P(9), P(4), sw - P(8)));
    if (value) lv_label_set_text(value, pct(v).c_str());
    if (compact && context) lv_label_set_text(context, ("On · " + pct(v)).c_str());
    if (!commit) return;
    set_pending(e, "brightness", v);
    request(e, "light.turn_on", "brightness_pct", std::to_string(v), "panel_brightness");
  });
}

inline void card_climate(lv_obj_t *c, const Tile &t, const EntityDetailState &d) {
  using onx_controls::effective;
  const Rect r = t.r;
  const auto mode = effective(d, "state");
  const bool off = mode == "off";
  float target = num(effective(d, "temperature"));
  const float want = pending_value(t.entity, "temperature");
  clear_pending_if(t.entity, "temperature", target);
  if (std::isfinite(want)) target = want;
  const bool range = !std::isfinite(target) && std::isfinite(num(d.low)) && std::isfinite(num(d.high));
  Tile head = t;
  head.context = hvac_label(mode);
  if (std::isfinite(num(d.current))) head.context += " · now " + fmt(num(d.current)) + "°";
  const int pad = P(14), cs = P(44), tx = pad + cs + P(12);
  chip_icon(c, t.entity, pad, pad, cs, !off);
  titled(c, head, tx, pad + P(2), r.w - tx - pad, off ? MUTED() : accent());
  const std::string value = range ? fmt(num(effective(d, "target_temp_low"))) + "–" + fmt(num(effective(d, "target_temp_high"))) + "°"
                                  : std::isfinite(target) ? fmt(target) + "°" : "—";
  const int bs = P(52), by = r.h - pad - bs;
  if (by < pad + cs) return;
  const bool big = r.h >= P(260) && hero_text(value);
  const lv_font_t *vf = big ? fonts.hero : fonts.big;
  const int cy = big ? (pad + cs + by + bs) / 2 : by + bs / 2;
  // Short cards (two controls on the 2.8-inch panel) leave no room for the
  // label between the header and the value; skip it rather than overlap.
  const int label_y = cy - line(vf) / 2 - line(fonts.small) + P(6);
  if (label_y >= pad + cs) text(c, "Target", fonts.small, MUTED(), 0, label_y, r.w, LV_TEXT_ALIGN_CENTER);
  text(c, value, vf, TEXT(), 0, cy - line(vf) / 2 + P(4), r.w, LV_TEXT_ALIGN_CENTER);
  // Single targets (feature 1) and low/high ranges (feature 2) both step.
  if (!d.available || !(d.features & 3)) return;
  const std::string e = t.entity;
  auto stepper = [e, range](int direction) {
    custom_logic_entity_sub::select_control_entity(e);
    custom_logic_entity_sub::control_setting = range ? "range" : "temperature";
    onx_controls::rotate(e, direction);
  };
  circle(c, pad + bs / 2, by + bs / 2, bs, INSET(), LV_SYMBOL_MINUS, TEXT(), [stepper]() { stepper(-1); });
  circle(c, r.w - pad - bs / 2, by + bs / 2, bs, INSET(), LV_SYMBOL_PLUS, TEXT(), [stepper]() { stepper(1); });
}

// Lyrics backdrop: the artwork reduced to 32x32, box-blurred three times (close
// to a Gaussian), then enlarged with bilinear filtering and Bayer dithering
// into a panel-sized RGB565 buffer in PSRAM, darkened for contrast. Built once
// per track/layout; drawing it is a plain copy, so line animations stay cheap.
struct LyricBackdrop {
  lv_image_dsc_t dsc{};
  uint16_t *px{nullptr};
  int w{0}, h{0};
  uint32_t key{0};  // Content fingerprint: the artwork buffer is reused for every cover.
};
inline LyricBackdrop &lyric_backdrop() { static LyricBackdrop b; return b; }
inline bool pixel_rgb(const lv_image_dsc_t *d, int x, int y, int &r, int &g, int &b) {
  const int w = d->header.w, bpp = lv_color_format_get_size(static_cast<lv_color_format_t>(d->header.cf));
  const int stride = d->header.stride ? d->header.stride : w * bpp;
  const uint8_t *row = d->data + y * stride;
  switch (d->header.cf) {
    case LV_COLOR_FORMAT_RGB565:
    case LV_COLOR_FORMAT_RGB565A8: {
      const uint16_t c = row[x * 2] | (row[x * 2 + 1] << 8);
      r = (c >> 11) << 3; g = ((c >> 5) & 63) << 2; b = (c & 31) << 3; return true;
    }
    case LV_COLOR_FORMAT_RGB888: b = row[x * 3]; g = row[x * 3 + 1]; r = row[x * 3 + 2]; return true;
    case LV_COLOR_FORMAT_ARGB8888:
    case LV_COLOR_FORMAT_XRGB8888: b = row[x * 4]; g = row[x * 4 + 1]; r = row[x * 4 + 2]; return true;
    default: return false;
  }
}
inline const lv_image_dsc_t *make_backdrop(const void *source, int out_w, int out_h) {
  constexpr int N = 32;
  const auto *d = static_cast<const lv_image_dsc_t *>(source);
  if (d == nullptr || d->data == nullptr || d->header.w < N || d->header.h < N || out_w < 8 || out_h < 8) return nullptr;
  auto &bd = lyric_backdrop();
  // FNV-1a over ~1000 bytes spread through the image, plus its size.
  uint32_t key = 2166136261u ^ d->header.w ^ (uint32_t(d->header.h) << 16);
  const uint32_t step = std::max<uint32_t>(1, d->data_size / 1024);
  for (uint32_t i = 0; i < d->data_size; i += step) key = (key ^ d->data[i]) * 16777619u;
  if (bd.px && bd.key == key && bd.w == out_w && bd.h == out_h) return &bd.dsc;
  int pr, pg, pb;
  if (!pixel_rgb(d, 0, 0, pr, pg, pb)) return nullptr;
  // 1. Cell averages (every second pixel).
  static float c[3][N * N], t[N * N];
  const int sw = d->header.w, sh = d->header.h;
  for (int cy = 0; cy < N; ++cy)
    for (int cx = 0; cx < N; ++cx) {
      int r = 0, g = 0, b = 0, n = 0;
      for (int y = cy * sh / N; y < (cy + 1) * sh / N; y += 2)
        for (int x = cx * sw / N; x < (cx + 1) * sw / N; x += 2) { pixel_rgb(d, x, y, pr, pg, pb); r += pr; g += pg; b += pb; ++n; }
      n = std::max(1, n);
      c[0][cy * N + cx] = r / float(n); c[1][cy * N + cx] = g / float(n); c[2][cy * N + cx] = b / float(n);
    }
  // 2. Three separable box blurs of radius 3, clamped at the edges.
  constexpr int R = 3;
  for (auto &ch : c)
    for (int pass = 0; pass < 3; ++pass) {
      for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
          float sum = 0;
          for (int k = -R; k <= R; ++k) sum += ch[y * N + std::clamp(x + k, 0, N - 1)];
          t[y * N + x] = sum / (2 * R + 1);
        }
      for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
          float sum = 0;
          for (int k = -R; k <= R; ++k) sum += t[std::clamp(y + k, 0, N - 1) * N + x];
          ch[y * N + x] = sum / (2 * R + 1);
        }
    }
  // 3. Bilinear enlargement, darkened, dithered to RGB565.
  if (bd.px == nullptr || bd.w * bd.h < out_w * out_h) {
    if (bd.px) heap_caps_free(bd.px);
    bd.px = static_cast<uint16_t *>(heap_caps_malloc(size_t(out_w) * out_h * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (bd.px == nullptr) { bd.w = bd.h = 0; return nullptr; }
  }
  bd.w = out_w; bd.h = out_h; bd.key = key;
  static const uint8_t bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
  constexpr float DIM = 0.62f;
  for (int y = 0; y < out_h; ++y) {
    const float fy = std::clamp((y + 0.5f) * N / out_h - 0.5f, 0.0f, N - 1.001f);
    const int y0 = int(fy); const float wy = fy - y0;
    for (int x = 0; x < out_w; ++x) {
      const float fx = std::clamp((x + 0.5f) * N / out_w - 0.5f, 0.0f, N - 1.001f);
      const int x0 = int(fx); const float wx = fx - x0;
      const int i = y0 * N + x0;
      const float dither = (bayer[(y & 3) * 4 + (x & 3)] + 0.5f) / 16.0f;
      int out[3];
      for (int k = 0; k < 3; ++k) {
        const float v = ((c[k][i] * (1 - wx) + c[k][i + 1] * wx) * (1 - wy) + (c[k][i + N] * (1 - wx) + c[k][i + N + 1] * wx) * wy) * DIM;
        const int levels = k == 1 ? 4 : 8;  // 6-bit green, 5-bit red/blue.
        out[k] = std::clamp(int(v / levels + dither), 0, 255 / levels);
      }
      bd.px[y * out_w + x] = uint16_t(out[0] << 11 | out[1] << 5 | out[2]);
    }
  }
  bd.dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  bd.dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  bd.dsc.header.w = out_w; bd.dsc.header.h = out_h;
  bd.dsc.header.stride = out_w * 2;
  bd.dsc.data_size = uint32_t(out_w) * out_h * 2;
  bd.dsc.data = reinterpret_cast<const uint8_t *>(bd.px);
  lv_image_cache_drop(&bd.dsc);
  return &bd.dsc;
}

// Synced lyrics in the media detail's artwork space: the current line in full
// (wrapped, centred), with the previous and next lines above and below when
// they fit whole. Re-laid out only when the line changes.
struct LyricView { std::string entity; lv_obj_t *prev{nullptr}, *cur{nullptr}, *next{nullptr}; int w{0}, h{0}, shown{-2}; };
inline LyricView lyric_view;
// Sets a lyric line: text, font, and a size that hugs the wrapped text plus a
// translucent black chip behind it (readable over light covers). At most `rows`
// lines, the last ending in "…" when cut. Returns the chip height.
inline int lyric_label(lv_obj_t *l, const std::string &s, const lv_font_t *f, int w, int rows) {
  const int px = P(8), py = P(3), tw = w - 2 * px;
  lv_obj_set_style_text_font(l, f, 0);
  lv_label_set_text(l, s.c_str());
  const int n = s.empty() || rows < 1 ? 0 : lines_for(s, f, tw, rows);
  if (n == 0) { lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN); return 0; }
  lv_point_t size;
  lv_text_get_size(&size, s.c_str(), f, 0, 0, tw, LV_TEXT_FLAG_NONE);
  const int cw = std::min(tw, int(size.x) + 2) + 2 * px, ch = n * line(f) + 2 * py;
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_size(l, cw, ch);
  lv_obj_set_x(l, (w - cw) / 2);
  lv_obj_remove_flag(l, LV_OBJ_FLAG_HIDDEN);
  return ch;
}
inline void lyric_anim_y(void *obj, int32_t v) { lv_obj_set_style_translate_y(static_cast<lv_obj_t *>(obj), v, 0); }
inline void lyric_anim_opa(void *obj, int32_t v) { lv_obj_set_style_opa(static_cast<lv_obj_t *>(obj), lv_opa_t(v), 0); }
inline void tick_lyrics(float seconds, bool visible) {
  auto &v = lyric_view;
  if (v.cur == nullptr) return;
  const int i = visible ? onx_lyrics::index_at(seconds) : -1;
  if (i == v.shown) return;
  // The first layout of a screen is drawn in place; later line changes slide up and fade in.
  const bool animate = v.shown != -2 && i == v.shown + 1;
  v.shown = i;
  std::string cur = onx_lyrics::line(i);
  if (cur.empty()) cur = i < 0 ? "" : "\xC2\xB7 \xC2\xB7 \xC2\xB7";  // Instrumental gap.
  const int gap = P(6), sl = line(fonts.small) + 2 * P(3);
  // The current line is always whole, in the largest font that keeps it to
  // two rows (any rows for the smallest) and leaves a row for each neighbour.
  const int tw = v.w - 2 * P(8);
  auto rows_h = [&](const lv_font_t *f) { return lines_for(cur, f, tw, 24) * line(f) + 2 * P(3); };
  const lv_font_t *cf = fonts.small;
  for (const lv_font_t *f : {fonts.title, fonts.body})
    if (lines_for(cur, f, tw, 24) <= 2 && rows_h(f) <= v.h - 2 * (gap + sl)) { cf = f; break; }
  if (rows_h(cf) > v.h - 2 * (gap + sl) && rows_h(fonts.body) <= v.h) cf = fonts.body;
  const int ch = std::min(v.h, lyric_label(v.cur, cur, cf, v.w, 24)), cy = (v.h - ch) / 2;
  lv_obj_set_y(v.cur, cy);
  // Neighbours in the small font, cut to the rows left above and below.
  const int rows = ((v.h - ch) / 2 - gap - 2 * P(3)) / line(fonts.small);
  const int ph = lyric_label(v.prev, onx_lyrics::line(i - 1), fonts.small, v.w, rows);
  lv_obj_set_y(v.prev, cy - gap - ph);
  lyric_label(v.next, onx_lyrics::line(i + 1), fonts.small, v.w, rows);
  lv_obj_set_y(v.next, cy + ch + gap);
  if (!animate) return;
  for (lv_obj_t *o : {v.prev, v.cur, v.next}) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, lyric_anim_y);
    lv_anim_set_values(&a, P(18), 0);
    lv_anim_set_duration(&a, 280);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
  }
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, v.cur);
  lv_anim_set_exec_cb(&a, lyric_anim_opa);
  lv_anim_set_values(&a, LV_OPA_20, LV_OPA_COVER);
  lv_anim_set_duration(&a, 280);
  lv_anim_start(&a);
}

// Live media position on main cards; moved every refresh without a rebuild.
struct MediaProgress { std::string entity; lv_obj_t *group{nullptr}, *fill{nullptr}, *elapsed{nullptr}, *total{nullptr}; int w{0}, h{0}; bool remaining{true}; };
inline std::vector<MediaProgress> media_progress;
inline void tick_media_progress() {
  for (auto &m : media_progress) {
    const auto *d = custom_logic_entity_sub::entity_detail(m.entity);
    if (d == nullptr || m.group == nullptr) continue;
    auto projection = d->progress;  // A copy: projecting must not move the detail's anchors.
    const auto v = projection.view(esphome::millis(), double(std::time(nullptr)), d->available);
    if (!v.visible) { lv_obj_add_flag(m.group, LV_OBJ_FLAG_HIDDEN); continue; }
    lv_obj_remove_flag(m.group, LV_OBJ_FLAG_HIDDEN);
    const int fw = std::max(m.h, m.w * v.permille / 1000);
    if (lv_obj_get_style_width(m.fill, LV_PART_MAIN) != fw) lv_obj_set_width(m.fill, fw);
    const std::string el = onx_panel_ui::duration_text(v.seconds),
                      to = m.remaining ? "-" + onx_panel_ui::duration_text(v.duration - v.seconds) : onx_panel_ui::duration_text(v.duration);
    if (el != lv_label_get_text(m.elapsed)) lv_label_set_text(m.elapsed, el.c_str());
    if (to != lv_label_get_text(m.total)) lv_label_set_text(m.total, to.c_str());
  }
  if (lyric_view.cur != nullptr) {
    const auto *d = custom_logic_entity_sub::entity_detail(lyric_view.entity);
    if (d != nullptr) {
      auto projection = d->progress;
      const auto v = projection.view(esphome::millis(), double(std::time(nullptr)), d->available);
      tick_lyrics(v.seconds, v.visible);
    }
  }
}

// Round transport: dark previous/next circles around a larger accent play.
inline void transport(lv_obj_t *parent, int cx, int cy, int side, int main, int gap, const std::string &e, bool playing) {
  circle(parent, cx - main / 2 - gap - side / 2, cy, side, INSET(), LV_SYMBOL_PREV, TEXT(),
         [e]() { request(e, "media_player.media_previous_track", "", "", "panel_media"); });
  circle(parent, cx, cy, main, accent(), playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY, TEXT(),
         [e, playing]() { request(e, playing ? "media_player.media_pause" : "media_player.media_play", "", playing ? "paused" : "playing", "state"); });
  circle(parent, cx + main / 2 + gap + side / 2, cy, side, INSET(), LV_SYMBOL_NEXT, TEXT(),
         [e]() { request(e, "media_player.media_next_track", "", "", "panel_media"); });
}
// Artwork frame size. Square covers keep the square side; other shapes (video
// thumbnails) take their own aspect ratio at the largest size that fits
// max_w x max_h, shown whole.
struct ArtBox { int w, h; bool shaped; };
inline ArtBox art_box(const void *src, int square, int max_w, int max_h) {
  lv_image_header_t h{};
  if (!src || lv_image_decoder_get_info(src, &h) != LV_RESULT_OK || !h.w || !h.h) return {square, square, false};
  const int iw = h.w, ih = h.h;
  if (std::abs(iw - ih) * 20 <= std::max(iw, ih)) return {square, square, false};
  if (int64_t(max_w) * ih <= int64_t(max_h) * iw) return {max_w, int(int64_t(max_w) * ih / iw), true};
  return {int(int64_t(max_h) * iw / ih), max_h, true};
}
// Puts the shared cover image in frame: a shaped frame shows the whole image,
// a square one is covered (cover = crop the longer side) or fitted.
inline void place_artwork(lv_obj_t *frame, const void *source, const ArtBox &b, bool cover) {
  if (!main_artwork) main_artwork = lv_image_create(lv_layer_top());
  lv_obj_set_parent(main_artwork, frame);
  lv_obj_remove_flag(main_artwork, LV_OBJ_FLAG_HIDDEN);
  lv_image_set_src(main_artwork, source);
  lv_image_header_t h{};
  if (lv_image_decoder_get_info(source, &h) != LV_RESULT_OK || !h.w || !h.h) return;
  const int iw = h.w, ih = h.h;
  lv_image_set_pivot(main_artwork, iw / 2, ih / 2);
  const int side = b.shaped ? iw : cover ? std::min(iw, ih) : std::max(iw, ih);
  lv_image_set_scale(main_artwork, b.w * 256 / std::max(1, side));
  lv_obj_center(main_artwork);
}
// Tall cards put the artwork across the width with the track below it; short
// ones keep it beside the text. Both show the position above the transport.
inline void card_media(lv_obj_t *c, const Tile &t, const EntityDetailState &d) {
  const Rect r = t.r;
  const int pad = P(14), bh = P(48);
  const bool playing = shown_state(t.entity, d.state) == "playing";
  const int by = r.h - pad - bh;
  const int bar = P(6), prog_h = bar + P(4) + line(fonts.small);
  const int prog_y = by - P(10) - prog_h;
  const int meta_h = line(fonts.small) + line(fonts.title) + line(fonts.small);
  const bool tall = prog_y - pad - meta_h - P(8) >= P(140);
  const int side = tall ? std::min(r.w - 2 * pad, prog_y - pad - meta_h - P(18))
                        : std::max(P(48), std::min({P(104), prog_y - pad - P(10), r.w * 36 / 100}));
  if (onx_media::main_entity.empty()) onx_media::main_entity = t.entity;
  const void *source = !t.artwork && artwork_source && onx_media::main_entity == t.entity
      ? artwork_source(t.entity) : nullptr;
  // A wide cover uses the card's width (tall) or up to half of it (short).
  const ArtBox b = art_box(source, side, tall ? r.w - 2 * pad : std::max(side, r.w / 2),
                           tall ? prog_y - pad - meta_h - P(18) : side);
  const int art = b.h, ax = tall ? (r.w - b.w) / 2 : pad;
  auto *frame = box(c, ax, pad, b.w, b.h, INSET(), P(10));
  lv_obj_set_style_clip_corner(frame, true, 0);
  if (source) place_artwork(frame, source, b, false);
  else icon(c, t.entity, ax + art / 2, pad + art / 2, std::min(art * 46 / 100, P(96)), playing ? accent() : SOFT_TEXT());
  const int tx = tall ? pad : pad + b.w + P(12), tw = r.w - tx - pad;
  const bool track = !d.media_title.empty() && d.state != "off" && d.state != "idle";
  int y = tall ? pad + art + P(8) : pad + P(2);
  text(c, t.name, fonts.small, MUTED(), tx, y, tw);
  y += line(fonts.small);
  const std::string title = track ? onx_media::display_text(d.media_title) : label(d.state.empty() ? "idle" : d.state);
  const lv_font_t *f = fit(title, tw, {fonts.title, fonts.body});
  // Keep the artist line clear of the progress bar below it.
  const bool artist = track && !d.media_artist.empty();
  if (artist && f == fonts.title && y + line(f) + line(fonts.small) + P(12) > prog_y) f = fonts.body;
  text(c, title, f, TEXT(), tx, y, tw);
  y += line(f);
  if (artist) text(c, onx_media::display_text(d.media_artist), fonts.small, SOFT_TEXT(), tx, y, tw);
  if (!tall && t.ring >= 0 && pad + art - P(4) > y + line(fonts.small)) level(c, tx, pad + art - P(4), tw, P(4), t.ring / 1000.0f);
  if (by < pad + art || !d.available) return;
  if (prog_y >= (tall ? y : pad + art)) {
    MediaProgress m;
    m.entity = t.entity; m.w = r.w - 2 * pad; m.h = bar;
    m.group = box(c, pad, prog_y, m.w, prog_h, CARD(), 0, LV_OPA_TRANSP);
    box(m.group, 0, 0, m.w, bar, CONTROL(), bar / 2);
    m.fill = box(m.group, 0, 0, bar, bar, accent(), bar / 2);
    m.elapsed = text(m.group, "", fonts.small, MUTED(), 0, bar + P(4), m.w / 2);
    m.total = text(m.group, "", fonts.small, MUTED(), m.w / 2, bar + P(4), m.w / 2, LV_TEXT_ALIGN_RIGHT);
    lv_obj_add_flag(m.group, LV_OBJ_FLAG_HIDDEN);
    media_progress.push_back(m);
  }
  const std::string e = t.entity;
  transport(c, r.w / 2, by + bh / 2, bh - P(8), bh, P(22), e, playing);
}

// Media detail: artwork, title, "artist · room", the position with elapsed and
// total time below it, the transport and a volume slider along the bottom.
__attribute__((noinline)) inline void build_media(const Layout &l, const std::string &e, const EntityDetailState &d,
                                                  const std::string &title) {
  // Landscape: the artwork on the left, everything else in a column beside it.
  const int pad = P(16), side_art = wide() ? std::min(H - 2 * pad, W * 42 / 100) : 0;
  const int x = wide() ? pad + side_art + P(16) : pad, w = wide() ? W - x - pad : W - 2 * pad;
  const bool playing = shown_state(e, d.state) == "playing", playback = onx_media::has_playback(d.state);
  int bottom = l.ctl.bottom();
  // The 2.8" panel packs the controls tighter to leave room for the artwork/lyrics.
  const bool tight = std::min(W, H) < 320 && !wide();
  // Volume row.
  const bool volume = (d.features & 4) != 0;
  if (volume) {
    const int vh = P(48), vy = bottom - vh;
    float level = num(onx_controls::effective(d, "volume_level"));
    const float want = pending_value(e, "volume_level");
    clear_pending_if(e, "volume_level", level);
    if (std::isfinite(want)) level = want;
    const int pct_v = std::isfinite(level) ? std::clamp(int(std::lround(level * 100)), 0, 100) : 0;
    auto *row = box(root, x, vy, w, vh, CARD(), P(14));
    text(row, LV_SYMBOL_VOLUME_MAX, fonts.symbol, SOFT_TEXT(), P(12), (vh - line(fonts.symbol)) / 2);
    // The track starts after the speaker glyph's own width plus a gap (a fixed
    // offset ran into the glyph's sound waves).
    const int nw = text_width("100", fonts.body) + P(4), sx = P(12) + text_width(LV_SYMBOL_VOLUME_MAX, fonts.symbol) + P(14),
              sw = w - sx - nw - P(24);
    lv_obj_t *value = text(row, std::to_string(pct_v), fonts.body, TEXT(), w - P(12) - nw, (vh - line(fonts.body)) / 2, nw,
                           LV_TEXT_ALIGN_RIGHT);
    const int th = P(6), ks = P(22);
    // A taller transparent touch strip around the thin track.
    lv_obj_t *strip = box(row, sx, 0, sw, vh, CARD(), 0, LV_OPA_TRANSP);
    box(strip, 0, (vh - th) / 2, sw, th, TRACK(), th / 2);
    const int fw = sw * pct_v / 100;
    lv_obj_t *fill = box(strip, 0, (vh - th) / 2, std::max(th, fw), th, accent(), th / 2);
    lv_obj_t *knob = box(strip, std::clamp(fw - ks / 2, 0, sw - ks), (vh - ks) / 2, ks, ks, TEXT(), ks / 2);
    drag(strip, [e, strip, fill, knob, value, sw, th, ks](lv_point_t p, bool commit) {
      lv_area_t a;
      lv_obj_get_coords(strip, &a);
      const int v = std::clamp(int(std::lround((p.x - a.x1) * 100.0f / sw)), 0, 100);
      const int fw = sw * v / 100;
      lv_obj_set_width(fill, std::max(th, fw));
      lv_obj_set_x(knob, std::clamp(fw - ks / 2, 0, sw - ks));
      lv_label_set_text(value, std::to_string(v).c_str());
      if (!commit) return;
      set_pending(e, "volume_level", v / 100.0f);
      char buf[8];
      std::snprintf(buf, sizeof(buf), "%.2f", v / 100.0f);
      request(e, "media_player.volume_set", "volume_level", buf, "panel_volume");
    });
    bottom = vy - (tight ? P(8) : P(14));
  }
  // Transport.
  const int bh = P(64), by = bottom - bh;
  transport(root, x + w / 2, by + bh / 2, P(52), bh, P(28), e, playing);
  // Position: bar, then elapsed / total underneath.
  // The 2.8" lets the time labels' empty lower line space reach toward the buttons.
  const int bar = P(6), lab = tight ? P(3) : P(6), prog_h = bar + lab + line(fonts.small),
            prog_y = by - (tight ? -P(2) : P(16)) - prog_h;
  {
    MediaProgress m;
    m.entity = e; m.w = w; m.h = bar; m.remaining = false;
    m.group = box(root, x, prog_y, w, prog_h, BG(), 0, LV_OPA_TRANSP);
    box(m.group, 0, 0, w, bar, TRACK(), bar / 2);
    m.fill = box(m.group, 0, 0, bar, bar, accent(), bar / 2);
    m.elapsed = text(m.group, "", fonts.small, MUTED(), 0, bar + lab, w / 2);
    m.total = text(m.group, "", fonts.small, MUTED(), w / 2, bar + lab, w / 2, LV_TEXT_ALIGN_RIGHT);
    lv_obj_add_flag(m.group, LV_OBJ_FLAG_HIDDEN);
    media_progress.push_back(m);
  }
  // Title and "artist · room".
  const std::string name = playback && !d.media_title.empty() ? onx_media::display_text(d.media_title)
                                                              : playback ? label(d.state) : "Nothing playing";
  const std::string sub = playback ? onx_media::display_text(d.media_artist) : "";
  const lv_font_t *tf = fit(name, w, {fonts.title, fonts.body});
  const int sub_y = prog_y - (tight ? P(6) : P(12)) - line(fonts.body), title_y = sub_y - line(tf);
  text(root, name, tf, TEXT(), x, title_y, w, LV_TEXT_ALIGN_CENTER);
  if (!sub.empty()) text(root, sub, fonts.body, MUTED(), x, sub_y, w, LV_TEXT_ALIGN_CENTER);
  const int top = pad, room = title_y - (tight ? P(8) : P(12)) - top;
  onx_media::main_entity = e;
  const bool lyrics = playback && onx_lyrics::available(e, d.media_title);
  auto toggle = [](lv_event_t *) { if (allowed()) { onx_lyrics::toggle(); built_signature.clear(); } };
  // Synced lyrics (optional blueprint) take the artwork's whole column; a tap
  // shows the artwork again.
  if (lyrics && onx_lyrics::shown(e, d.media_title)) {
    detach_artwork();
    const int ry = wide() ? pad : top + header_reserve, rh = wide() ? H - 2 * pad : room - header_reserve;
    const int rx = wide() ? pad : x, rw = wide() ? side_art : w;
    auto *panel = box(root, rx, ry, rw, rh, CARD(), P(14));
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    on(panel, LV_EVENT_CLICKED, toggle);
    if (const auto *glow = make_backdrop(artwork_source ? artwork_source(e) : nullptr, rw, rh)) {
      lv_obj_set_style_clip_corner(panel, true, 0);
      auto *img = lv_image_create(panel);
      lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
      lv_image_set_src(img, glow);
      lv_obj_set_pos(img, 0, 0);
    }
    const int lp = P(10);
    auto *area = box(panel, lp, lp, rw - 2 * lp, rh - 2 * lp, CARD(), 0, LV_OPA_TRANSP);
    lyric_view = LyricView{e};
    lyric_view.w = rw - 2 * lp; lyric_view.h = rh - 2 * lp;
    auto make = [&](uint32_t color) {
      auto *l = text(area, "", fonts.body, color, 0, 0);
      lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
      lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_style_bg_color(l, lv_color_black(), 0);
      lv_obj_set_style_bg_opa(l, LV_OPA_50, 0);
      lv_obj_set_style_radius(l, P(12), 0);
      lv_obj_set_style_pad_hor(l, P(8), 0);
      lv_obj_set_style_pad_ver(l, P(3), 0);
      return l;
    };
    // Lines sit on black pills over the artwork: keep them white in both themes.
    lyric_view.prev = make(0xFFFFFF); lyric_view.cur = make(0xFFFFFF); lyric_view.next = make(0xFFFFFF);
    for (auto *l : {lyric_view.prev, lyric_view.cur, lyric_view.next}) {
      lv_obj_add_flag(l, onx_render::FIXED_PALETTE);
      lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    }
    tick_media_progress();
    return;
  }
  // Artwork fills the space above, centred: square covers as a square, other
  // shapes (video thumbnails) at their own aspect across the whole width,
  // below the header buttons.
  const void *source = artwork_source ? artwork_source(e) : nullptr;
  const int square = wide() ? side_art : std::max(P(48), std::min({P(220), w - 2 * header_reserve, room}));
  const int max_h = wide() ? H - 2 * pad : std::max(P(48), room - header_reserve);
  const ArtBox b = art_box(source, square, wide() ? side_art : w, max_h);
  const int art_top = wide() || !b.shaped ? top : top + header_reserve, art_room = wide() || !b.shaped ? room : max_h;
  const int ax = wide() ? pad + (side_art - b.w) / 2 : (W - b.w) / 2,
            ay = wide() ? (H - b.h) / 2 : art_top + std::max(0, (art_room - b.h) / 2);
  auto *frame = box(root, ax, ay, b.w, b.h, CONTROL(), P(14));
  lv_obj_set_style_clip_corner(frame, true, 0);
  if (lyrics) { lv_obj_add_flag(frame, LV_OBJ_FLAG_CLICKABLE); on(frame, LV_EVENT_CLICKED, toggle); }
  if (source) place_artwork(frame, source, b, true);
  else icon(root, e, ax + b.w / 2, ay + b.h / 2, b.w * 40 / 100, playing ? accent() : SOFT_TEXT());
  tick_media_progress();
}

inline std::string camera_ptz_entity(const std::string &e) {
  const int i = onx_sensor_view::index(e);
  return i >= 0 && onx_sensor_view::configs[i].secondary[0] ? std::string(onx_sensor_view::configs[i].secondary) : std::string();
}

inline const char *ptz_caption(const char *dir) {
  const std::string d = dir;
  return d == "LEFT" ? "Panning left" : d == "RIGHT" ? "Panning right" : d == "UP" ? "Tilting up" : d == "DOWN" ? "Tilting down"
       : d == "ZOOM_IN" ? "Zooming in" : "Zooming out";
}
// Landscape camera: the name across the top, a wide snapshot below it and the
// buttons in a narrow column (two with PTZ) on the right, so the 16:9 frame
// gets most of the screen rather than the default half-width visual column.
inline Layout camera_layout(const Layout &base, bool ptz) {
  if (!wide()) {
    // Portrait: the snapshot fills everything between the name and a bottom
    // row of buttons (two rows with PTZ).
    const int pad = P(16), gap = P(8), rh = P(48), rows = ptz ? 2 : 1;
    const int ctl_h = rows * rh + (rows - 1) * gap, top = pad + P(52);
    Layout l{};
    l.head = {pad, pad, W - 2 * pad, P(48)};
    l.ctl = {pad, H - pad - ctl_h, W - 2 * pad, ctl_h};
    l.vis = {pad, top, W - 2 * pad, l.ctl.y - P(12) - top};
    return l;
  }
  const int pad = P(16), gap = P(12), col = P(52), top = pad + P(44);
  const int cw = ptz ? 2 * col + P(8) : col;
  Layout l{};
  l.head = {pad, pad, W - 2 * pad, P(48)};
  l.vis = {pad, top, W - 2 * pad - cw - gap, H - pad - top};
  l.ctl = {l.vis.x + l.vis.w + gap, top, cw, l.vis.h};
  return l;
}
__attribute__((noinline)) inline void build_camera(const Layout &base, const std::string &e, const EntityDetailState &d, const std::string &title) {
  onx_media::main_entity = e;
  const Layout l = camera_layout(base, !camera_ptz_entity(e).empty());
  const bool is_on = d.state == "streaming" || d.state == "recording";
  const bool has_motion = d.motion_detection == "True" || d.motion_detection == "true" ||
                          d.motion_detection == "False" || d.motion_detection == "false";
  const bool motion_on = d.motion_detection == "True" || d.motion_detection == "true";
  const std::string ptz = camera_ptz_entity(e);
  // The power button already shows on/off; the line keeps motion and PTZ only.
  std::string context;
  if (has_motion) context = motion_on ? "Motion on" : "Motion off";
  if (!ptz.empty()) context += context.empty() ? "PTZ" : " · PTZ";
  header(l.head, title, context);
  const void *source = artwork_source ? artwork_source(e) : nullptr;
  const int square = std::min(l.vis.w, l.vis.h);
  const ArtBox b = source ? art_box(source, square, l.vis.w, l.vis.h)
                          : ArtBox{l.vis.w, std::min(l.vis.h, l.vis.w * 9 / 16), true};
  const int ax = l.vis.x + (l.vis.w - b.w) / 2, ay = l.vis.y + (l.vis.h - b.h) / 2;
  auto *frame = box(root, ax, ay, b.w, b.h, CONTROL(), P(14));
  lv_obj_set_style_clip_corner(frame, true, 0);
  interactive(frame, false);
  on(frame, LV_EVENT_CLICKED, [](lv_event_t *) { if (allowed() && refresh_camera) refresh_camera(); });
  if (source) {
    place_artwork(frame, source, b, true);
  } else {
    icon(root, e, ax + b.w / 2, ay + b.h / 2 - P(10), std::min(b.h * 38 / 100, P(56)), is_on ? accent() : SOFT_TEXT());
    text(root, "Snapshot", fonts.small, MUTED(), ax + P(8), ay + b.h / 2 + P(22), b.w - P(16), LV_TEXT_ALIGN_CENTER);
  }
  struct Btn { const char *symbol; bool hot; std::function<void()> fn; };
  std::vector<Btn> btns;
  btns.push_back({LV_SYMBOL_REFRESH, false, []() { if (refresh_camera) refresh_camera(); toast("Refreshing snapshot"); }});
  if (d.features & 1) {
    btns.push_back({LV_SYMBOL_POWER, is_on, [e, is_on]() {
      request(e, is_on ? "camera.turn_off" : "camera.turn_on", "", is_on ? "idle" : "streaming", "state");
      toast(is_on ? "Turning camera off" : "Turning camera on");
    }});
  }
  if (has_motion) {
    btns.push_back({motion_on ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE, motion_on, [e, motion_on]() {
      request(e, motion_on ? "camera.disable_motion_detection" : "camera.enable_motion_detection",
              "", motion_on ? "false" : "true", "motion_detection");
      toast(motion_on ? "Motion detection off" : "Motion detection on");
    }});
  }
  if (wide()) {
    // Columns: actions, then (with PTZ) the six moves.
    const int gap = P(8), col = ptz.empty() ? l.ctl.w : (l.ctl.w - gap) / 2;
    const int n = btns.size(), bh = std::min(P(52), (l.ctl.h - gap * (n - 1)) / n);
    for (int i = 0; i < n; ++i)
      icon_button(root, l.ctl.x, l.ctl.y + i * (bh + gap), col, bh, P(14), btns[i].hot ? strong() : CONTROL(),
                  btns[i].symbol, TEXT(), btns[i].fn);
    if (!ptz.empty()) {
      struct Ptz { const char *symbol; const char *param; const char *dir; };
      static constexpr Ptz moves[] = {
          {LV_SYMBOL_UP, "tilt", "UP"},          {LV_SYMBOL_DOWN, "tilt", "DOWN"},
          {LV_SYMBOL_LEFT, "pan", "LEFT"},       {LV_SYMBOL_RIGHT, "pan", "RIGHT"},
          {LV_SYMBOL_PLUS, "zoom", "ZOOM_IN"},   {LV_SYMBOL_MINUS, "zoom", "ZOOM_OUT"},
      };
      const int pgap = P(6), ph = (l.ctl.h - pgap * 5) / 6, x = l.ctl.x + col + gap;
      for (int i = 0; i < 6; ++i) {
        const auto m = moves[i];
        icon_button(root, x, l.ctl.y + i * (ph + pgap), l.ctl.w - col - gap, ph, P(12), CONTROL(), m.symbol, TEXT(),
                    [ptz, m]() { if (send_ptz) send_ptz(ptz, m.param, m.dir); toast(ptz_caption(m.dir)); });
      }
    }
    return;
  }
  const int gap = P(8), rows = ptz.empty() ? 1 : 2;
  const int rh = std::min(P(48), (l.ctl.h - gap * (rows - 1)) / rows);
  int y = l.ctl.y;
  {
    const int n = btns.size(), bw = (l.ctl.w - gap * (n - 1)) / n;
    for (int i = 0; i < n; ++i) {
      const int w = i == n - 1 ? l.ctl.w - i * (bw + gap) : bw;
      icon_button(root, l.ctl.x + i * (bw + gap), y, w, rh, P(14), btns[i].hot ? strong() : CONTROL(),
                  btns[i].symbol, TEXT(), btns[i].fn);
    }
    y += rh + gap;
  }
  if (!ptz.empty() && y + rh <= l.ctl.bottom() + P(4)) {
    struct Ptz { const char *symbol; const char *param; const char *dir; };
    static constexpr Ptz moves[] = {
        {LV_SYMBOL_LEFT, "pan", "LEFT"},     {LV_SYMBOL_UP, "tilt", "UP"},
        {LV_SYMBOL_DOWN, "tilt", "DOWN"},    {LV_SYMBOL_RIGHT, "pan", "RIGHT"},
        {LV_SYMBOL_MINUS, "zoom", "ZOOM_OUT"}, {LV_SYMBOL_PLUS, "zoom", "ZOOM_IN"},
    };
    const int pgap = P(6), n = 6, bw = (l.ctl.w - pgap * (n - 1)) / n;
    for (int i = 0; i < n; ++i) {
      const auto m = moves[i];
      const int w = i == n - 1 ? l.ctl.w - i * (bw + pgap) : bw;
      icon_button(root, l.ctl.x + i * (bw + pgap), y, w, rh, P(12), CONTROL(), m.symbol, TEXT(),
                  [ptz, m]() { if (send_ptz) send_ptz(ptz, m.param, m.dir); toast(ptz_caption(m.dir)); });
    }
  }
}

inline void card_generic(lv_obj_t *c, const Tile &t, const EntityDetailState *d) {
  // Covers: the position as the card's bar.
  // Only where the reading still fits above it.
  if (t.ring < 0 && d != nullptr && d->available && t.entity.rfind("cover.", 0) == 0 && std::isfinite(num(d->position)) &&
      t.r.h - P(14) * 2 - P(44) - P(10) - P(22) >= line(fonts.big)) {
    Tile with_bar = t;
    with_bar.ring = int(std::clamp(num(d->position), 0.0f, 100.0f) * 10);
    return card_generic(c, with_bar, d);
  }
  const Rect r = t.r;
  const std::string e = t.entity, domain = e.substr(0, e.find('.'));
  const int pad = P(14), cs = P(44), tx = pad + cs + P(12);
  const bool available = d != nullptr && d->available;
  const bool switchable = available && (domain == "switch" || domain == "input_boolean" || domain == "automation" ||
                                        domain == "fan" || domain == "siren");
  const bool runnable = available && (domain == "scene" || domain == "script" || domain == "button" || domain == "input_button");
  // Numbers: the value between −/+ steppers along the bottom, as the climate
  // card, so a tap changes it without opening the detail.
  if (available && (domain == "number" || domain == "input_number") && std::isfinite(num(d->state))) {
    // Short cards (split rows) leave out the chip so the name keeps its width.
    const bool slim = r.h < P(150) || r.w < P(200);  // Split rows and half-width cards.
    const int head = slim ? line(fonts.body) + P(4) : cs;
    int bs = std::min(P(52), r.h - 2 * pad - head - P(4));
    if (bs >= P(30)) {
      if (slim) text(c, t.name, fit(t.name, r.w - 2 * pad, {fonts.body, fonts.small}), TEXT(), pad, pad, r.w - 2 * pad);
      else { chip_icon(c, e, pad, pad, cs, true); titled(c, t, tx, pad + P(2), r.w - tx - pad); }
      const int by = r.h - pad - bs;
      const std::string v = onx_detail_view::compact(d->state);
      const std::string u = d->unit;
      // Tall cards: a large value in the middle; short ones keep it in the row.
      const int mid_top = pad + head + P(8);
      const bool tall = by - mid_top >= line(fonts.big) + P(8);
      const bool hero = tall && by - mid_top >= line(fonts.hero) + P(8) && hero_text(v);
      const lv_font_t *vf = tall ? (hero ? fonts.hero : fonts.big) : fit(v + u, r.w - 2 * pad - 2 * bs - P(16), {fonts.big, fonts.title, fonts.body});
      const int vy = tall ? mid_top + (by - mid_top - line(vf)) / 2 : by + (bs - line(vf)) / 2;
      int uw = u.empty() ? 0 : text_width(u, fonts.small) + P(4);
      const int vw = text_width(v, vf) + 1;
      if (!tall) {  // Between the buttons: drop the unit, then narrow the buttons.
        if (vw + uw > r.w - 2 * pad - 2 * bs - P(8)) uw = 0;
        bs = std::max(P(30), std::min(bs, (r.w - 2 * pad - vw - P(8)) / 2));
      }
      const int vx = (r.w - vw - uw) / 2;
      text(c, v, vf, TEXT(), vx, vy, vw);
      if (uw) text(c, u, fonts.small, MUTED(), vx + vw + P(4), vy + line(vf) - line(fonts.small) - P(4), uw);
      const int bcy = by + (r.h - pad - by) / 2;  // The row the value is centred in.
      auto stepper = [e](int direction) {
        custom_logic_entity_sub::select_control_entity(e);
        custom_logic_entity_sub::control_setting = "state";
        onx_controls::rotate(e, direction);
      };
      circle(c, pad + bs / 2, bcy, bs, INSET(), LV_SYMBOL_MINUS, TEXT(), [stepper]() { stepper(-1); });
      circle(c, r.w - pad - bs / 2, bcy, bs, INSET(), LV_SYMBOL_PLUS, TEXT(), [stepper]() { stepper(1); });
      return;
    }
  }
  // Percentages in a short card: a ring with the value, the name beside it and
  // a sparkline below the name.
  const float pv = d != nullptr ? num(d->state) : NAN;
  if (available && !switchable && d->unit == "%" && std::isfinite(pv) && pv >= 0 && pv <= 100 && r.h < P(300) &&
      r.h >= P(110)) {
    const int tw = r.w - 2 * pad;
    // The 2.8" keeps the title to one line so the ring has room.
    const int nl = lines_for(t.name, fonts.title, tw, std::min(W, H) < 320 ? 1 : 2);
    text(c, t.name, fonts.title, TEXT(), pad, pad, tw, LV_TEXT_ALIGN_LEFT, nl);
    const int top = pad + nl * line(fonts.title) + P(10);
    const int avail = r.h - pad - top;
    if (avail <= 0) return;
    const int size = std::min(avail, r.w * 34 / 100);
    lv_obj_t *arc = lv_arc_create(c);
    lv_obj_set_size(arc, size, size);
    lv_obj_set_pos(arc, pad, top + (avail - size) / 2);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_range(arc, 0, 1000);
    lv_arc_set_value(arc, int(pv * 10));
    const int aw = std::max(P(8), size / 10);
    lv_obj_set_style_arc_width(arc, aw, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, aw, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(CONTROL()), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(accent()), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(arc, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);
    lv_obj_remove_flag(arc, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    const std::string v = fmt(pv, pv < 10 ? 1 : 0) + "%";
    const lv_font_t *vf = fit(v, size - 2 * aw - P(8), {fonts.big, fonts.title, fonts.body, fonts.small});
    text(c, v, vf, TEXT(), pad, top + (avail - line(vf)) / 2, size, LV_TEXT_ALIGN_CENTER);
    const int nx = pad + size + P(14), nw = r.w - nx - pad;
    int y = top;
    if (!t.context.empty()) {
      const int lines = std::clamp(avail / line(fonts.body), 1, 2);
      text(c, t.context, fonts.body, SOFT_TEXT(), nx, y, nw, LV_TEXT_ALIGN_LEFT, lines);
      y += lines_for(t.context, fonts.body, nw, lines) * line(fonts.body) + P(8);
    }
    const auto history = card_history(e);
    const int gh = r.h - pad - y;
    if (gh >= P(24) && nw >= P(48)) chart(c, nx, y, nw, gh, e, history, accent(), false);
    return;
  }
  lv_obj_t *chip = chip_icon(c, e, pad, pad, cs, t.on);
  lv_obj_t *chip_view = !icon_uses.empty() ? icon_uses.back().view : nullptr;
  // Short sensor cards: too low for the large reading, so a smaller one
  // replaces the context line (which repeats the value).
  const bool short_numeric = available && !runnable && d != nullptr && std::isfinite(num(d->state)) &&
                             (domain == "sensor" || domain == "number" || domain == "input_number");
  const int short_room = r.h - pad - (t.ring >= 0 ? P(22) : 0) - (pad + cs + P(10));
  const bool small_reading = short_numeric && short_room < line(fonts.big) && short_room >= line(fonts.title);
  const int before = lv_obj_get_child_count(c);
  if (small_reading) {
    Tile head = t;
    head.context.clear();
    titled(c, head, tx, pad + P(2), r.w - tx - pad);
  } else titled(c, t, tx, pad + P(2), r.w - tx - pad - (switchable ? P(62) : 0));
  lv_obj_t *context = !t.context.empty() && int(lv_obj_get_child_count(c)) > before ? lv_obj_get_child(c, -1) : nullptr;
  if (switchable) {
    const bool on = shown_state(e, d->state) == "on";
    lv_obj_t *sw_toggle = toggle(c, r.w - pad - P(54), pad + P(7), on, [e, domain]() {
      const auto *dd = custom_logic_entity_sub::entity_detail(e);
      const bool now_on = dd != nullptr && shown_state(e, dd->state) == "on";
      request(e, domain + ".toggle", "", now_on ? "off" : "on", "state");
    });
    // On/off and the context line change in place; anything else rebuilds.
    card_patch = [e, chip, chip_view, context, sw_toggle](const Tile &next) {
      const auto *dd = custom_logic_entity_sub::entity_detail(e);
      if (dd == nullptr) return false;
      const bool now_on = shown_state(e, dd->state) == "on";
      patch_chip(chip, chip_view, now_on);
      patch_toggle(sw_toggle, now_on);
      if (context) onx_render::label(context, next.context.c_str());
      else if (!next.context.empty()) return false;
      return true;
    };
  }
  int bottom = r.h - pad;
  auto run = [e, domain]() {
    if (domain == "script") request(e, "script.turn_on", "", "on", "panel_run");
    else onx_controls::press(e);
  };
  // Tall cards: one large round target in the middle instead of a thin pill.
  const int top = pad + cs + P(10);
  const int strip = P(16) + 2 * line(fonts.small) + P(14);
  int room = bottom - top - line(fonts.body) - P(10);
  if (room - strip >= P(140)) room -= strip;
  if (runnable && room >= P(140)) {
    const int s = std::min({P(150), room - P(16), r.w - 2 * pad});
    const int cy = top + P(8) + (room - P(16)) / 2;
    lv_obj_t *b = circle(c, r.w / 2, cy, s, accent(), LV_SYMBOL_PLAY, TEXT(), run);
    lv_obj_set_style_border_width(b, P(8), 0);
    lv_obj_set_style_border_color(b, lv_color_hex(CONTROL()), 0);
    // Last run: the state of a scene/button is its activation time; scripts
    // use the starts recorded on the panel.
    const time_t now = std::time(nullptr);
    const auto *log = changes_for(e);
    time_t last = log != nullptr && !log->items.empty() ? log->items.front().at : 0;
    if (!last && domain != "script") custom_logic_entity_sub::parse_ha_datetime_epoch(d->state, &last);
    const std::string caption = last ? "Last run " + onx_sensor_view::relative(last, now) : std::string("Run");
    const int ty = cy + s / 2 + P(10);
    text(c, caption, fonts.body, TEXT(), pad, ty, r.w - 2 * pad, LV_TEXT_ALIGN_CENTER);
    // Runs over the last 24 h as ticks on a strip along the bottom.
    const int sh = P(16), sy = r.h - pad - sh - line(fonts.small) - P(4);
    if (sy >= ty + line(fonts.body) + P(8) + line(fonts.small) && now > 1577836800) {
      text(c, "Runs, last 24 h", fonts.small, MUTED(), pad, sy - line(fonts.small) - P(2), r.w - 2 * pad);
      const int sw = r.w - 2 * pad;
      auto *strip = box(c, pad, sy, sw, sh, INSET(), P(4));
      if (log != nullptr)
        for (const auto &run : log->items) {
          if (run.at > now || now - run.at >= 86400) continue;
          const int x = int(int64_t(86400 - (now - run.at)) * (sw - P(4)) / 86400);
          box(strip, x, 0, P(4), sh, accent(), P(2));
        }
      text(c, "-24 h", fonts.small, MUTED(), pad, sy + sh + P(4), sw / 2);
      text(c, "now", fonts.small, MUTED(), pad + sw / 2, sy + sh + P(4), sw / 2, LV_TEXT_ALIGN_RIGHT);
    }
    return;
  }
  // Wide, short cards (landscape): the round button on the left, last run
  // and the 24 h strip beside it.
  if (runnable && r.w >= r.h && room >= P(84) && r.w - 2 * pad - std::min(P(150), room) >= P(140)) {
    const int s = std::min(P(150), room);
    const int cy = top + (bottom - top) / 2;
    lv_obj_t *b = circle(c, pad + s / 2, cy, s, accent(), LV_SYMBOL_PLAY, TEXT(), run);
    lv_obj_set_style_border_width(b, std::max(P(4), s / 18), 0);
    lv_obj_set_style_border_color(b, lv_color_hex(CONTROL()), 0);
    const time_t now = std::time(nullptr);
    const auto *log = changes_for(e);
    time_t last = log != nullptr && !log->items.empty() ? log->items.front().at : 0;
    if (!last && domain != "script") custom_logic_entity_sub::parse_ha_datetime_epoch(d->state, &last);
    const int rx = pad + s + P(16), rw = r.w - rx - pad;
    const std::string caption = last ? "Last run " + onx_sensor_view::relative(last, now) : std::string("Tap to run");
    const int sh = P(16);
    const bool strip_fits = now > 1577836800 && bottom - top >= line(fonts.body) + P(12) + line(fonts.small) * 2 + sh + P(8);
    const int block = line(fonts.body) + (strip_fits ? P(12) + line(fonts.small) + P(2) + sh + P(4) + line(fonts.small) : 0);
    int y = cy - block / 2;
    text(c, caption, fonts.body, TEXT(), rx, y, rw);
    if (strip_fits) {
      y += line(fonts.body) + P(12);
      text(c, "Runs, last 24 h", fonts.small, MUTED(), rx, y, rw);
      y += line(fonts.small) + P(2);
      auto *strip = box(c, rx, y, rw, sh, INSET(), P(4));
      if (log != nullptr)
        for (const auto &run : log->items) {
          if (run.at > now || now - run.at >= 86400) continue;
          box(strip, int(int64_t(86400 - (now - run.at)) * (rw - P(4)) / 86400), 0, P(4), sh, accent(), P(2));
        }
      y += sh + P(4);
      text(c, "-24 h", fonts.small, MUTED(), rx, y, rw / 2);
      text(c, "now", fonts.small, MUTED(), rx + rw / 2, y, rw / 2, LV_TEXT_ALIGN_RIGHT);
    }
    return;
  }
  if (runnable && bottom - P(48) >= pad + cs + P(8)) {
    bottom -= P(48);
    button(c, pad, bottom, r.w - 2 * pad, P(48), P(24), INSET(), "Run", fonts.body, TEXT(), run);
    bottom -= P(10);
  }
  // Covers in tall cards: open / stop / close along the bottom, as the detail.
  if (available && domain == "cover") {
    struct Btn { const char *caption, *service; uint32_t bit; };
    std::vector<Btn> shown;
    for (const Btn &b : {Btn{"Open", "cover.open_cover", 1}, Btn{"Stop", "cover.stop_cover", 8}, Btn{"Close", "cover.close_cover", 2}})
      if (d->features == 0 || (d->features & b.bit)) shown.push_back(b);
    const int bh = P(44), gap = P(8);
    if (!shown.empty() && bottom - bh - P(10) - P(22) - (pad + cs + P(10)) >= line(fonts.big)) {
      bottom -= bh;
      const int n = shown.size(), bw = (r.w - 2 * pad - gap * (n - 1)) / n;
      for (int i = 0; i < n; ++i) {
        const Btn b = shown[i];
        button(c, pad + i * (bw + gap), bottom, i == n - 1 ? r.w - 2 * pad - i * (bw + gap) : bw, bh, P(14), INSET(), b.caption,
               fonts.body, TEXT(), [e, b]() { request(e, b.service, "", "", "panel_cover"); });
      }
      bottom -= P(10);
    }
  }
  const bool bar = t.ring >= 0 && !runnable;
  if (bar) bottom -= P(12) + P(10);
  const int vy = pad + cs + P(10);
  if (small_reading) {
    const std::string v = onx_detail_view::compact(d->state), u = d->unit;
    const int vw = std::min(text_width(v, fonts.title) + 1, r.w - 2 * pad), vt = bottom - line(fonts.title);
    text(c, v, fonts.title, TEXT(), pad, vt, vw);
    if (!u.empty() && vw + P(4) < r.w - 2 * pad)
      text(c, u, fonts.small, MUTED(), pad + vw + P(4), vt + line(fonts.title) - line(fonts.small) - P(2), r.w - 2 * pad - vw - P(4));
    if (bar) level(c, pad, bottom + P(10), r.w - 2 * pad, P(12), t.ring / 1000.0f);
    return;
  }
  if (bottom - vy < line(fonts.big)) {
    if (bar && bottom + P(10) > vy) level(c, pad, bottom + P(10), r.w - 2 * pad, P(12), t.ring / 1000.0f);
    return;
  }
  // The value sits at the bottom of the card, above its bar.
  const bool numeric = d != nullptr && std::isfinite(num(d->state)) && (domain == "sensor" || domain == "number" || domain == "input_number");
  const std::string value = numeric ? onx_detail_view::compact(d->state) : t.value;
  const std::string unit = numeric ? d->unit : "";
  // Smart plugs: On/Off large with the power reading beside it, then the
  // power history as 24 columns along the bottom.
  if (domain == "switch" && available) {
    const int i = onx_sensor_view::index(e);
    if (i >= 0 && onx_sensor_view::configs[i].secondary[0]) {
      const auto &s = onx_sensor_view::states[i];
      const std::string state = d->state == "on" ? "On" : "Off";
      // Short (split) cards: a smaller state and no caption, so the chart
      // still gets a useful height.
      const int caption = line(fonts.small) + P(4);
      const lv_font_t *sf = r.h - pad - vy - line(fonts.big) - P(10) - caption >= P(48) ? fonts.big : fonts.title;
      const bool label = r.h - pad - vy - line(sf) - P(8) - caption >= P(40);
      text(c, state, sf, TEXT(), pad, vy, text_width(state, sf) + 1);
      if (s.received && s.available) {
        const int sx = pad + text_width(state, sf) + P(10);
        const std::string now = onx_detail_view::compact(s.value) + " " + s.unit + (s.unit == "W" ? " now" : "");
        text(c, now, fonts.body, SOFT_TEXT(), sx, vy + line(sf) - line(fonts.body) - (sf == fonts.big ? P(4) : 0), r.w - sx - pad);
      }
      const int gy = vy + line(sf) + P(8), gb = r.h - pad - (label ? caption : 0);
      auto history = card_history(e);
      if (gb - gy >= P(20) && chart(c, pad, gy, r.w - 2 * pad, gb - gy, e, std::move(history), accent(), false, true) && label)
        text(c, std::string(s.unit == "kWh" ? "Energy" : "Power") + " · " + s.range_label(), fonts.small, MUTED(), pad,
             gb + P(4), r.w - 2 * pad);
      return;
    }
  }
  // With history: the reading under the title, then min / max, and the chart
  // fills the rest of the card.
  if (available && !runnable) {
    auto history = card_history(e);
    const bool big = bottom - vy >= line(fonts.hero) + line(fonts.small) + P(120) && numeric && hero_safe(value);
    const int vh = big ? line(fonts.hero) : line(fonts.big), sy = vy + vh, gy = sy + line(fonts.small) + P(8);
    if (history.size() >= 2 && bottom - gy >= P(44)) {
      const float delta = history.back().value - history.front().value;
      const Chart *ch = chart(c, pad, gy, r.w - 2 * pad, bottom - gy, e, std::move(history), accent(), true);
      if (ch != nullptr) {
        reading(c, value, unit, pad, vy, r.w - 2 * pad, big);
        const int i = onx_sensor_view::index(e);
        std::string stats = onx_sensor_view::states[i].range_label() + "  ·  min " + fmt(ch->lo, 1) + "  ·  max " + fmt(ch->hi, 1);
        text(c, stats, fonts.small, MUTED(), pad, sy, r.w - 2 * pad);
        if (std::fabs(delta) >= 0.05f)
          text(c, std::string(delta > 0 ? "+" : "-") + fmt(std::fabs(delta), 1), fonts.small, accent(), pad, sy, r.w - 2 * pad,
               LV_TEXT_ALIGN_RIGHT);
        if (bar) level(c, pad, bottom + P(10), r.w - 2 * pad, P(12), t.ring / 1000.0f);
        return;
      }
    }
  }
  const bool hero = bottom - vy >= line(fonts.hero) && numeric && hero_safe(value);
  const int vh = hero ? line(fonts.hero) : line(fonts.big);
  reading(c, value, unit, pad, bottom - vh, r.w - 2 * pad, hero);
  if (bar) level(c, pad, bottom + P(10), r.w - 2 * pad, P(12), t.ring / 1000.0f);
}

// A compact row's direct control: a toggle for on/off entities, a run button
// for scenes, scripts and buttons. Returns its left edge, or the right padding.
inline int row_control(lv_obj_t *c, const Tile &t, const EntityDetailState *d, int pad) {
  const Rect r = t.r;
  if (d == nullptr || !d->available || r.w < P(180)) return r.w - pad;
  const std::string e = t.entity, domain = e.substr(0, e.find('.'));
  const int th = P(30);
  if (domain == "light" || domain == "switch" || domain == "input_boolean" || domain == "fan" ||
      domain == "automation" || domain == "siren") {
    const bool on = domain == "light" ? light_shown_on(e) : shown_state(e, d->state) == "on";
    const int x = r.w - pad - P(54);
    toggle(c, x, (r.h - th) / 2, on, [e, domain]() {
      if (domain == "light") { light_toggle(e); return; }
      const auto *dd = custom_logic_entity_sub::entity_detail(e);
      const bool now_on = dd != nullptr && shown_state(e, dd->state) == "on";
      request(e, domain + ".toggle", "", now_on ? "off" : "on", "state");
    });
    return x;
  }
  if (domain == "scene" || domain == "script" || domain == "button" || domain == "input_button") {
    const int s = std::min(P(36), r.h - P(10));
    circle(c, r.w - pad - s / 2, r.h / 2, s, soft(), LV_SYMBOL_PLAY, accent(), [e, domain]() {
      if (domain == "script") request(e, "script.turn_on", "", "on", "panel_run");
      else onx_controls::press(e);
    });
    return r.w - pad - s;
  }
  if (domain == "vacuum") {
    const bool cleaning = shown_state(e, d->state) == "cleaning";
    const int s = std::min(P(36), r.h - P(10));
    circle(c, r.w - pad - s / 2, r.h / 2, s, cleaning ? strong() : soft(),
           cleaning ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY, cleaning ? TEXT() : accent(),
           [e]() { onx_controls::press(e); });
    return r.w - pad - s;
  }
  if (domain == "lock" && (d->code_format.empty() || d->code_format == "None")) {
    const bool unlocked = shown_state(e, d->state) != "locked";
    const int x = r.w - pad - P(54);
    toggle(c, x, (r.h - th) / 2, unlocked, [e]() { onx_controls::press(e); });
    return x;
  }
  return r.w - pad;
}
// One line: icon, name, value (with a smaller unit) on the right.
inline void card_row(lv_obj_t *c, const Tile &t, const EntityDetailState *d) {
  const Rect r = t.r;
  const int pad = P(14), is = P(24);
  const int right = row_control(c, t, d, pad);
  if (right < r.w - pad) {
    // A control instead of the reading: the name and its state beside the icon.
    icon(c, t.entity, pad + is / 2, r.h / 2, is, t.on ? accent() : SOFT_TEXT());
    const int nx = pad + is + P(12), nw = right - P(10) - nx;
    const std::string state = t.context.empty() ? t.value : t.context;
    if (r.h >= line(fonts.body) + line(fonts.small) + P(8) && !state.empty()) {
      const int y = (r.h - line(fonts.body) - line(fonts.small)) / 2;
      text(c, t.name, fonts.body, TEXT(), nx, y, nw);
      text(c, state, fonts.small, MUTED(), nx, y + line(fonts.body), nw);
    } else {
      text(c, t.name, fonts.body, TEXT(), nx, (r.h - line(fonts.body)) / 2, nw);
    }
    return;
  }
  const bool binary = has_prefix(t.entity, "binary_sensor.");
  icon(c, t.entity, pad + is / 2, r.h / 2, is, accent());
  const bool numeric = d != nullptr && d->available && std::isfinite(num(d->state));
  const std::string value = numeric ? onx_detail_view::compact(d->state) : t.value;
  const std::string unit = numeric ? d->unit : "";
  const int uw = unit.empty() ? 0 : text_width(unit, fonts.small) + P(4);
  const lv_font_t *vf = fonts.body;
  const int vw = std::min(text_width(value, vf) + 1, r.w / 2);
  const int vx = r.w - pad - uw - vw, nx = pad + is + P(12);
  const int vy = (r.h - line(vf)) / 2;
  // A sparkline (numeric with history) or how long ago it changed (binary).
  int name_end = vx - P(8);
  const int sw = std::min(P(64), r.w / 5), sh = std::min(P(22), r.h - P(12));
  if (numeric && name_end - nx - sw - P(8) >= P(64)) {
    if (chart(c, name_end - sw, (r.h - sh) / 2, sw, sh, t.entity, card_history(t.entity), accent(), false)) name_end -= sw + P(10);
  } else if (binary && d != nullptr && d->available) {
    const auto *ch = changes_for(t.entity);
    const time_t now = std::time(nullptr);
    if (ch != nullptr && !ch->items.empty()) {
      const std::string ago = onx_sensor_view::relative(ch->items.front().at, now);
      const int aw = text_width(ago, fonts.small) + 1;
      if (name_end - nx - aw - P(8) >= P(64)) {
        text(c, ago, fonts.small, MUTED(), name_end - aw, (r.h - line(fonts.small)) / 2, aw);
        name_end -= aw + P(10);
      }
    }
  }
  // The secondary sensor's text (e.g. "Resets in 2 hours") under the name
  // when the row has room for two lines.
  if (!t.context.empty() && r.h >= 2 * line(fonts.small) + P(8)) {
    const int ny = (r.h - 2 * line(fonts.small)) / 2;
    text(c, t.name, fonts.small, SOFT_TEXT(), nx, ny, name_end - nx);
    text(c, t.context, fonts.small, MUTED(), nx, ny + line(fonts.small), name_end - nx);
  } else {
    text(c, t.name, fonts.small, SOFT_TEXT(), nx, (r.h - line(fonts.small)) / 2, name_end - nx);
  }
  text(c, value, vf, binary ? accent() : TEXT(), vx, vy, vw);
  if (uw) text(c, unit, fonts.small, MUTED(), vx + vw + P(4), vy + line(vf) - line(fonts.small) - P(2), uw);
}

// The carousel's Settings item, drawn like the other main pages (same
// background, time row and card) instead of the base layout's round page. A tap
// falls through to the shared press action, which opens the settings menu.
constexpr const char *SETTINGS = "[Local] Settings";
inline const void *settings_icon = nullptr;
// A stack chip: the entity's icon and its reading in a pill; the name joins
// when the chip is wide. Binary sensors that are on (open, detected) and
// entities that are on take the accent so alerts stand out. A tap opens the
// detail like any card.
inline void card_chip(lv_obj_t *c, const Tile &t, const EntityDetailState *d) {
  const Rect r = t.r;
  const bool available = d != nullptr && d->available;
  const bool numeric = available && std::isfinite(num(d->state));
  const bool lit = available && t.on;
  if (lit) lv_obj_set_style_bg_color(c, lv_color_hex(soft()), 0);
  const int is = std::min(P(18), r.h - P(16)), px = std::min(P(10), r.h / 4);
  std::string value = !available ? std::string("—") : numeric ? onx_detail_view::compact(d->state) : t.value;
  if (value.empty()) value = t.context.empty() ? "—" : t.context;
  // A long reading falls back to the tile's short state (grid_badge), then to
  // whole numbers.
  const int room = r.w - 2 * px - is - P(6);
  if (available && text_width(value, fonts.body) > room && !t.badge.empty() && !numeric) value = t.badge;
  if (numeric && text_width(value, fonts.body) > room) value = onx_detail_view::compact(std::to_string(int(std::lround(num(d->state)))));
  // A text state that does not fit: the icon alone, centred (the accent still
  // marks "on"); the detail has the words.
  if (!numeric && available && text_width(value, fonts.body) > room) {
    icon(c, t.entity, r.w / 2, r.h / 2, is, lit ? accent() : SOFT_TEXT());
    return;
  }
  icon(c, t.entity, px + is / 2, r.h / 2, is, lit ? accent() : available ? SOFT_TEXT() : MUTED());
  // The reading comes first: the unit goes when both do not fit.
  const int x = px + is + P(6), w = r.w - x - px;
  std::string unit = numeric ? d->unit : "";
  int uw = unit.empty() ? 0 : text_width(unit, fonts.small) + P(2);
  if (text_width(value, fonts.body) + 1 + uw > w) { unit.clear(); uw = 0; }
  const int vw = std::min(text_width(value, fonts.body) + 1, w - uw);
  int nx = x;
  if (w - vw - uw >= P(72)) {  // Room for the name before the reading.
    const int nw = w - vw - uw - P(8);
    text(c, t.name, fonts.small, MUTED(), x, (r.h - line(fonts.small)) / 2, nw);
    nx = x + w - vw - uw;
  }
  const int vy = (r.h - line(fonts.body)) / 2;
  text(c, value, fonts.body, lit ? accent() : TEXT(), nx, vy, vw);
  if (uw) text(c, unit, fonts.small, MUTED(), nx + vw + P(2), vy + line(fonts.body) - line(fonts.small) - P(1), uw);
}
inline void card_settings(lv_obj_t *c, const Tile &t) {
  const Rect r = t.r;
  const int is = std::min(P(96), r.h / 3), title_h = line(fonts.title), gap = P(14);
  const int block = is + gap + title_h + P(4) + line(fonts.small);
  int y = std::max(P(14), (r.h - block) / 2);
  if (settings_icon != nullptr) {
    lv_obj_t *img = lv_image_create(c);
    lv_image_set_src(img, settings_icon);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    lv_image_header_t h{};
    if (lv_image_decoder_get_info(settings_icon, &h) == LV_RESULT_OK && h.w && int(h.w) != is) {
      lv_image_set_pivot(img, h.w / 2, h.h / 2);
      lv_image_set_scale(img, is * 256 / h.w);
    }
    lv_obj_set_style_image_recolor(img, lv_color_hex(accent()), 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_align(img, LV_ALIGN_TOP_MID, 0, y + (is - int(h.h)) / 2);
  }
  y += is + gap;
  text(c, "Settings", fonts.title, TEXT(), 0, y, r.w, LV_TEXT_ALIGN_CENTER);
  y += title_h + P(4);
  text(c, "Wi-Fi, display and Home Assistant", fonts.small, MUTED(), P(14), y, r.w - P(28), LV_TEXT_ALIGN_CENTER);
}

struct GridKeyLayout {
  int rad{0}, px{0}, py{0}, lh{0};
  int icon_cx{0}, icon_cy{0}, icon_size{0};
  int label_x{0}, label_w{0}, label_y{0}, badge_y{0}, max_lines{1};
  bool show_badge{false}, show_dot{false}, horizontal{false};
  std::vector<std::string> lines;
};

inline GridKeyLayout layout_grid_key(const Tile &t) {
  GridKeyLayout l{};
  const Rect &r = t.r;
  l.rad = key_radius(r);
  l.show_dot = t.on && r.w >= P(56) && r.h >= P(40) && !(t.icon_only && t.artwork);
  if (t.icon_only) {
    l.icon_cx = r.w / 2;
    l.icon_cy = r.h / 2;
    l.icon_size = t.artwork ? std::max(r.w, r.h) : std::min(P(64), std::min(r.w, r.h) * 64 / 100);
    return l;
  }
  l.px = r.w < P(76) ? P(5) : P(8);
  l.py = r.h < P(68) ? P(4) : P(7);
  l.lh = fonts.small ? line(fonts.small) : 16;
  const int ih = std::max(1, r.h - 2 * l.py);
  // Wide, shallow landscape tiles (e.g. 2.8" landscape 4x4) switch to icon-left + label-right.
  if (r.h < l.lh + P(24) && r.w >= r.h * 3 / 2) {
    l.horizontal = true;
    l.px = P(6);
    l.icon_size = std::clamp(r.h - P(12), P(14), P(24));
    const int gap = P(5);
    l.icon_cx = l.px + l.icon_size / 2;
    l.icon_cy = r.h / 2;
    l.label_x = l.px + l.icon_size + gap;
    const int right_pad = l.show_dot ? P(10) : l.px;
    l.label_w = std::max(1, r.w - l.label_x - right_pad);
    l.max_lines = 1;
    l.lines = onx_grid_cache::wrap_lines(t.name, fonts.small, l.label_w, 1);
    l.label_y = (r.h - l.lh) / 2;
    return l;
  }
  l.label_x = l.px;
  l.label_w = std::max(1, r.w - 2 * l.px);
  const bool tall_badge = r.w >= P(108) && r.h >= P(122);
  const bool wide_badge = r.w >= P(140) && r.h >= P(92);
  l.show_badge = !t.badge.empty() && (tall_badge || wide_badge);
  const int badge_h = l.show_badge ? l.lh : 0;
  l.max_lines = (r.h >= P(82) && ih >= l.lh * 2 + badge_h + (l.show_badge ? P(38) : P(24))) ? 2 : 1;
  l.lines = onx_grid_cache::wrap_lines(t.name, fonts.small, l.label_w, l.max_lines);
  const int nlines = std::max<int>(1, int(l.lines.size()));
  const int text_h = nlines * l.lh + badge_h;
  const int gap = r.h < P(68) ? P(3) : r.h < P(110) ? P(5) : P(8);
  const int max_icon_h = std::max(P(18), ih - text_h - gap);
  l.icon_size = std::min({P(44), std::min(l.label_w, r.h) * 44 / 100, max_icon_h});
  const int block_h = l.icon_size + gap + text_h;
  const int nudge = r.h >= P(100) ? P(2) : 0;
  const int top_y = std::max(l.py, (r.h - block_h) / 2 - nudge);
  l.icon_cx = r.w / 2;
  l.icon_cy = top_y + l.icon_size / 2;
  l.label_y = top_y + l.icon_size + gap;
  l.badge_y = l.label_y + nlines * l.lh;
  return l;
}

inline std::string icon_src_token(const void *src) {
  if (!src) return "0";
  if (lv_image_src_get_type(src) == LV_IMAGE_SRC_FILE) return std::string("F:") + static_cast<const char *>(src);
  return std::to_string(reinterpret_cast<uintptr_t>(src));
}

inline void sync_grid_overlay(size_t idx, const Tile &t, const void *src, bool ready) {
  if (idx >= grid_overlay_objs.size()) return;
  if (grid_overlay_objs[idx]) {
    lv_anim_delete(grid_overlay_objs[idx], nullptr);
    lv_obj_delete(grid_overlay_objs[idx]);
    grid_overlay_objs[idx] = nullptr;
  }
  if (!root || t.entity.empty()) return;
  const auto kl = layout_grid_key(t);
  const bool anim_icon = t.on && t.effect != 0 && !t.artwork && src != nullptr;
  const bool emoji_label = !t.icon_only && onx_grid_cache::needs_emoji_overlay(t.name);
  if (!anim_icon && !emoji_label) return;
  const uint32_t icon_rgb = t.on ? accent() : ready ? TEXT() : MUTED();
  const uint32_t label_rgb = t.on ? TEXT() : ready ? SOFT_TEXT() : MUTED();
  lv_obj_t *holder = lv_obj_create(root);
  lv_obj_remove_flag(holder, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_pos(holder, t.r.x, t.r.y);
  lv_obj_set_size(holder, t.r.w, t.r.h);
  lv_obj_set_style_bg_opa(holder, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(holder, 0, 0);
  lv_obj_set_style_pad_all(holder, 0, 0);
  grid_overlay_objs[idx] = holder;
  if (anim_icon) {
    lv_obj_t *img = lv_image_create(holder);
    lv_obj_remove_flag(img, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_style_image_recolor(img, lv_color_hex(icon_rgb), 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    if (scale_icon(img, src, kl.icon_size)) {
      const auto *sd = &static_cast<ScaledIcon *>(lv_obj_get_user_data(img))->dsc;
      lv_obj_set_pos(img, kl.icon_cx - int(sd->header.w) / 2, kl.icon_cy - int(sd->header.h) / 2);
    } else {
      lv_image_set_src(img, src);
      lv_image_header_t h{};
      if (lv_image_decoder_get_info(src, &h) == LV_RESULT_OK && h.w && h.h) {
        lv_image_set_pivot(img, h.w / 2, h.h / 2);
        lv_image_set_scale(img, std::max<int>(16, kl.icon_size * 256 / std::max<int>(h.w, h.h)));
        lv_obj_set_pos(img, kl.icon_cx - int(h.w) / 2, kl.icon_cy - int(h.h) / 2);
      }
    }
    animate_icon(img, t.effect);
  }
  if (emoji_label) {
    text(holder, t.name, fonts.small, label_rgb, kl.label_x, kl.label_y, kl.label_w,
         kl.horizontal ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, kl.max_lines);
  }
}

inline void rasterize_grid_tile(onx_grid_cache::GridPage &page, size_t idx, const Tile &t, const void *src, bool ready) {
  using namespace onx_grid_cache;
  const int bw = page.w, bh = page.h;
  const int bx = t.r.x, by = t.r.y - page.top_y;
  fill_rect(page.pixels, bw, bh, bx, by, t.r.w, t.r.h, pack565(BG()));
  const auto kl = layout_grid_key(t);
  if (t.entity.empty()) {
    draw_rounded_box(page.pixels, bw, bh, bx, by, t.r.w, t.r.h, kl.rad,
                     onx_display_settings::mix(CARD(), BG(), 45), 255, 0, 0);
    return;
  }
  const uint32_t fill_rgb = t.on ? soft() : ready ? CONTROL() : CARD();
  const int border_w = t.on ? std::max(2, P(2)) : ready ? 1 : 0;
  const uint32_t border_rgb = t.on ? onx_display_settings::mix(accent(), BG(), 68)
                                   : onx_display_settings::mix(TEXT(), BG(), 18);
  const uint32_t icon_rgb = t.on ? accent() : ready ? TEXT() : MUTED();
  const uint32_t label_rgb = t.on ? TEXT() : ready ? SOFT_TEXT() : MUTED();

  if (t.icon_only && t.artwork) {
    draw_rounded_box(page.pixels, bw, bh, bx, by, t.r.w, t.r.h, kl.rad, fill_rgb, 255, 0, 0);
    if (src && lv_image_src_get_type(src) == LV_IMAGE_SRC_VARIABLE) {
      const int side = std::max(t.r.w, t.r.h);
      blit_rgb565_scaled(page.pixels, bw, bh, static_cast<const lv_image_dsc_t *>(src),
                         bx + t.r.w / 2, by + t.r.h / 2, side, side, bx, by, t.r.w, t.r.h, kl.rad);
    }
    if (t.on) {
      draw_rounded_box(page.pixels, bw, bh, bx, by, t.r.w, t.r.h, kl.rad, 0, 0, border_w, accent());
    }
    return;
  }

  draw_rounded_box(page.pixels, bw, bh, bx, by, t.r.w, t.r.h, kl.rad, fill_rgb, 255, border_w, border_rgb);
  if (kl.show_dot) {
    const float dr = std::max(2.5f, P(6) / 2.0f);
    const int ins = std::min(t.r.w, t.r.h) < P(72) ? P(8) : P(10);
    draw_disc(page.pixels, bw, bh, float(bx + t.r.w - ins), float(by + ins), dr, accent());
  }

  const Clip tile_clip{bx + border_w, by + border_w, bx + t.r.w - border_w, by + t.r.h - border_w};
  const bool anim_icon = t.on && t.effect != 0 && !t.artwork && src != nullptr;
  if (!anim_icon && src != nullptr) {
    if (t.artwork && lv_image_src_get_type(src) == LV_IMAGE_SRC_VARIABLE) {
      const int ar = std::min(P(8), kl.icon_size / 5);
      const int ax = bx + kl.icon_cx - kl.icon_size / 2, ay = by + kl.icon_cy - kl.icon_size / 2;
      blit_rgb565_scaled(page.pixels, bw, bh, static_cast<const lv_image_dsc_t *>(src),
                         bx + kl.icon_cx, by + kl.icon_cy, kl.icon_size, kl.icon_size,
                         ax, ay, kl.icon_size, kl.icon_size, ar);
    } else if (const auto *sa = get_scaled_alpha(src, kl.icon_size)) {
      blend_alpha_icon(page.pixels, bw, bh, *sa, bx + kl.icon_cx, by + kl.icon_cy, icon_rgb, tile_clip);
    }
  }

  if (!t.icon_only && fonts.small) {
    if (!needs_emoji_overlay(t.name)) {
      if (kl.horizontal) {
        draw_text_aligned(page.pixels, bw, bh, kl.lines, fonts.small, label_rgb,
                          bx + kl.label_x, by + kl.label_y, kl.label_w, false, tile_clip);
      } else {
        draw_text_centered(page.pixels, bw, bh, kl.lines, fonts.small, label_rgb,
                           bx + kl.label_x, by + kl.label_y, kl.label_w, tile_clip);
      }
    }
    if (kl.show_badge) {
      draw_text_centered(page.pixels, bw, bh, {t.badge}, fonts.small, t.on ? accent() : MUTED(),
                         bx + kl.label_x, by + kl.badge_y, kl.label_w, tile_clip);
    }
  }
  (void)idx;
}

inline const void *resolve_grid_icon(size_t idx, const Tile &t) {
  if (t.entity.empty() || idx >= icon_pool.size()) return nullptr;
  if (t.artwork) {
    if (auto *art = onx_grid_cache::get_artwork(t.artwork))
      return art->frame(t.on, animation_elapsed(t.entity));
    return nullptr;
  }
  if (!bind_icon) return nullptr;
  auto *&img = icon_pool[idx];
  if (img == nullptr) {
    img = lv_image_create(lv_layer_top());
    lv_obj_remove_flag(img, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  }
  if (lv_obj_get_parent(img) != lv_layer_top()) lv_obj_set_parent(img, lv_layer_top());
  lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
  bind_icon(img, t.entity);
  return lv_image_get_src(img);
}

inline uint64_t grid_tile_sig(const Tile &t, const void *src, bool ready) {
  uint64_t h = 14695981039346656037ull;
  auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
  auto mix_str = [&](const std::string &s) {
    mix(0xFFu);
    for (unsigned char c : s) mix(c);
  };
  mix_str(t.entity);
  mix_str(t.name);
  mix_str(t.badge);
  mix(uint64_t(t.on) | (uint64_t(ready) << 1) | (uint64_t(t.icon_only) << 2) | (uint64_t(t.effect) << 4));
  mix(uint64_t(uint16_t(t.r.x)) | (uint64_t(uint16_t(t.r.y)) << 16) |
      (uint64_t(uint16_t(t.r.w)) << 32) | (uint64_t(uint16_t(t.r.h)) << 48));
  mix(t.artwork);
  if (src && lv_image_src_get_type(src) == LV_IMAGE_SRC_FILE) {
    for (const char *p = static_cast<const char *>(src); *p; ++p) mix(static_cast<unsigned char>(*p));
  } else {
    mix(reinterpret_cast<uintptr_t>(src));
  }
  return h ? h : 1ull;
}

inline void refresh_grid_icons() {
  if (!grid_cached_mode || !active_grid_page || !grid_image) return;
  bool any_dirty = false;
  for (size_t i = 0; i < grid_tiles.size() && i < icon_pool.size(); ++i) {
    const auto &t = grid_tiles[i];
    if (t.entity.empty()) continue;
    const void *src = resolve_grid_icon(i, t);
    if (src == grid_bound_srcs[i]) continue;
    const auto *d = custom_logic_entity_sub::entity_detail(t.entity);
    const bool ready = d != nullptr && d->received && d->available;
    rasterize_grid_tile(*active_grid_page, i, t, src, ready);
    sync_grid_overlay(i, t, src, ready);
    grid_bound_srcs[i] = src;
    active_grid_page->tile_sigs[i] = grid_tile_sig(t, src, ready);
    if (!any_dirty) { lv_image_cache_drop(&active_grid_page->dsc); any_dirty = true; }
    lv_area_t area{t.r.x, t.r.y, t.r.x + t.r.w - 1, t.r.y + t.r.h - 1};
    lv_obj_invalidate_area(grid_image, &area);
  }
}

inline void card_vacuum(lv_obj_t *c, const Tile &t, const EntityDetailState &d) {
  const Rect r = t.r;
  const int pad = P(14), cs = P(44), tx = pad + cs + P(12);
  const auto state = onx_controls::effective(d, "state");
  const bool cleaning = state == "cleaning";
  const float bat = num(d.battery_level);
  const bool has_bat = std::isfinite(bat);
  chip_icon(c, t.entity, pad, pad, cs, t.on);
  int bottom = r.h - pad;
  const int bh = P(44), gap = P(8);
  struct Act { const char *symbol; const char *service; const char *target; bool hot; };
  std::vector<Act> btns;
  if (cleaning && (d.features & 4)) btns.push_back({LV_SYMBOL_PAUSE, "vacuum.pause", "paused", true});
  else if (d.features == 0 || (d.features & 8192)) btns.push_back({LV_SYMBOL_PLAY, "vacuum.start", "cleaning", false});
  if (d.features & 8) btns.push_back({LV_SYMBOL_STOP, "vacuum.stop", "idle", cleaning && !(d.features & 4)});
  if (d.features == 0 || (d.features & 16)) btns.push_back({LV_SYMBOL_HOME, "vacuum.return_to_base", "returning", state == "returning"});
  const std::string e = t.entity;
  const bool show_btns = !btns.empty() && bottom - bh >= pad + cs + P(8);
  const int avail_bottom = show_btns ? bottom - bh - P(10) : bottom;
  const int top = pad + cs + P(10);
  const bool show_big = avail_bottom - top >= line(fonts.title);
  Tile head = t;
  if (!show_big) head.context = label(state.empty() ? "idle" : state) + (has_bat ? " · " + pct(bat) : "");
  titled(c, head, tx, pad + P(2), r.w - tx - pad);
  if (show_btns) {
    bottom -= bh;
    const int n = btns.size(), bw = (r.w - 2 * pad - gap * (n - 1)) / n;
    for (int i = 0; i < n; ++i) {
      const auto a = btns[i];
      const int w = i == n - 1 ? r.w - 2 * pad - i * (bw + gap) : bw;
      icon_button(c, pad + i * (bw + gap), bottom, w, bh, P(14), a.hot ? strong() : INSET(), a.symbol, TEXT(),
                  [e, a]() { request(e, a.service, "", a.target, "state"); });
    }
    bottom -= P(10);
  }
  const bool bar = has_bat && bottom - top >= line(fonts.title) + P(18);
  if (bar) bottom -= P(10) + P(8);
  if (show_big) {
    const std::string v = label(state.empty() ? "idle" : state);
    const lv_font_t *vf = bottom - top >= line(fonts.big) ? fit(v, r.w - 2 * pad, {fonts.big, fonts.title}) : fonts.title;
    text(c, v, vf, t.on ? accent() : TEXT(), pad, bottom - line(vf), r.w - 2 * pad);
  }
  if (bar) level(c, pad, bottom + P(8), r.w - 2 * pad, P(10), std::clamp(bat, 0.0f, 100.0f) / 100.0f);
}

inline void card_camera(lv_obj_t *c, const Tile &t, const EntityDetailState &d) {
  const Rect r = t.r;
  const int pad = P(12);
  if (onx_media::main_entity.empty()) onx_media::main_entity = t.entity;
  const void *source = !t.artwork && artwork_source && onx_media::main_entity == t.entity
      ? artwork_source(t.entity) : nullptr;
  const std::string st = label(d.state.empty() ? "idle" : d.state);
  const int sw = std::min(text_width(st, fonts.small) + 1, r.w / 3);
  const int foot_h = line(fonts.body);
  const int fw = r.w - 2 * pad, fh = r.h - 2 * pad - foot_h - P(8);
  if (fh >= P(40)) {
    const ArtBox b = source ? art_box(source, std::min(fw, fh), fw, fh) : ArtBox{fw, fh, true};
    const int ax = pad + (fw - b.w) / 2, ay = pad + (fh - b.h) / 2;
    auto *frame = box(c, ax, ay, b.w, b.h, INSET(), P(10));
    lv_obj_set_style_clip_corner(frame, true, 0);
    if (source) place_artwork(frame, source, b, false);
    else icon(c, t.entity, ax + b.w / 2, ay + b.h / 2, std::min({b.w, b.h, P(96)}) * 46 / 100, t.on ? accent() : SOFT_TEXT());
  }
  const int fy = r.h - pad - foot_h;
  text(c, t.name, fit(t.name, r.w - 2 * pad - sw - P(8), {fonts.body, fonts.small}), TEXT(), pad, fy, r.w - 2 * pad - sw - P(8));
  text(c, st, fonts.small, t.on ? accent() : MUTED(), r.w - pad - sw, fy + (foot_h - line(fonts.small)) / 2, sw, LV_TEXT_ALIGN_RIGHT);
}

inline void card_lock(lv_obj_t *c, const Tile &t, const EntityDetailState &d) {
  const Rect r = t.r;
  const int pad = P(14), cs = P(44), tx = pad + cs + P(12);
  const auto state = onx_controls::effective(d, "state");
  const bool locked = state == "locked";
  const bool needs_code = !d.code_format.empty() && d.code_format != "None";
  chip_icon(c, t.entity, pad, pad, cs, t.on);
  int bottom = r.h - pad;
  const int bh = P(44), gap = P(8);
  const bool show_btns = !needs_code && bottom - bh >= pad + cs + P(8);
  const int avail_bottom = show_btns ? bottom - bh - P(10) : bottom;
  const int top = pad + cs + P(10);
  const bool show_big = avail_bottom - top >= line(fonts.title);
  Tile head = t;
  if (!show_big) head.context = label(state.empty() ? "locked" : state);
  titled(c, head, tx, pad + P(2), r.w - tx - pad);
  if (show_btns) {
    bottom -= bh;
    const std::string e = t.entity;
    struct Act { const char *caption; const char *service; const char *target; bool hot; };
    std::vector<Act> btns{{"Lock", "lock.lock", "locked", locked}, {"Unlock", "lock.unlock", "unlocked", !locked}};
    if (d.features & 1) btns.push_back({"Open", "lock.open", "open", state == "open"});
    const int n = btns.size(), bw = (r.w - 2 * pad - gap * (n - 1)) / n;
    for (int i = 0; i < n; ++i) {
      const auto a = btns[i];
      const int w = i == n - 1 ? r.w - 2 * pad - i * (bw + gap) : bw;
      button(c, pad + i * (bw + gap), bottom, w, bh, P(14), a.hot ? strong() : INSET(), a.caption,
             n >= 3 ? fonts.small : fonts.body, TEXT(), [e, a]() { request(e, a.service, "", a.target, "state"); });
    }
    bottom -= P(10);
  }
  if (show_big) {
    const std::string v = label(state.empty() ? "locked" : state);
    const lv_font_t *vf = bottom - top >= line(fonts.big) ? fit(v, r.w - 2 * pad, {fonts.big, fonts.title}) : fonts.title;
    text(c, v, vf, t.on ? accent() : TEXT(), pad, bottom - line(vf), r.w - 2 * pad);
  }
}

inline void card_alarm(lv_obj_t *c, const Tile &t, const EntityDetailState &d) {
  const Rect r = t.r;
  const int pad = P(14), cs = P(44), tx = pad + cs + P(12);
  const auto state = d.state.empty() ? "disarmed" : d.state;
  const bool disarmed = state == "disarmed";
  const bool needs_code = d.code_format == "number" || (!d.code_format.empty() && d.code_format != "None");
  const bool arm_code = needs_code && (d.code_arm_required.empty() || d.code_arm_required == "True" || d.code_arm_required == "true");
  const std::string st = state == "armed_custom_bypass" ? "Armed bypass" : label(state);
  chip_icon(c, t.entity, pad, pad, cs, t.on);
  int bottom = r.h - pad;
  const int bh = P(44), gap = P(8);
  const bool can_act = disarmed ? !arm_code : !needs_code;
  const bool show_btns = can_act && bottom - bh >= pad + cs + P(8);
  const int avail_bottom = show_btns ? bottom - bh - P(10) : bottom;
  const int top = pad + cs + P(10);
  const bool show_big = avail_bottom - top >= line(fonts.title);
  Tile head = t;
  if (!show_big) head.context = st;
  else if (needs_code && !disarmed) head.context = "Tap to disarm";
  titled(c, head, tx, pad + P(2), r.w - tx - pad);
  if (show_btns) {
    bottom -= bh;
    const std::string e = t.entity;
    if (!disarmed) {
      button(c, pad, bottom, r.w - 2 * pad, bh, P(14), strong(), "Disarm", fonts.body, TEXT(),
             [e]() { request(e, "alarm_control_panel.alarm_disarm", "", "disarmed", "state"); });
    } else {
      const int bw = (r.w - 2 * pad - gap) / 2;
      button(c, pad, bottom, bw, bh, P(14), INSET(), "Home", fonts.body, TEXT(),
             [e]() { request(e, "alarm_control_panel.alarm_arm_home", "", "armed_home", "state"); });
      button(c, pad + bw + gap, bottom, r.w - 2 * pad - bw - gap, bh, P(14), INSET(), "Away", fonts.body, TEXT(),
             [e]() { request(e, "alarm_control_panel.alarm_arm_away", "", "armed_away", "state"); });
    }
    bottom -= P(10);
  }
  if (show_big) {
    const lv_font_t *vf = bottom - top >= line(fonts.big) ? fit(st, r.w - 2 * pad, {fonts.big, fonts.title}) : fonts.title;
    text(c, st, vf, t.on ? accent() : TEXT(), pad, bottom - line(vf), r.w - 2 * pad);
  }
}

__attribute__((noinline)) inline void build_card(const Tile &t) {
  const auto *d = custom_logic_entity_sub::entity_detail(t.entity);
  const bool ready = d != nullptr && d->received && d->available;
  if (t.key) {
    const auto kl = layout_grid_key(t);
    if (t.entity.empty()) {
      box(root, t.r.x, t.r.y, t.r.w, t.r.h, onx_display_settings::mix(CARD(), BG(), 45), kl.rad);
      return;
    }
    const uint32_t fill_rgb = t.on ? soft() : ready ? CONTROL() : CARD();
    const int border_w = t.on ? std::max(2, P(2)) : ready ? 1 : 0;
    const uint32_t border_rgb = t.on ? onx_display_settings::mix(accent(), BG(), 68)
                                     : onx_display_settings::mix(TEXT(), BG(), 18);
    lv_obj_t *c = box(root, t.r.x, t.r.y, t.r.w, t.r.h, fill_rgb, kl.rad);
    lv_obj_set_style_clip_corner(c, t.icon_only && t.artwork, 0);
    if (!(t.icon_only && t.artwork) && border_w > 0) {
      lv_obj_set_style_border_width(c, border_w, 0);
      lv_obj_set_style_border_color(c, lv_color_hex(border_rgb), 0);
    }
    const uint32_t icon_rgb = t.on ? accent() : ready ? TEXT() : MUTED();
    const uint32_t label_rgb = t.on ? TEXT() : ready ? SOFT_TEXT() : MUTED();
    if (kl.show_dot) {
      const int ds = std::max(5, P(6)), ins = std::min(t.r.w, t.r.h) < P(72) ? P(8) : P(10);
      box(c, t.r.w - ins - ds / 2, ins - ds / 2, ds, ds, accent(), LV_RADIUS_CIRCLE);
    }
    if (t.icon_only) {
      icon(c, t.entity, kl.icon_cx, kl.icon_cy, kl.icon_size, icon_rgb);
      if (t.on && t.effect && !icon_uses.empty()) animate_icon(shown_icon(icon_uses.back()), t.effect);
      if (t.artwork && t.on) {
        auto *frame = box(c, 0, 0, t.r.w, t.r.h, CARD(), kl.rad, LV_OPA_TRANSP);
        lv_obj_set_style_border_width(frame, border_w, 0);
        lv_obj_set_style_border_color(frame, lv_color_hex(accent()), 0);
      }
      return;
    }
    icon(c, t.entity, kl.icon_cx, kl.icon_cy, kl.icon_size, icon_rgb);
    if (t.on && t.effect && !icon_uses.empty()) animate_icon(shown_icon(icon_uses.back()), t.effect);
    text(c, t.name, fonts.small, label_rgb, kl.label_x, kl.label_y, kl.label_w,
         kl.horizontal ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, kl.max_lines);
    if (kl.show_badge)
      text(c, t.badge, fonts.small, t.on ? accent() : MUTED(), kl.label_x, kl.badge_y, kl.label_w, LV_TEXT_ALIGN_CENTER, 1);
    return;
  }
  if (t.r.w <= 0 || t.r.h <= 0) return;  // An empty stack chip.
  if (t.slot && t.entity.empty()) {
    box(root, t.r.x, t.r.y, t.r.w, t.r.h, onx_display_settings::mix(CARD(), BG(), 45), t.chip ? t.r.h / 2 : t.compact ? P(14) : P(18));
    return;
  }
  lv_obj_t *c = box(root, t.r.x, t.r.y, t.r.w, t.r.h, CARD(), t.chip ? t.r.h / 2 : t.compact ? P(14) : P(18));
  lv_obj_set_style_clip_corner(c, false, 0);
  if (t.chip) { card_chip(c, t, d); return; }
  if (t.compact) { card_row(c, t, d); return; }
  if (t.entity == SETTINGS) card_settings(c, t);
  else if (ready && has_prefix(t.entity, "weather.")) card_weather(c, t, *d);
  else if (ready && has_prefix(t.entity, "light.")) card_light(c, t, *d);
  else if (ready && has_prefix(t.entity, "climate.")) card_climate(c, t, *d);
  else if (ready && has_prefix(t.entity, "media_player.")) card_media(c, t, *d);
  else if (ready && has_prefix(t.entity, "vacuum.")) card_vacuum(c, t, *d);
  else if (ready && has_prefix(t.entity, "camera.")) card_camera(c, t, *d);
  else if (ready && has_prefix(t.entity, "lock.")) card_lock(c, t, *d);
  else if (ready && has_prefix(t.entity, "alarm_control_panel.")) card_alarm(c, t, *d);
  else card_generic(c, t, d);
}

inline void ensure_root(lv_obj_t *parent) {
  if (root != nullptr) {
    if (lv_obj_get_parent(root) != parent) lv_obj_set_parent(root, parent);
    return;
  }
  root = lv_obj_create(parent);
  lv_obj_remove_flag(root, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(root, 0, 0);
  lv_obj_set_style_pad_all(root, 0, 0);
  lv_obj_set_style_radius(root, 0, 0);
}
inline void clear(bool invalidate_root = true);
inline std::string signature(const std::string &e, const EntityDetailState *d, const onx_detail_view::View &v,
                             const std::string &title);
inline std::string built_main_structure;
inline std::vector<std::string> built_tile_sigs;
inline std::string main_page_key;
inline void build_main_cards(const std::vector<Tile> &tiles) {
  card_patches.clear();
  // One cover or camera snapshot downloads at a time: prefer a playing media
  // card, then a camera card, then an idle media card. Swiping pages does not
  // clear the choice, so drop one that is not a card on this page.
  if (!onx_media::main_entity.empty() &&
      std::none_of(tiles.begin(), tiles.end(), [](const Tile &t) {
        return !t.chip && !t.compact && !t.key && !t.artwork && t.entity == onx_media::main_entity;
      }))
    onx_media::main_entity.clear();
  if (onx_media::main_entity.empty()) {
    std::string first_camera, first_media;
    for (const auto &t : tiles) {
      if (t.chip || t.compact || t.key || t.artwork) continue;
      const bool media = has_prefix(t.entity, "media_player."), cam = has_prefix(t.entity, "camera.");
      if (!media && !cam) continue;
      const auto *d = custom_logic_entity_sub::entity_detail(t.entity);
      if (d == nullptr || !d->received || !d->available) continue;
      if (cam) { if (first_camera.empty()) first_camera = t.entity; continue; }
      if (first_media.empty()) first_media = t.entity;
      if (onx_media::has_playback(d->state)) { onx_media::main_entity = t.entity; break; }
    }
    if (onx_media::main_entity.empty()) onx_media::main_entity = !first_camera.empty() ? first_camera : first_media;
  }
  for (const auto &t : tiles) {
    card_artwork = t.artwork;
    card_on = t.on;
    card_patch = nullptr;
    build_card(t);
    card_patches.push_back(std::move(card_patch));
  }
  card_patch = nullptr;
  card_artwork = 0;
}

inline bool render_grid_cached(const std::vector<Tile> &tiles) {
  if (tiles.empty() || tiles.size() > 16) return false;
  int min_y = H, max_y = 0;
  for (const auto &t : tiles) {
    min_y = std::min(min_y, t.r.y);
    max_y = std::max(max_y, t.r.bottom());
  }
  min_y = std::clamp(min_y, 0, H);
  max_y = std::clamp(max_y, min_y + 1, H);
  const int buf_h = max_y - min_y;
  uint32_t pal_sig = 2166136261u;
  auto mix32 = [&](uint32_t v) { pal_sig ^= v; pal_sig *= 16777619u; };
  mix32(BG()); mix32(accent()); mix32(uint32_t(W)); mix32(uint32_t(H));
  const std::string cache_key = !main_page_key.empty() ? main_page_key : tiles[0].entity;
  bool full_redraw = false;
  auto *page = onx_grid_cache::acquire_page(cache_key, W, buf_h, min_y, pal_sig, int(tiles.size()), full_redraw);
  if (!page) return false;

  const bool mounting = !grid_cached_mode || active_grid_page != page || grid_image == nullptr;
  if (mounting) {
    clear(!main_mode);
    main_mode = true;
    grid_cached_mode = true;
    active_grid_page = page;
    grid_image = lv_image_create(root);
    lv_obj_remove_flag(grid_image, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_pos(grid_image, 0, min_y);
    lv_obj_set_size(grid_image, W, buf_h);
    lv_image_set_src(grid_image, &page->dsc);
  }
  grid_tiles = tiles;
  if (full_redraw) {
    onx_grid_cache::fill_rect(page->pixels, page->w, page->h, 0, 0, page->w, page->h, onx_grid_cache::pack565(BG()));
  }

  bool any_tile_dirty = false;
  for (size_t i = 0; i < tiles.size(); ++i) {
    const auto &t = tiles[i];
    const auto *d = custom_logic_entity_sub::entity_detail(t.entity);
    const bool ready = d != nullptr && d->received && d->available;
    const void *src = resolve_grid_icon(i, t);
    const uint64_t tsig = grid_tile_sig(t, src, ready);
    grid_bound_srcs[i] = src;
    if (full_redraw || page->tile_sigs[i] != tsig) {
      rasterize_grid_tile(*page, i, t, src, ready);
      sync_grid_overlay(i, t, src, ready);
      page->tile_sigs[i] = tsig;
      if (!full_redraw && !mounting) {
        if (!any_tile_dirty) { lv_image_cache_drop(&page->dsc); any_tile_dirty = true; }
        lv_area_t area{t.r.x, t.r.y, t.r.x + t.r.w - 1, t.r.y + t.r.h - 1};
        lv_obj_invalidate_area(grid_image, &area);
      } else {
        any_tile_dirty = true;
      }
    } else if (mounting) {
      sync_grid_overlay(i, t, src, ready);
    }
  }
  page->valid = true;
  if (full_redraw || (mounting && any_tile_dirty)) {
    lv_image_cache_drop(&page->dsc);
    lv_image_set_src(grid_image, &page->dsc);
    lv_obj_invalidate(grid_image);
  }
  return true;
}

inline void compute_main_signatures(const std::vector<Tile> &tiles, std::string &sig, std::string &structure,
                                    std::vector<std::string> &tile_sigs) {
  std::string head = "main|" + std::to_string(W) + 'x' + std::to_string(H) + '|' + std::to_string(accent()) + '|' + std::to_string(BG());
  sig = head;
  structure = head;
  tile_sigs.clear();
  tile_sigs.reserve(tiles.size());
  const time_t now = std::time(nullptr);
  for (const auto &t : tiles) {
    const auto *d = custom_logic_entity_sub::entity_detail(t.entity);
    onx_detail_view::View v;
    v.value.text = t.value; v.context.text = t.context; v.name.text = t.name; v.value_ring = t.ring;
    const std::string place = std::to_string(t.r.x) + ',' + std::to_string(t.r.y) + ',' + std::to_string(t.r.w) + ',' +
                              std::to_string(t.r.h) + ',' + std::to_string(t.compact) + ',' + std::to_string(t.key) + ',' +
                              std::to_string(t.icon_only) + ',' + std::to_string(t.effect) + ',' + std::to_string(t.artwork) + ',' +
                              std::to_string(t.chip) + std::to_string(t.slot);
    std::string full = "#" + place + ',' + std::to_string(t.on) + ',' + t.badge + ',' + signature(t.entity, d, v, t.name);
    // These cards show relative event times independently of the header clock.
    if (!t.key && (runnable_entity(t.entity) || has_prefix(t.entity, "binary_sensor.")))
      full += '|' + std::to_string(now > 1600000000 ? now / 60 : 0);
    // Light and switch cards change on/off, brightness and their context line
    // in place (card patches); everything else in the signature rebuilds.
    std::string shape = full;
    const bool ready = d != nullptr && d->received && d->available;
    if (ready && !t.key && !t.compact && !t.chip && !t.artwork) {
      const auto domain = t.entity.substr(0, t.entity.find('.'));
      if (domain == "light" && light_state) {
        const auto st = light_state(t.entity);
        shape = "#L" + place + '|' + t.entity + '|' + t.name + '|' + std::to_string(st.supports_brightness()) +
                std::to_string(st.supports_color_temperature());
      } else if (domain == "switch" || domain == "input_boolean" || domain == "automation" || domain == "fan" || domain == "siren") {
        shape = "#S" + place + '|' + t.entity + '|' + t.name + '|' + t.value + '|' + d->unit + '|' + std::to_string(t.ring) + '|' +
                std::to_string(t.context.empty());
      }
      // Charts are not patched in place: new history rebuilds the card.
      const int si = onx_sensor_view::index(t.entity);
      if (si >= 0 && onx_sensor_view::configs[si].graph) {
        const auto &st = onx_sensor_view::states[si];
        shape += "|g" + std::to_string(st.history(now).size()) + ',' + std::to_string(st.range_seconds) + ',' +
                 std::to_string(st.requested) + std::to_string(st.pending) + std::to_string(st.failed);
      }
    }
    sig += full;
    structure += shape;
    tile_sigs.push_back(std::move(full));
  }
}

// Draws or keeps the cards for the current page.
inline void render_main(lv_obj_t *parent, const std::vector<Tile> &tiles, int width, int height) {
  ensure_root(parent);
  W = width; H = height;
  lv_obj_set_pos(root, 0, 0);
  lv_obj_set_size(root, W, H);
  lv_obj_remove_flag(root, LV_OBJ_FLAG_HIDDEN);
  active = true;
  if (dragging) return;
  if (!tiles.empty() && tiles[0].key && render_grid_cached(tiles)) {
    built_signature.clear();
    return;
  }
  std::string sig, structure;
  std::vector<std::string> tile_sigs;
  compute_main_signatures(tiles, sig, structure, tile_sigs);
  if (sig == built_signature && main_mode && !grid_cached_mode) { refresh_icons(); tick_media_progress(); return; }
  if (main_mode && !grid_cached_mode && structure == built_main_structure && card_patches.size() == tiles.size() &&
      built_tile_sigs.size() == tiles.size()) {
    bool patched = true;
    for (size_t i = 0; i < tiles.size() && patched; ++i)
      if (tile_sigs[i] != built_tile_sigs[i]) patched = card_patches[i] && card_patches[i](tiles[i]);
    if (patched) {
      built_signature = sig;
      built_tile_sigs = std::move(tile_sigs);
      refresh_icons();
      tick_media_progress();
      return;
    }
  }
  clear(!main_mode);
  built_signature = sig;
  built_main_structure = structure;
  built_tile_sigs = std::move(tile_sigs);
  main_mode = true;
  build_main_cards(tiles);
  tick_media_progress();
}

// ------------------------------------------------------------ dispatch
inline bool supports(const std::string &e) {
  if (custom_logic_entity_sub::is_timer_detail(e)) return true;
  for (const char *p : {"light.", "climate.", "fan.", "cover.", "switch.", "input_boolean.", "automation.", "scene.",
                        "script.", "button.", "input_button.", "sensor.", "binary_sensor.", "number.", "input_number.",
                        "select.", "input_select.", "weather.", "media_player.", "siren.", "event.", "camera.", "vacuum.", "lock.", "alarm_control_panel."})
    if (has_prefix(e, p)) return true;
  return false;
}
inline std::string signature(const std::string &e, const EntityDetailState *d, const onx_detail_view::View &v,
                             const std::string &title) {
  std::string s = e + '|' + title + '|' + std::to_string(W) + 'x' + std::to_string(H) + '|' + std::to_string(accent()) + '|' + std::to_string(BG());
  if (d == nullptr) return s;
  using onx_controls::effective;
  s += '|' + d->state + '|' + effective(*d, "state") + '|' + d->unit + '|' + d->current + '|' + effective(*d, "temperature") +
       '|' + d->low + '|' + d->high + '|' + d->modes + '|' + d->brightness + '|' + d->position + '|' +
       effective(*d, "percentage") + '|' + effective(*d, "oscillating") + '|' + effective(*d, "direction") + '|' +
       effective(*d, "preset_mode") + '|' + effective(*d, "fan_mode") + '|' + effective(*d, "swing_mode") + '|' + effective(*d, "swing_horizontal_mode") + '|' + d->presets +
       '|' + d->options + '|' + d->humidity + '|' + d->apparent_temperature + '|' + d->wind_speed + '|' +
       std::to_string(d->features) + '|' + std::to_string(d->available) + '|' + std::to_string(d->received) + '|' +
       std::to_string(d->forecast_count) + '|' + std::to_string(d->forecast_waiting(esphome::millis())) + '|' + d->timer_duration;
  for (int i = 0; i < d->forecast_count; ++i) s += '|' + d->forecast[i].date + d->forecast[i].high + d->forecast[i].low;
  s += '|' + d->media_title + '|' + d->media_artist + '|' + d->picture + '|' + onx_media::ready_url +
       '|' + d->battery_level + '|' + d->status + '|' + d->motion_detection + '|' + camera_ptz_entity(e) +
       '|' + d->changed_by + '|' + d->code_format + '|' + d->code_arm_required + '|' + (pin_entity == e ? std::to_string(pin_code.size()) : "");
  if (has_prefix(e, "media_player.")) s += '|' + std::to_string(onx_lyrics::store().version);
  s += '|' + v.value.text + '|' + v.context.text + '|' + v.name.text + '|' + std::to_string(v.value_ring);
  if (has_prefix(e, "light.") && light_state) {
    const auto l = light_state(e);
    s += '|' + std::to_string(l.is_on) + ',' + std::to_string(l.brightness_pct) + ',' + std::to_string(l.color_temperature_kelvin) +
         ',' + std::to_string(light_mode ? light_mode() : 0) + ',' + fmt(d->light_hue, 0) + ',' + fmt(d->light_saturation, 0) + ',' +
         std::to_string(d->light_temperature);
  }
  const int i = onx_sensor_view::index(e);
  if (i >= 0) {
    const auto &st = onx_sensor_view::states[i];
    s += '|' + st.value + st.unit + std::to_string(st.history(std::time(nullptr)).size()) + std::to_string(st.range_seconds) + std::to_string(st.waiting(esphome::millis())) + std::to_string(st.failed) + std::to_string(st.requested);
  }
  if (const auto *c = changes_for(e)) s += '|' + std::to_string(c->items.size()) + ',' + std::to_string(std::time(nullptr) / 60);
  if (pending.entity == e) s += '|' + std::to_string(pending_value(e, pending.key));
  s += '|' + std::to_string(esphome::millis() - ran_at < 1500);
  s += '|' + std::to_string(onx_playlist::browser.open && onx_playlist::browser.entity == e);
  if (has_prefix(e,"cover.")) s += "|" + onx_cover_presets::encode(onx_entity_config::cover_presets(e)) + std::to_string(onx_entity_config::cover_presets(e).enabled);
  if (shortcut) s += '|' + shortcut(e);
  if (custom_logic_entity_sub::is_timer_detail(e)) s += '|' + timer_signature();
  return s;
}

inline void detach_graph() {
  onx_sensor_graph::panel_owned = false;
  auto *canvas = onx_sensor_graph::canvas;
  if (canvas == nullptr || root == nullptr) return;
  for (lv_obj_t *p = lv_obj_get_parent(canvas); p != nullptr; p = lv_obj_get_parent(p)) {
    if (p != root) continue;
    lv_obj_set_parent(canvas, lv_layer_top());
    lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);
    return;
  }
}
inline void clear(bool invalidate_root) {
  pressed_card = nullptr;
  grid_press_overlay = nullptr;
  grid_image = nullptr;
  active_grid_page = nullptr;
  grid_cached_mode = false;
  grid_tiles.clear();
  grid_bound_srcs.fill(nullptr);
  grid_overlay_objs.fill(nullptr);
  patch = nullptr;
  card_patches.clear();
  built_main_structure.clear();
  built_tile_sigs.clear();
  built_structure.clear();
  timer_visuals.clear();
  timer_screen_active = false;
  detach_graph();
  detach_icons();
  detach_artwork();
  onx_media::main_entity.clear();
  main_mode = false;
  auto &t = onx_panel_ui::touch_targets;
  for (auto *o : targets) t.erase(std::remove(t.begin(), t.end(), o), t.end());
  targets.clear();
  media_progress.clear();
  lyric_view = LyricView{};
  if (root != nullptr) {
    if (invalidate_root) {
      lv_obj_clean(root);
    } else {
      // The main-page root is transparent and never scrolls. Deleting each
      // child invalidates its own bounds; lv_obj_clean dirties the whole panel.
      while (auto *child = lv_obj_get_child(root, -1)) lv_obj_delete(child);
    }
  }
  actions.clear();
  charts.clear();
  dragging = false;
  built_signature.clear();
}
inline void hide() {
  timers.focus.clear();
  if (!active) return;
  active = false;
  clear();
  if (root != nullptr) lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
}

// Save recently visited non-grid main pages (action grids keep their flat
// RGB565 framebuffers in onx_grid_cache::grid_pages instead). Media uses
// shared mutable artwork storage so it keeps the regular rebuild path.
struct RetainedPage {
  std::string key, signature, main_structure;
  std::vector<std::string> tile_sigs;
  std::vector<CardPatch> patches;
  lv_obj_t *root{nullptr};
  decltype(targets) touch_targets;
  decltype(actions) callbacks;
  decltype(charts) chart_data;
  decltype(icon_pool) icons{};
  decltype(icon_uses) image_uses;
};
inline std::array<RetainedPage, 6> retained_pages;
inline size_t next_retained_page = 0;

inline void delete_page_widgets(lv_obj_t *page_root, const decltype(icon_pool) &icons) {
  for (auto *icon : icons) {
    if (!icon) continue;
    lv_obj_delete(icon);
    if (unbind_icon) unbind_icon(icon);
  }
  if (page_root) lv_obj_delete(page_root);
}

inline void leave_main_page() {
  if (main_page_key.empty()) return;
  const bool retain = main_mode && !grid_cached_mode && root && !dragging && onx_media::main_entity.empty();
  if (!retain) {
    main_page_key.clear();
    return;
  }
  lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
  auto &live_targets = onx_panel_ui::touch_targets;
  for (auto *target : targets)
    live_targets.erase(std::remove(live_targets.begin(), live_targets.end(), target), live_targets.end());
  for (auto *icon : icon_pool)
    if (icon) lv_anim_delete(icon, nullptr);
  auto &slot = retained_pages[next_retained_page];
  next_retained_page = (next_retained_page + 1) % retained_pages.size();
  delete_page_widgets(slot.root, slot.icons);
  slot = {std::move(main_page_key), std::move(built_signature), std::move(built_main_structure),
          std::move(built_tile_sigs), std::move(card_patches), root, std::move(targets), std::move(actions),
          std::move(charts), icon_pool, std::move(icon_uses)};
  main_page_key.clear();
  root = nullptr;
  icon_pool.fill(nullptr);
  active = false;
  main_mode = false;
}

inline void activate_main_page(const std::string &key) {
  if (key == main_page_key) return;
  RetainedPage saved;
  for (auto &slot : retained_pages) {
    if (slot.key != key) continue;
    saved = std::move(slot);
    slot = {};
    break;
  }
  leave_main_page();
  main_page_key = key;
  if (!saved.root) return;
  if (root) clear();
  delete_page_widgets(root, icon_pool);
  root = saved.root;
  built_signature = std::move(saved.signature);
  built_main_structure = std::move(saved.main_structure);
  built_tile_sigs = std::move(saved.tile_sigs);
  card_patches = std::move(saved.patches);
  targets = std::move(saved.touch_targets);
  actions = std::move(saved.callbacks);
  charts = std::move(saved.chart_data);
  icon_pool = saved.icons;
  icon_uses = std::move(saved.image_uses);
  auto &live_targets = onx_panel_ui::touch_targets;
  live_targets.insert(live_targets.end(), targets.begin(), targets.end());
  main_mode = true;
}

// Pre-warm a neighbor Action Grid page directly into its PSRAM RGB565 buffer
// without touching any visible LVGL widget or evicting the active grid page.
inline lv_obj_t *prewarm_icon_img = nullptr;
inline bool prewarm_grid(const std::string &key, const std::vector<Tile> &tiles, int width, int height) {
  if (tiles.empty() || tiles.size() > 16) return false;
  const int saved_w = W, saved_h = H;
  W = width; H = height;
  int min_y = H, max_y = 0;
  for (const auto &t : tiles) {
    min_y = std::min(min_y, t.r.y);
    max_y = std::max(max_y, t.r.bottom());
  }
  min_y = std::clamp(min_y, 0, H);
  max_y = std::clamp(max_y, min_y + 1, H);
  const int buf_h = max_y - min_y;
  uint32_t pal_sig = 2166136261u;
  auto mix32 = [&](uint32_t v) { pal_sig ^= v; pal_sig *= 16777619u; };
  mix32(BG()); mix32(accent()); mix32(uint32_t(W)); mix32(uint32_t(H));
  if (onx_grid_cache::has_valid_page(key, W, buf_h, min_y, pal_sig, int(tiles.size()))) {
    W = saved_w; H = saved_h;
    return false;
  }
  bool full_redraw = false;
  auto *page = onx_grid_cache::acquire_page(key, W, buf_h, min_y, pal_sig, int(tiles.size()), full_redraw, active_grid_page);
  if (!page) { W = saved_w; H = saved_h; return false; }
  if (full_redraw)
    onx_grid_cache::fill_rect(page->pixels, page->w, page->h, 0, 0, page->w, page->h, onx_grid_cache::pack565(BG()));
  for (size_t i = 0; i < tiles.size(); ++i) {
    const auto &t = tiles[i];
    const auto *d = custom_logic_entity_sub::entity_detail(t.entity);
    const bool ready = d != nullptr && d->received && d->available;
    const void *src = nullptr;
    if (!t.entity.empty()) {
      if (t.artwork) {
        if (auto *art = onx_grid_cache::get_artwork(t.artwork))
          src = art->frame(t.on, animation_elapsed(t.entity));
      } else if (bind_icon) {
        if (prewarm_icon_img == nullptr) {
          prewarm_icon_img = lv_image_create(lv_layer_top());
          lv_obj_add_flag(prewarm_icon_img, LV_OBJ_FLAG_HIDDEN);
          lv_obj_remove_flag(prewarm_icon_img, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
        }
        bind_icon(prewarm_icon_img, t.entity);
        src = lv_image_get_src(prewarm_icon_img);
      }
    }
    const uint64_t tsig = grid_tile_sig(t, src, ready);
    if (full_redraw || page->tile_sigs[i] != tsig) {
      rasterize_grid_tile(*page, i, t, src, ready);
      page->tile_sigs[i] = tsig;
    }
  }
  page->valid = true;
  W = saved_w; H = saved_h;
  return true;
}

// Pre-warm a neighbor main page while idle (PSRAM buffer for grids, hidden
// RetainedPage widget tree for regular non-media pages). Returns true if work ran.
inline bool prewarm_main_page(lv_obj_t *parent, const std::string &key, const std::vector<Tile> &tiles,
                              int width, int height) {
  if (!parent || key.empty() || key == main_page_key || tiles.empty() || dragging) return false;
  if (tiles[0].key) return prewarm_grid(key, tiles, width, height);
  for (const auto &slot : retained_pages) if (slot.key == key && slot.root) return false;
  for (const auto &t : tiles) if (has_prefix(t.entity, "media_player.")) return false;
  // Save active page state cleanly, build the neighbor into a hidden root, stash it, restore.
  const int saved_w = W, saved_h = H;
  const bool saved_main = main_mode, saved_grid = grid_cached_mode, saved_active = active;
  auto saved_key = std::move(main_page_key);
  auto saved_sig = std::move(built_signature);
  auto saved_struct = std::move(built_main_structure);
  auto saved_tile_sigs = std::move(built_tile_sigs);
  auto saved_patches = std::move(card_patches);
  auto *saved_root = root;
  auto saved_targets = std::move(targets);
  auto saved_actions = std::move(actions);
  auto saved_charts = std::move(charts);
  auto saved_icons = icon_pool;
  auto saved_uses = std::move(icon_uses);
  auto saved_media_entity = onx_media::main_entity;

  W = width; H = height;
  root = nullptr;
  icon_pool.fill(nullptr);
  targets.clear(); actions.clear(); charts.clear(); icon_uses.clear(); card_patches.clear();
  main_mode = true; grid_cached_mode = false;
  ensure_root(parent);
  lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_pos(root, 0, 0);
  lv_obj_set_size(root, W, H);

  std::string sig, structure;
  std::vector<std::string> tile_sigs;
  compute_main_signatures(tiles, sig, structure, tile_sigs);
  build_main_cards(tiles);

  auto &live_targets = onx_panel_ui::touch_targets;
  for (auto *target : targets)
    live_targets.erase(std::remove(live_targets.begin(), live_targets.end(), target), live_targets.end());
  for (auto *icon : icon_pool) if (icon) lv_anim_delete(icon, nullptr);

  // Prefer an empty RetainedPage slot so pre-warming never evicts recently visited pages prematurely.
  RetainedPage *dest = nullptr;
  for (auto &slot : retained_pages) if (!slot.root) { dest = &slot; break; }
  if (!dest) {
    dest = &retained_pages[next_retained_page];
    next_retained_page = (next_retained_page + 1) % retained_pages.size();
    delete_page_widgets(dest->root, dest->icons);
  }
  *dest = {key, std::move(sig), std::move(structure), std::move(tile_sigs), std::move(card_patches),
           root, std::move(targets), std::move(actions), std::move(charts), icon_pool, std::move(icon_uses)};

  W = saved_w; H = saved_h;
  main_mode = saved_main; grid_cached_mode = saved_grid; active = saved_active;
  main_page_key = std::move(saved_key);
  built_signature = std::move(saved_sig);
  built_main_structure = std::move(saved_struct);
  built_tile_sigs = std::move(saved_tile_sigs);
  card_patches = std::move(saved_patches);
  root = saved_root;
  targets = std::move(saved_targets);
  actions = std::move(saved_actions);
  charts = std::move(saved_charts);
  icon_pool = saved_icons;
  icon_uses = std::move(saved_uses);
  onx_media::main_entity = std::move(saved_media_entity);
  return true;
}

// Quick actions: every entry as a large tile, two columns over the whole
// screen; a tap runs it and closes the view.
inline void build_quick_actions(const std::string &e, const onx_playlist::Config &list) {
  const int pad = P(16), bs = P(44), gap = P(10);
  text(root, "Quick actions", fonts.title, TEXT(), pad, pad + (bs - line(fonts.title)) / 2, W - 2 * pad - bs - P(8));
  circle(root, W - pad - bs / 2, pad + bs / 2, bs, CONTROL(), LV_SYMBOL_CLOSE, TEXT(), []() { onx_playlist::browser.clear(); });
  const int n = list.count, cols = n == 1 ? 1 : 2, rows = (n + cols - 1) / cols;
  const int top = pad + bs + P(14), avail = H - pad - top;
  const int th = std::min(P(96), (avail - (rows - 1) * gap) / std::max(1, rows));
  const int tw = (W - 2 * pad - (cols - 1) * gap) / cols;
  for (int i = 0; i < n; ++i) {
    const std::string action = list.entries[i].action, name = list.entries[i].name;
    const std::string caption = name.empty() ? action : name;
    lv_obj_t *b = button(root, pad + (i % cols) * (tw + gap), top + (i / cols) * (th + gap), tw, th, P(16), CONTROL(), "",
                         fonts.body, TEXT(), [action]() {
                           onx_playlist::browser.clear();
                           if (run_quick_action) run_quick_action(action);
                         });
    const int lines = std::min(lines_for(caption, fonts.body, tw - P(20), 3), std::max(1, (th - P(12)) / line(fonts.body)));
    lv_obj_t *l = text(b, caption, fonts.body, TEXT(), 0, 0, tw - P(20), LV_TEXT_ALIGN_CENTER, lines);
    lv_obj_center(l);
  }
}
// Draws (or keeps) the redesigned detail for `entity` inside `parent`.
inline void render(lv_obj_t *parent, const std::string &e, const std::string &title, const onx_detail_view::View &v,
                   int width, int height) {
  ensure_root(parent);
  W = width; H = height;
  lv_obj_set_pos(root, 0, 0);
  lv_obj_set_size(root, W, H);
  lv_obj_remove_flag(root, LV_OBJ_FLAG_HIDDEN);
  active = true;
  if (dragging || (custom_logic_entity_sub::is_timer_detail(e) && timer_interaction_pending && timer_interaction_pending())) return;
  const auto *d = custom_logic_entity_sub::entity_detail(e);
  const auto sig = signature(e, d, v, title);
  if (sig == built_signature && !main_mode) { refresh_icons(); tick_media_progress(); tick_timers(); return; }
  if (!main_mode && patch && d != nullptr && has_prefix(e, "light.") && !built_structure.empty() &&
      light_structure(e, *d, title) == built_structure && patch()) {
    built_signature = sig;
    return;
  }
  clear();
  built_signature = sig;
  if (d != nullptr && has_prefix(e, "light.")) built_structure = light_structure(e, *d, title);
  auto l = layout();
  // Shortcut button along the bottom of the control column.
  const std::string action = shortcut ? shortcut(e) : "";
  const bool has_shortcut = (action.rfind("script.", 0) == 0 || action.rfind("automation.", 0) == 0) &&
                            !has_prefix(e, "weather.") && !has_prefix(e, "select.") && !has_prefix(e, "input_select.");
  const auto *list = quick_actions ? quick_actions(e) : nullptr;
  header_reserve = 0;
  if (list && list->count) {
    auto &browser = onx_playlist::browser;
    if (browser.open && browser.entity == e) { build_quick_actions(e, *list); return; }
  }
  // Lyrics hide the quick actions button; tapping them shows the artwork and the button again.
  const bool lyrics_up = d != nullptr && has_prefix(e, "media_player.") && onx_media::has_playback(d->state) &&
                         onx_lyrics::shown(e, d->media_title);
  if (list && list->count && !lyrics_up) {
    // A list button at the top right opens the quick actions full screen.
    const int bs = P(44);
    header_reserve = bs + P(8);
    circle(root, W - P(16) - bs / 2, P(16) + bs / 2, bs, CONTROL(), LV_SYMBOL_LIST, TEXT(), [e]() {
      custom_logic_entity_sub::select_control_entity(e);
      onx_playlist::browser.toggle(e);
    });
  }
  if (has_shortcut && !(list && list->count)) {
    const int bh = P(48), y = l.ctl.bottom() - bh;
    std::string name = shortcut_title ? shortcut_title(action) : "";
    if (name.empty()) name = action;
    button(root, l.ctl.x, y, l.ctl.w, bh, bh / 2, CONTROL(), std::string(LV_SYMBOL_PLAY) + "  " + name, fonts.body, TEXT(),
           []() { if (run_shortcut) run_shortcut(); });
    l.ctl.h -= bh + P(10);
    if (!wide() && l.ctl.h < P(56)) {  // Tight portrait screens give space from the visual block.
      const int need = P(56) - l.ctl.h;
      l.vis.h -= need; l.ctl.y -= need; l.ctl.h += need;
    }
  }
  if (d == nullptr || !d->received || !d->available) {
    header(l.head, title, d == nullptr || !d->received ? "Waiting for initial data" : "Unavailable");
    text(root, d == nullptr || !d->received ? "…" : "—", fonts.big, MUTED(), l.vis.x, l.vis.y + l.vis.h / 3, l.vis.w, LV_TEXT_ALIGN_CENTER);
    return;
  }
  if (custom_logic_entity_sub::is_timer_detail(e)) build_timers(e, title);
  else if (has_prefix(e, "light.")) build_light(l, e, *d, title);
  else if (has_prefix(e, "climate.")) build_climate(l, e, *d, title);
  else if (has_prefix(e, "fan.")) build_fan(l, e, *d, title);
  else if (has_prefix(e, "cover.")) build_cover(l, e, *d, title);
  else if (has_prefix(e, "automation.") || has_prefix(e, "scene.") || has_prefix(e, "script.") || has_prefix(e, "button.") || has_prefix(e, "input_button."))
    build_action(l, e, *d, title);
  else if (has_prefix(e, "binary_sensor.")) build_binary(l, e, *d, title);
  else if (has_prefix(e, "event.")) build_event(l, e, *d, title);
  else if (has_prefix(e, "siren.")) build_siren(l, e, *d, title);
  else if (has_prefix(e, "vacuum.")) build_vacuum(l, e, *d, title);
  else if (has_prefix(e, "camera.")) build_camera(l, e, *d, title);
  else if (has_prefix(e, "lock.")) build_lock(l, e, *d, title);
  else if (has_prefix(e, "alarm_control_panel.")) build_alarm(l, e, *d, title);
  else if (has_prefix(e, "sensor.")) build_sensor(l, e, *d, title, v);
  else if (has_prefix(e, "number.") || has_prefix(e, "input_number.")) build_number(l, e, *d, title);
  else if (has_prefix(e, "select.") || has_prefix(e, "input_select.")) build_select(l, e, *d, title);
  else if (has_prefix(e, "weather.")) build_weather(e, *d, title);
  else if (has_prefix(e, "media_player.")) build_media(l, e, *d, title);
  else build_toggle(l, e, *d, title);
}
// The panel's own (local Assist) timers, shown above everything.
inline void render_timers(lv_obj_t *parent, int width, int height) {
  ensure_root(parent);
  W = width; H = height;
  lv_obj_set_pos(root, 0, 0);
  lv_obj_set_size(root, W, H);
  lv_obj_remove_flag(root, LV_OBJ_FLAG_HIDDEN);
  active = true;
  // Countdown refresh must preserve the pressed widget through LVGL click dispatch.
  if (dragging || (timer_interaction_pending && timer_interaction_pending())) return;
  const std::string sig = "timers|" + std::to_string(W) + 'x' + std::to_string(H) + '|' + std::to_string(accent()) + '|' + std::to_string(BG()) + '|' +
                          timer_signature();
  if (sig == built_signature && !main_mode) { tick_timers(); return; }
  clear();
  built_signature = sig;
  build_timers("", "Timers");
}
// Keeps the sensor graph fresh between rebuilds.
inline void tick(const std::string &e) {
  if (!active || !draw_graph || onx_sensor_graph::canvas == nullptr) return;
  lv_obj_t *parent = lv_obj_get_parent(onx_sensor_graph::canvas);
  bool inside = false;
  for (lv_obj_t *p = parent; p != nullptr; p = lv_obj_get_parent(p)) inside |= p == root;
  if (inside && !lv_obj_has_flag(onx_sensor_graph::canvas, LV_OBJ_FLAG_HIDDEN))
    draw_graph(parent, e, lv_obj_get_style_y(onx_sensor_graph::canvas, LV_PART_MAIN));
}
}  // namespace onx_panel_detail
