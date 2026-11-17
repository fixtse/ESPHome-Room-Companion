#pragma once
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
#include <cmath>
#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "lvgl.h"
#include "esphome/core/hal.h"
#include "onx_render.h"
#include "onx_panel_ui.h"
#include "onx_display_settings.h"
#include "onx_detail_view.h"
#include "onx_sensor_graph.h"

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

// ------------------------------------------------------------ geometry
inline int W = 320, H = 480;
// Layout is drawn for the 320 px short side of the 3.5" panel and scaled.
inline int P(int v) { return v * std::min(W, H) / 320; }
inline bool wide() { return W > H; }
struct Rect { int x, y, w, h; int right() const { return x + w; } int bottom() const { return y + h; } };
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
inline void track(time_t now) {
  if (now < 1577836800) return;
  for (const auto &d : custom_logic_entity_sub::entity_details) {
    if (!has_prefix(d.entity_id, "binary_sensor.") || !d.received || !d.available) continue;
    auto it = std::find_if(changes.begin(), changes.end(), [&](const Changes &c) { return c.entity == d.entity_id; });
    if (it == changes.end()) {
      if (changes.size() >= 20) changes.erase(changes.begin());
      changes.push_back({d.entity_id, d.state, {}});
      continue;  // The first sample is not a change.
    }
    if (it->last == d.state) continue;
    it->last = d.state;
    it->items.insert(it->items.begin(), {d.state, now});
    if (it->items.size() > 5) it->items.pop_back();
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
inline void interactive(lv_obj_t *obj) {
  lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  targets.push_back(obj);
  onx_panel_ui::touch_targets.push_back(obj);
}
inline bool allowed() {
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
inline void header(const Rect &r, const std::string &name, const std::string &context, int reserve = 0) {
  const int w = r.w - reserve;
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
  interactive(obj);
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
  custom_logic_entity_sub::request_control(entity, service, {{field.empty() ? "panel_" + parameter : field, parameter, value}});
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
__attribute__((noinline)) inline void build_light(const Layout &l, const std::string &e, const EntityDetailState &d, const std::string &title) {
  const auto s = light_state ? light_state(e) : esphome::onx_storage::NormalLightState{};
  const bool on = s.is_on && d.available;
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
                         info.x, y, info.w, LV_TEXT_ALIGN_CENTER);
  y += line(fonts.big);
  std::string context = !d.available ? "Unavailable" : on ? "On" : "Off";
  if (on && s.color_temperature_kelvin > 0 && s.supports_color_temperature() &&
      (d.light_temperature || !s.supports_rgb()))
    context += " · " + std::to_string(s.color_temperature_kelvin) + " K";
  text(root, context, fonts.small, MUTED(), info.x, y, info.w, LV_TEXT_ALIGN_CENTER);
  y += line(fonts.small) + P(14);

  // Mode bar: power | modes.
  const int bs = P(44), gap = P(6), n = modes.size() > 1 ? modes.size() : 0;
  const int bar_w = P(8) * 2 + bs + (n ? P(17) + n * bs + (n - 1) * gap : 0);
  // The bottom of the control column leaves room for a shortcut button (render()).
  const int bar_x = info.x + (info.w - bar_w) / 2, bar_y = l.ctl.bottom() - bar_h;
  lv_obj_t *bar = box(root, bar_x, bar_y, bar_w, bar_h, CARD(), bar_h / 2);
  const int by = (bar_h - bs) / 2;
  lv_obj_t *power = button(bar, P(8), by, bs, bs, LV_RADIUS_CIRCLE, on ? strong() : CONTROL(), "", fonts.small, TEXT(),
                           [e, on]() { request(e, "light.toggle", "", on ? "off" : "on", "panel_power"); });
  lv_obj_center(text(power, LV_SYMBOL_POWER, fonts.symbol,
                     on ? (onx_display_settings::light_color(strong()) ? 0x161B22 : 0xFFFFFF) : SOFT_TEXT(), 0, 0));
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

  // The large control.
  const Rect vis = wide() ? l.vis : Rect{pad, y, W - 2 * pad, bar_y - P(16) - y};
  if (!d.available || vis.h < P(60)) return;
  if (mode < 0) {  // On/off only: one large power button.
    const int size = std::min({vis.w, vis.h, P(150)});
    circle(root, vis.x + vis.w / 2, vis.y + vis.h / 2, size, on ? strong() : CONTROL(), LV_SYMBOL_POWER,
           on ? TEXT() : SOFT_TEXT(), [e, on]() { request(e, "light.toggle", "", on ? "off" : "on", "panel_power"); });
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
  lv_obj_t *fill = box(track, 0, sh - fill_h, sw, fill_h, accent(), 0);
  const int kh = P(5), kw = sw * 30 / 100;
  auto knob_y = [sh, kh](int h) { return std::clamp(sh - h + P(12), P(8), sh - kh - P(8)); };
  lv_obj_t *knob = box(track, (sw - kw) / 2, knob_y(fill_h), kw, kh, ink, kh / 2);
  lv_obj_set_style_bg_opa(knob, LV_OPA_70, 0);
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
    interactive(arc);
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

  // HVAC modes and one option row.
  auto modes = onx_control::options(d.modes);
  if (modes.size() > 4) modes.resize(4);
  const int ch = P(44), gap = P(10);
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
  struct Option { const char *key, *name, *service; std::string list, value; uint32_t bit; };
  const Option options[] = {{"preset_mode", "Preset", "climate.set_preset_mode", d.presets, effective(d, "preset_mode"), 16},
                            {"fan_mode", "Fan", "climate.set_fan_mode", d.fan_modes, effective(d, "fan_mode"), 8},
                            {"swing_mode", "Swing", "climate.set_swing_mode", d.swing_modes, effective(d, "swing_mode"), 32}};
  for (const auto &o : options) {
    if (!(d.features & o.bit) || onx_control::options(o.list).empty() || y + ch > l.ctl.bottom() + P(4)) continue;
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

__attribute__((noinline)) inline void build_cover(const Layout &l, const std::string &e, const EntityDetailState &d, const std::string &title) {
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
}

__attribute__((noinline)) inline void build_toggle(const Layout &l, const std::string &e, const EntityDetailState &d, const std::string &title) {
  const bool on = d.state == "on";
  const auto domain = e.substr(0, e.find('.'));
  header(l.head, title, d.available ? (on ? "On" : "Off") : "Unavailable");
  const int size = std::min({P(150), l.vis.w, l.vis.h - line(fonts.big) - P(16)});
  const int cx = l.vis.x + l.vis.w / 2, cy = l.vis.y + (l.vis.h - line(fonts.big) - P(12)) / 2;
  circle(root, cx, cy, size, on ? strong() : CONTROL(), LV_SYMBOL_POWER, on ? TEXT() : SOFT_TEXT(),
        [e, domain, on]() { request(e, domain + ".toggle", "", on ? "off" : "on", "panel_power"); });
  text(root, on ? "On" : "Off", fonts.big, TEXT(), l.vis.x, cy + size / 2 + P(10), l.vis.w, LV_TEXT_ALIGN_CENTER);
  // Smart plugs: power reading and graph from the configured secondary sensor.
  const int i = onx_sensor_view::index(e);
  if (i < 0 || !onx_sensor_view::configs[i].secondary[0]) return;
  const auto &s = onx_sensor_view::states[i];
  const int th = P(56);
  tile(root, {l.ctl.x, l.ctl.y, l.ctl.w, th}, s.unit == "kWh" ? "Energy" : "Power",
       s.received && s.available ? onx_detail_view::compact(s.value) + " " + s.unit : "—");
  if (onx_sensor_view::configs[i].graph && draw_graph && l.ctl.h >= th + P(70)) draw_graph(root, e, l.ctl.y + th + P(8));
}

__attribute__((noinline)) inline void build_action(const Layout &l, const std::string &e, const EntityDetailState &d, const std::string &title) {
  const bool script = has_prefix(e, "script."), scene = has_prefix(e, "scene."), automation = has_prefix(e, "automation.");
  const bool running = script && d.state == "on";
  const std::string kind = script ? "Script" : automation ? "Automation" : scene ? "Scene" : "Button";
  header(l.head, title, kind + (running ? " · Running…" : ""));
  const int size = std::min({P(170), l.vis.w, l.vis.h - P(40)});
  const int cx = l.vis.x + l.vis.w / 2, cy = l.vis.y + (l.vis.h - P(30)) / 2;
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
  text(root, running ? "Running…" : "Tap to run", fonts.body, SOFT_TEXT(), l.vis.x, cy + size / 2 + P(12), l.vis.w,
       LV_TEXT_ALIGN_CENTER);
  if (automation) {
    const bool enabled = d.state == "on";
    button(root, l.ctl.x, l.ctl.y, l.ctl.w, P(48), P(16), CONTROL(),
           enabled ? "Enabled · Tap to disable" : "Disabled · Tap to enable", fonts.body, TEXT(),
           [e, enabled]() { request(e, enabled ? "automation.turn_off" : "automation.turn_on", "", enabled ? "off" : "on", "panel_automation"); });
  } else if (running) {
    button(root, l.ctl.x, l.ctl.y, l.ctl.w, P(48), P(16), CONTROL(), "Stop script", fonts.body, TEXT(),
           [e]() { request(e, "script.turn_off", "", "off", "panel_stop"); });
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
  std::string focus;              // Key of the focused timer ("" = default).
};
inline TimerState timers;
inline std::function<void()> stop_ringing;  // Supplied by assist.yaml.
inline std::function<void()> close_timers;  // Closes the local timer screen.
struct TimerItem {
  std::string key, entity, name, kind, state;  // state: active, paused, ringing, idle
  double left{0}, total{0};
  bool ready{false}, helper{false};
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
    if (d.state != "active" || age < 5000)
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
      out.push_back(entity_timer(e, *d, title.empty() ? e : onx_media::display_text(title)));
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
// The focused timer, controls and the other timers. `preferred` is the
// detail's own entity (or "" on the local timer screen).
__attribute__((noinline)) inline void build_timers(const std::string &preferred, const std::string &fallback_title) {
  auto items = all_timers();
  std::stable_sort(items.begin(), items.end(), [](const TimerItem &a, const TimerItem &b) {
    return rank(a) != rank(b) ? rank(a) < rank(b) : a.left < b.left;
  });
  int focus = -1;
  for (int i = 0; i < int(items.size()); ++i)
    if (!timers.focus.empty() && items[i].key == timers.focus) focus = i;
  if (focus < 0)
    for (int i = 0; i < int(items.size()); ++i)
      if (!preferred.empty() && items[i].key == preferred) focus = i;
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
  const int pad = P(16);
  const bool landscape = wide();
  const Rect col = landscape ? Rect{W * 48 / 100 + P(8), pad, W - W * 48 / 100 - P(8) - pad, H - 2 * pad} : Rect{pad, pad, W - 2 * pad, H - 2 * pad};
  const lv_font_t *nf = fit(t.name, col.w, {fonts.title, fonts.body});
  text(root, t.name, nf, TEXT(), col.x, col.y, col.w);
  text(root, context, fonts.small, ringing ? accent() : MUTED(), col.x, col.y + line(nf), col.w);
  int y = col.y + line(nf) + line(fonts.small) + P(14);
  // Controls: only what the timer's own system supports.
  struct Control { const char *symbol; std::string caption; bool primary; std::function<void()> fn; };
  std::vector<Control> controls;
  const std::string e = t.entity;
  if (local && ringing) {
    controls.push_back({LV_SYMBOL_STOP, "Stop", true, []() { if (stop_ringing) stop_ringing(); }});
  } else if (t.helper && t.ready) {
    const bool active = t.state == "active", paused = t.state == "paused";
    if (active || paused) {
      controls.push_back({LV_SYMBOL_CLOSE, "Cancel", false, [e]() { request(e, "timer.cancel", "", "", "panel_timer"); }});
      controls.push_back({active ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY, active ? "Pause" : "Resume", true,
                          [e, active]() { request(e, active ? "timer.pause" : "timer.start", "", "", "panel_timer"); }});
      controls.push_back({LV_SYMBOL_PLUS, "1 min", false, [e]() { request(e, "timer.change", "duration", "00:01:00", "panel_timer"); }});
    } else {
      controls.push_back({LV_SYMBOL_PLAY, ringing ? "Restart" : "Start", true, [e]() { request(e, "timer.start", "", "", "panel_timer"); }});
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
  const lv_font_t *vf = fit(value, inner, {hero_text(value) ? fonts.hero : nullptr, fonts.big, fonts.title});
  const std::string sub = ringing ? "Time is up" : t.total > 0 ? "of " + onx_timer::countdown(t.total) : "";
  const int block = line(vf) + (sub.empty() ? 0 : line(fonts.small));
  const int vy = ay + (size - block) / 2;
  text(root, value, vf, TEXT(), cx - size / 2, vy, size, LV_TEXT_ALIGN_CENTER);
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
    lv_arc_set_range(mini, 0, 100);
    lv_arc_set_value(mini, o.total > 0 ? int(std::clamp(o.left / o.total, 0.0, 1.0) * 100) : 100);
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
    text(row, left, fonts.body, o.state == "ringing" ? accent() : TEXT(), col.w - rp - lw, (rh - line(fonts.body)) / 2, lw);
    interactive(row);
    const std::string key = o.key;
    on(row, LV_EVENT_CLICKED, [key](lv_event_t *) { if (allowed()) { timers.focus = key; built_signature.clear(); } });
  }
}
// Changes once a second while any timer runs.
inline std::string timer_signature() {
  std::string s = timers.focus + '|' + std::to_string(timers.ringing);
  for (const auto &t : all_timers())
    s += '|' + t.key + t.state + std::to_string(long(std::lround(t.left))) + ',' + std::to_string(long(t.total)) +
         std::to_string(t.ready);
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
  if (history.size() >= 2) {
    const float delta = history.back().value - history.front().value;
    const std::string arrow = delta > 0 ? "▲ " : delta < 0 ? "▼ " : "";
    text(root, arrow + fmt(std::fabs(delta), 1) + " over this range", fonts.small, accent(), area.x, y, area.w);
    y += line(fonts.small);
  } else if (graph) {
    const auto &state = onx_sensor_view::states[i];
    text(root, state.pending ? "Loading history…" : state.requested ? "No recorded history" : "Collecting history…", fonts.small, MUTED(), area.x, y, area.w);
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
  const int th = P(64);
  const Rect bottom = wide() ? Rect{l.ctl.x, l.ctl.bottom() - th, l.ctl.w, th} : Rect{area.x, area.bottom() - th, area.w, th};
  if (graph && draw_graph) {
    const Rect g = wide() ? Rect{l.ctl.x, l.ctl.y, l.ctl.w, (stats ? bottom.y - P(12) : l.ctl.bottom()) - l.ctl.y}
                          : Rect{area.x, y, area.w, (stats ? bottom.y - P(12) : area.bottom()) - y};
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
  if (kind == "door" || kind == "window" || kind == "opening" || kind == "garage_door") return on ? "Open" : "Closed";
  if (kind == "motion" || kind == "occupancy" || kind == "presence") return on ? "Detected" : "Clear";
  if (kind == "moisture") return on ? "Wet" : "Dry";
  if (kind == "lock") return on ? "Unlocked" : "Locked";
  if (kind == "connectivity") return on ? "Connected" : "Disconnected";
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
  interactive(list);  // Scroll gestures go to LVGL.
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
    const bool timed_out = d.forecast_pending && esphome::millis() - d.forecast_requested_ms >= 20000;
    text(panel, d.forecast_pending && !timed_out ? "Loading forecast…" : "Forecast not available", fonts.small, MUTED(), P(14),
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
struct Tile {
  std::string entity, name, value, context;
  bool on{false};
  int ring{-1};
  Rect r{};
  bool compact{false};
};
inline uint32_t INSET() {return CONTROL();}  // Surfaces inside a card (same as CONTROL()).
// Entity icons are bound by the icon downloader, which keeps one binding per
// widget: a fixed pool of image objects is re-parented on every rebuild
// instead of creating new widgets.
inline std::function<void(lv_obj_t *, const std::string &)> bind_icon;
struct IconUse { lv_obj_t *image; std::string entity; int cx, cy, size; const void *src; };
inline std::array<lv_obj_t *, 8> icon_pool{};
inline std::vector<IconUse> icon_uses;
inline bool main_mode = false;
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
  lv_image_set_pivot(u.image, h.w / 2, h.h / 2);
  lv_image_set_scale(u.image, std::max<int>(16, u.size * 256 / std::max<int>(h.w, h.h)));
  lv_obj_set_pos(u.image, u.cx - int(h.w) / 2, u.cy - int(h.h) / 2);
}
inline void icon(lv_obj_t *parent, const std::string &e, int cx, int cy, int size, uint32_t color) {
  if (!bind_icon || icon_uses.size() >= icon_pool.size()) return;
  auto *&img = icon_pool[icon_uses.size()];
  if (img == nullptr) {
    img = lv_image_create(lv_layer_top());
    lv_obj_remove_flag(img, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  }
  lv_obj_set_parent(img, parent);
  lv_obj_remove_flag(img, LV_OBJ_FLAG_HIDDEN);
  bind_icon(img, e);
  // After binding, which applies its own white recolour on a new binding.
  lv_obj_set_style_image_recolor(img, lv_color_hex(color), 0);
  lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
  icon_uses.push_back({img, e, cx, cy, size, nullptr});
  place_icon(icon_uses.back());
}
// Re-binds each frame so downloaded icons replace the fallbacks.
inline void refresh_icons() {
  if (!bind_icon) return;
  for (auto &u : icon_uses) {
    bind_icon(u.image, u.entity);
    if (lv_image_get_src(u.image) != u.src) place_icon(u);
  }
}
// Round icon chip, tinted while the entity is active.
inline void chip_icon(lv_obj_t *parent, const std::string &e, int x, int y, int size, bool on) {
  box(parent, x, y, size, size, on ? soft() : INSET(), size / 2);
  icon(parent, e, x + size / 2, y + size / 2, size * 56 / 100, on ? accent() : SOFT_TEXT());
}
// Pill switch.
inline void toggle(lv_obj_t *parent, int x, int y, bool on, std::function<void()> fn) {
  const int w = P(54), h = P(30), k = h - P(8);
  lv_obj_t *b = button(parent, x, y, w, h, h / 2, on ? accent() : TRACK(), "", fonts.small, TEXT(), std::move(fn));
  box(b, on ? w - k - P(4) : P(4), P(4), k, k, on ? 0x101418 : SOFT_TEXT(), k / 2);
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
  const int pad = P(16), is = std::min(P(84), r.h / 3);
  text(c, t.name, fonts.small, SOFT_TEXT(), pad, pad, r.w - 2 * pad - is);
  int y = pad + line(fonts.small);
  const std::string temp = present(d.target, "°").empty() ? "—" : present(d.target, "°");
  const bool hero = r.h >= P(240) && hero_text(temp);
  const lv_font_t *tf = hero ? fonts.hero : fonts.big;
  text(c, temp, tf, TEXT(), pad - (hero ? P(3) : 0), y - (hero ? P(6) : 0), r.w - 2 * pad - is);
  y += line(tf) - (hero ? P(14) : P(2));
  text(c, condition(d.state), fonts.body, TEXT(), pad, y, r.w - 2 * pad - is);
  y += line(fonts.body) + P(14);
  icon(c, t.entity, r.w - pad - is / 2, pad + is / 2 + P(8), is, TEXT());
  std::vector<std::pair<std::string, std::string>> stats;
  const std::string bearing = compass(d.wind_bearing);
  auto add = [&](const std::string &name, const std::string &value) { if (!value.empty()) stats.push_back({name, value}); };
  add("Humidity", present(d.humidity, "%"));
  add(bearing.empty() ? "Wind" : "Wind · " + bearing, present(d.wind_speed, d.wind_unit));
  add("Pressure", present(d.pressure, d.pressure_unit));
  add("Visibility", present(d.visibility, d.visibility_unit));
  add("UV index", present(d.uv_index, ""));
  add("Feels like", present(d.apparent_temperature, "°"));
  const int gap = P(8), th = P(56), cols = 2, tw = (r.w - 2 * pad - gap) / cols;
  const int rows = std::min<int>((stats.size() + 1) / 2, (r.h - pad - y + gap) / (th + gap));
  for (int i = 0; i < std::min<int>(stats.size(), rows * cols); ++i)
    tile(c, {pad + (i % cols) * (tw + gap), y + (i / cols) * (th + gap), tw, th}, stats[i].first, stats[i].second, INSET());
  // Short cards: the first readings on one line instead of tiles.
  if (rows == 0 && !stats.empty() && r.h - pad - y + P(14) >= line(fonts.small)) {
    std::string summary;
    for (size_t i = 0; i < std::min<size_t>(stats.size(), 2); ++i)
      summary += (i ? "  ·  " : "") + stats[i].first + " " + stats[i].second;
    text(c, summary, fonts.small, MUTED(), pad, y - P(8), r.w - 2 * pad);
  }
}

inline void card_light(lv_obj_t *c, const Tile &t, const EntityDetailState &d) {
  const Rect r = t.r;
  const auto s = light_state ? light_state(t.entity) : esphome::onx_storage::NormalLightState{};
  const bool on = s.is_on && d.available, dimmable = s.supports_brightness();
  float level = on ? s.brightness_pct : 0;
  const float want = pending_value(t.entity, "brightness");
  clear_pending_if(t.entity, "brightness", level);
  if (std::isfinite(want)) level = want;
  Tile head = t;
  head.context = !d.available ? "Unavailable" : on ? "On" : "Off";
  if (on && s.color_temperature_kelvin > 0 && s.supports_color_temperature())
    head.context += " · " + std::to_string(s.color_temperature_kelvin) + " K";
  const int pad = P(14), cs = P(44), tx = pad + cs + P(12);
  chip_icon(c, t.entity, pad, pad, cs, on);
  titled(c, head, tx, pad + P(2), r.w - tx - pad - P(62));
  const std::string e = t.entity;
  if (d.available)
    toggle(c, r.w - pad - P(54), pad + P(7), on, [e, on]() { request(e, "light.toggle", "", on ? "off" : "on", "panel_power"); });
  const int sh = P(40), sy = r.h - pad - sh;
  const int vy = pad + cs + P(8);
  if (!dimmable) {
    if (sy - vy >= line(fonts.big)) text(c, on ? "On" : "Off", fonts.big, TEXT(), pad, vy, r.w - 2 * pad);
    return;
  }
  lv_obj_t *value = nullptr;
  if (sy - vy >= line(fonts.big)) value = text(c, pct(level), fonts.big, TEXT(), pad, vy, r.w - 2 * pad);
  const int sw = r.w - 2 * pad;
  lv_obj_t *track = box(c, pad, sy, sw, sh, INSET(), P(12));
  lv_obj_set_style_clip_corner(track, true, 0);
  const int fw = int(sw * std::clamp(level, 0.0f, 100.0f) / 100);
  lv_obj_t *fill = box(track, 0, 0, fw, sh, accent(), 0);
  lv_obj_t *knob = box(track, std::clamp(fw - P(9), P(4), sw - P(8)), P(9), P(4), sh - P(18), 0x101418, P(2));
  if (!d.available) return;
  drag(track, [e, track, fill, knob, value, sw, sh](lv_point_t p, bool commit) {
    lv_area_t a;
    lv_obj_get_coords(track, &a);
    const int v = std::clamp(int(std::lround((p.x - a.x1) * 100.0f / sw)), 1, 100);
    const int w = sw * v / 100;
    lv_obj_set_size(fill, w, sh);
    lv_obj_set_x(knob, std::clamp(w - P(9), P(4), sw - P(8)));
    if (value) lv_label_set_text(value, pct(v).c_str());
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
  text(c, "Target", fonts.small, MUTED(), 0, cy - line(vf) / 2 - line(fonts.small) + P(6), r.w, LV_TEXT_ALIGN_CENTER);
  text(c, value, vf, TEXT(), 0, cy - line(vf) / 2 + P(4), r.w, LV_TEXT_ALIGN_CENTER);
  if (!d.available || !(d.features & 1)) return;
  const std::string e = t.entity;
  auto stepper = [e, range](int direction) {
    custom_logic_entity_sub::select_control_entity(e);
    custom_logic_entity_sub::control_setting = range ? "range" : "temperature";
    onx_controls::rotate(e, direction);
  };
  circle(c, pad + bs / 2, by + bs / 2, bs, INSET(), LV_SYMBOL_MINUS, TEXT(), [stepper]() { stepper(-1); });
  circle(c, r.w - pad - bs / 2, by + bs / 2, bs, INSET(), LV_SYMBOL_PLUS, TEXT(), [stepper]() { stepper(1); });
}

inline void card_media(lv_obj_t *c, const Tile &t, const EntityDetailState &d) {
  const Rect r = t.r;
  const int pad = P(14), bh = P(48);
  const bool playing = d.state == "playing";
  const int by = r.h - pad - bh;
  const int art = std::max(P(48), std::min({P(104), by - pad - P(12), r.w * 36 / 100}));
  auto *frame = box(c, pad, pad, art, art, INSET(), P(10));
  lv_obj_set_style_clip_corner(frame, true, 0);
  if (onx_media::main_entity.empty()) onx_media::main_entity = t.entity;
  const void *source = artwork_source && onx_media::main_entity == t.entity ? artwork_source(t.entity) : nullptr;
  if (source) {
    if (!main_artwork) main_artwork = lv_image_create(lv_layer_top());
    lv_obj_set_parent(main_artwork, frame);
    lv_obj_remove_flag(main_artwork, LV_OBJ_FLAG_HIDDEN);
    lv_image_set_src(main_artwork, source);
    lv_image_header_t h{};
    if (lv_image_decoder_get_info(source, &h) == LV_RESULT_OK && h.w && h.h) {
      lv_image_set_pivot(main_artwork, h.w / 2, h.h / 2);
      lv_image_set_scale(main_artwork, art * 256 / std::max(h.w, h.h));
      lv_obj_center(main_artwork);
    }
  } else icon(c, t.entity, pad + art / 2, pad + art / 2, art * 46 / 100, playing ? accent() : SOFT_TEXT());
  const int tx = pad + art + P(12), tw = r.w - tx - pad;
  const bool track = !d.media_title.empty() && d.state != "off" && d.state != "idle";
  int y = pad + P(2);
  text(c, t.name, fonts.small, MUTED(), tx, y, tw);
  y += line(fonts.small);
  const std::string title = track ? onx_media::display_text(d.media_title) : label(d.state.empty() ? "idle" : d.state);
  const lv_font_t *f = fit(title, tw, {fonts.title, fonts.body});
  text(c, title, f, TEXT(), tx, y, tw);
  y += line(f);
  if (track && !d.media_artist.empty()) text(c, onx_media::display_text(d.media_artist), fonts.small, SOFT_TEXT(), tx, y, tw);
  if (t.ring >= 0 && pad + art - P(4) > y + line(fonts.small)) level(c, tx, pad + art - P(4), tw, P(4), t.ring / 1000.0f);
  if (by < pad + art || !d.available) return;
  const std::string e = t.entity;
  const int cx = r.w / 2, pw = P(96), gap = P(26);
  circle(c, cx - pw / 2 - gap - bh / 2, by + bh / 2, bh, INSET(), LV_SYMBOL_PREV, TEXT(),
         [e]() { request(e, "media_player.media_previous_track", "", "", "panel_media"); });
  lv_obj_t *play = button(c, cx - pw / 2, by, pw, bh, bh / 2, accent(), "", fonts.symbol, TEXT(),
                          [e, playing]() { request(e, playing ? "media_player.media_pause" : "media_player.media_play", "", "", "panel_media"); });
  lv_obj_center(text(play, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY, fonts.symbol, 0x101418, 0, 0));
  circle(c, cx + pw / 2 + gap + bh / 2, by + bh / 2, bh, INSET(), LV_SYMBOL_NEXT, TEXT(),
         [e]() { request(e, "media_player.media_next_track", "", "", "panel_media"); });
}

inline void card_generic(lv_obj_t *c, const Tile &t, const EntityDetailState *d) {
  const Rect r = t.r;
  const std::string e = t.entity, domain = e.substr(0, e.find('.'));
  const int pad = P(14), cs = P(44), tx = pad + cs + P(12);
  const bool available = d != nullptr && d->available;
  const bool switchable = available && (domain == "switch" || domain == "input_boolean" || domain == "automation" ||
                                        domain == "fan");
  const bool runnable = available && (domain == "scene" || domain == "script" || domain == "button" || domain == "input_button");
  chip_icon(c, e, pad, pad, cs, t.on);
  titled(c, t, tx, pad + P(2), r.w - tx - pad - (switchable ? P(62) : 0));
  if (switchable) {
    const bool on = d->state == "on";
    toggle(c, r.w - pad - P(54), pad + P(7), on, [e, domain, on]() { request(e, domain + ".toggle", "", on ? "off" : "on", "panel_power"); });
  }
  int bottom = r.h - pad;
  if (runnable && bottom - P(48) >= pad + cs + P(8)) {
    bottom -= P(48);
    button(c, pad, bottom, r.w - 2 * pad, P(48), P(24), INSET(), "Run", fonts.body, TEXT(), [e, domain]() {
      if (domain == "script") request(e, "script.turn_on", "", "on", "panel_run");
      else onx_controls::press(e);
    });
    bottom -= P(10);
  }
  const bool bar = t.ring >= 0 && !runnable;
  if (bar) bottom -= P(12) + P(10);
  const int vy = pad + cs + P(10);
  if (bottom - vy < line(fonts.big)) {
    if (bar && bottom + P(10) > vy) level(c, pad, bottom + P(10), r.w - 2 * pad, P(12), t.ring / 1000.0f);
    return;
  }
  // The value sits at the bottom of the card, above its bar.
  const bool numeric = d != nullptr && std::isfinite(num(d->state)) && (domain == "sensor" || domain == "number" || domain == "input_number");
  const std::string value = numeric ? onx_detail_view::compact(d->state) : t.value;
  const std::string unit = numeric ? d->unit : "";
  const bool hero = bottom - vy >= line(fonts.hero) && numeric && hero_safe(value);
  const int vh = hero ? line(fonts.hero) : line(fonts.big);
  reading(c, value, unit, pad, bottom - vh, r.w - 2 * pad, hero);
  if (bar) level(c, pad, bottom + P(10), r.w - 2 * pad, P(12), t.ring / 1000.0f);
}

// One line: icon, name, value (with a smaller unit) on the right.
inline void card_row(lv_obj_t *c, const Tile &t, const EntityDetailState *d) {
  const Rect r = t.r;
  const int pad = P(14), is = P(24);
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
  text(c, t.name, fonts.small, SOFT_TEXT(), nx, (r.h - line(fonts.small)) / 2, vx - nx - P(8));
  text(c, value, vf, binary ? accent() : TEXT(), vx, vy, vw);
  if (uw) text(c, unit, fonts.small, MUTED(), vx + vw + P(4), vy + line(vf) - line(fonts.small) - P(2), uw);
}

// The carousel's Settings item, drawn like the other main pages (same
// background, time row and card) instead of the base layout's round page. A tap
// falls through to the shared press action, which opens the settings menu.
constexpr const char *SETTINGS = "[Local] Settings";
inline const void *settings_icon = nullptr;
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

__attribute__((noinline)) inline void build_card(const Tile &t) {
  lv_obj_t *c = box(root, t.r.x, t.r.y, t.r.w, t.r.h, CARD(), t.compact ? P(14) : P(18));
  lv_obj_set_style_clip_corner(c, false, 0);
  const auto *d = custom_logic_entity_sub::entity_detail(t.entity);
  const bool ready = d != nullptr && d->received && d->available;
  if (t.compact) { card_row(c, t, d); return; }
  if (t.entity == SETTINGS) card_settings(c, t);
  else if (ready && has_prefix(t.entity, "weather.")) card_weather(c, t, *d);
  else if (ready && has_prefix(t.entity, "light.")) card_light(c, t, *d);
  else if (ready && has_prefix(t.entity, "climate.")) card_climate(c, t, *d);
  else if (ready && has_prefix(t.entity, "media_player.")) card_media(c, t, *d);
  else card_generic(c, t, d);
}

inline void ensure_root(lv_obj_t *parent) {
  if (root != nullptr) return;
  root = lv_obj_create(parent);
  lv_obj_remove_flag(root, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(root, 0, 0);
  lv_obj_set_style_pad_all(root, 0, 0);
  lv_obj_set_style_radius(root, 0, 0);
}
inline void clear();
inline std::string signature(const std::string &e, const EntityDetailState *d, const onx_detail_view::View &v,
                             const std::string &title);
// Draws (or keeps) a main page: the cards plus the time in the top row.
inline void render_main(lv_obj_t *parent, const std::vector<Tile> &tiles, int width, int height) {
  ensure_root(parent);
  W = width; H = height;
  lv_obj_set_pos(root, 0, 0);
  lv_obj_set_size(root, W, H);
  lv_obj_remove_flag(root, LV_OBJ_FLAG_HIDDEN);
  active = true;
  if (dragging) return;
  const time_t now = std::time(nullptr);
  std::string sig = "main|" + std::to_string(W) + 'x' + std::to_string(H) + '|' + std::to_string(accent()) + '|' + std::to_string(BG()) + '|' +
                    std::to_string(now > 1600000000 ? now / 60 : 0);
  for (const auto &t : tiles) {
    onx_detail_view::View v;
    v.value.text = t.value; v.context.text = t.context; v.name.text = t.name; v.value_ring = t.ring;
    sig += "#" + std::to_string(t.r.x) + ',' + std::to_string(t.r.y) + ',' + std::to_string(t.r.w) + ',' +
           std::to_string(t.r.h) + ',' + std::to_string(t.compact) + ',' + std::to_string(t.on) + ',' +
           signature(t.entity, custom_logic_entity_sub::entity_detail(t.entity), v, t.name);
  }
  if (sig == built_signature && main_mode) { refresh_icons(); return; }
  clear();
  built_signature = sig;
  main_mode = true;
  const auto *clock_font = fonts.header_clock != nullptr ? fonts.header_clock : fonts.small;
  if (now > 1600000000 && !tiles.empty() && tiles.front().r.y >= line(clock_font))
    // Left of the top row: the Wi-Fi / Home Assistant status sits in the
    // centre and the bell on the right.
    text(root, clock(now), clock_font, TEXT(), tiles.front().r.x + P(4), (tiles.front().r.y - line(clock_font)) / 2, W / 4);
  for (const auto &t : tiles) build_card(t);
}

// ------------------------------------------------------------ dispatch
inline bool supports(const std::string &e) {
  if (custom_logic_entity_sub::is_timer_detail(e)) return true;
  for (const char *p : {"light.", "climate.", "fan.", "cover.", "switch.", "input_boolean.", "automation.", "scene.",
                        "script.", "button.", "input_button.", "sensor.", "binary_sensor.", "number.", "input_number.",
                        "select.", "input_select.", "weather."})
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
       effective(*d, "preset_mode") + '|' + effective(*d, "fan_mode") + '|' + effective(*d, "swing_mode") + '|' + d->presets +
       '|' + d->options + '|' + d->humidity + '|' + d->apparent_temperature + '|' + d->wind_speed + '|' +
       std::to_string(d->features) + '|' + std::to_string(d->available) + '|' + std::to_string(d->received) + '|' +
       std::to_string(d->forecast_count) + '|' + std::to_string(d->forecast_pending) + '|' + d->timer_duration;
  for (int i = 0; i < d->forecast_count; ++i) s += '|' + d->forecast[i].date + d->forecast[i].high + d->forecast[i].low;
  s += '|' + d->media_title + '|' + d->media_artist + '|' + d->picture + '|' + onx_media::ready_url;
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
    s += '|' + st.value + st.unit + std::to_string(st.history(std::time(nullptr)).size()) + std::to_string(st.range_seconds) + std::to_string(st.pending);
  }
  if (const auto *c = changes_for(e)) s += '|' + std::to_string(c->items.size()) + ',' + std::to_string(std::time(nullptr) / 60);
  if (pending.entity == e) s += '|' + std::to_string(pending_value(e, pending.key));
  s += '|' + std::to_string(esphome::millis() - ran_at < 1500);
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
inline void clear() {
  detach_graph();
  detach_icons();
  detach_artwork();
  onx_media::main_entity.clear();
  main_mode = false;
  auto &t = onx_panel_ui::touch_targets;
  for (auto *o : targets) t.erase(std::remove(t.begin(), t.end(), o), t.end());
  targets.clear();
  if (root != nullptr) lv_obj_clean(root);
  actions.clear();
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

// Draws (or keeps) the redesigned detail for `entity` inside `parent`.
inline void render(lv_obj_t *parent, const std::string &e, const std::string &title, const onx_detail_view::View &v,
                   int width, int height) {
  ensure_root(parent);
  W = width; H = height;
  lv_obj_set_pos(root, 0, 0);
  lv_obj_set_size(root, W, H);
  lv_obj_remove_flag(root, LV_OBJ_FLAG_HIDDEN);
  active = true;
  if (dragging) return;  // Never rebuild under a finger.
  const auto *d = custom_logic_entity_sub::entity_detail(e);
  const auto sig = signature(e, d, v, title);
  if (sig == built_signature && !main_mode) { refresh_icons(); return; }
  clear();
  built_signature = sig;
  auto l = layout();
  // Shortcut button along the bottom of the control column.
  const std::string action = shortcut ? shortcut(e) : "";
  const bool has_shortcut = (action.rfind("script.", 0) == 0 || action.rfind("automation.", 0) == 0) &&
                            !has_prefix(e, "weather.") && !has_prefix(e, "select.") && !has_prefix(e, "input_select.");
  const auto *list = quick_actions ? quick_actions(e) : nullptr;
  if (list && list->count) {
    const int bh = P(48), y = l.ctl.bottom() - bh;
    auto *row = box(root, l.ctl.x, y, l.ctl.w, bh, BG(), 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(row, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_AUTO);
    int x = 0;
    for (int i = 0; i < list->count; ++i) {
      const std::string action = list->entries[i].action;
      const std::string name = list->entries[i].name;
      const int bw = std::max(P(120), std::min(l.ctl.w, int(name.size()) * P(10) + P(36)));
      button(row, x, 0, bw, bh - P(4), P(16), CONTROL(), name.empty() ? action : name, fonts.body, TEXT(),
             [action]() { if (run_quick_action) run_quick_action(action); });
      x += bw + P(8);
    }
    l.ctl.h -= bh + P(10);
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
  else if (has_prefix(e, "sensor.")) build_sensor(l, e, *d, title, v);
  else if (has_prefix(e, "number.") || has_prefix(e, "input_number.")) build_number(l, e, *d, title);
  else if (has_prefix(e, "select.") || has_prefix(e, "input_select.")) build_select(l, e, *d, title);
  else if (has_prefix(e, "weather.")) build_weather(e, *d, title);
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
  if (dragging) return;
  const std::string sig = "timers|" + std::to_string(W) + 'x' + std::to_string(H) + '|' + std::to_string(accent()) + '|' + std::to_string(BG()) + '|' +
                          timer_signature();
  if (sig == built_signature && !main_mode) return;
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
