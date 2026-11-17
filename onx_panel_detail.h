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
constexpr uint32_t BG = 0x101418, CARD = 0x232323, CONTROL = 0x2A2F35, TRACK = 0x414850,
                   TEXT = 0xFFFFFF, SOFT_TEXT = 0xD2D2D2, MUTED = 0x9C9C9E;
inline uint32_t accent() { return onx_display_settings::accent(); }
inline uint32_t strong() { return onx_display_settings::accent_strong(); }
inline uint32_t soft() { return onx_display_settings::accent_soft(); }

struct Fonts { const lv_font_t *small{nullptr}, *body{nullptr}, *title{nullptr}, *big{nullptr}, *symbol{nullptr}; };
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
inline lv_obj_t *text(lv_obj_t *parent, const std::string &s, const lv_font_t *font, uint32_t color, int x, int y,
                      int w = -1, lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_label_set_text(l, s.c_str());
  if (w > 0) {
    lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(l, align, 0);
  }
  lv_obj_set_pos(l, x, y);
  lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
  return l;
}
inline int line(const lv_font_t *font) { return lv_font_get_line_height(font); }
inline lv_obj_t *button(lv_obj_t *parent, int x, int y, int w, int h, int radius, uint32_t bg,
                        const std::string &caption, const lv_font_t *font, uint32_t fg, std::function<void()> fn) {
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
  lv_obj_t *b = button(parent, x, y, w, h, h / 2, selected ? soft() : CONTROL, caption, fonts.small,
                       selected ? accent() : SOFT_TEXT, std::move(fn));
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
  lv_obj_t *b = box(parent, r.x, r.y, r.w, r.h, CARD, P(10));
  const int pad = P(12), lh = line(fonts.small);
  text(b, name, fonts.small, MUTED, pad, (r.h - lh) / 2, r.w / 2 - pad);
  text(b, value, fonts.small, TEXT, r.w / 2, (r.h - lh) / 2, r.w / 2 - pad, LV_TEXT_ALIGN_RIGHT);
  if (fn) {
    interactive(b);
    on(b, LV_EVENT_CLICKED, [fn](lv_event_t *) { if (allowed()) fn(); });
  }
  return b;
}
// A vertical value tile: small muted name above a value.
inline void tile(lv_obj_t *parent, const Rect &r, const std::string &name, const std::string &value) {
  lv_obj_t *b = box(parent, r.x, r.y, r.w, r.h, CARD, P(10));
  const int pad = P(10);
  text(b, name, fonts.small, MUTED, pad, P(6), r.w - 2 * pad);
  text(b, value, fonts.body, TEXT, pad, r.h - line(fonts.body) - P(6), r.w - 2 * pad);
}
inline lv_obj_t *circle(lv_obj_t *parent, int cx, int cy, int size, uint32_t bg, const char *symbol, uint32_t fg,
                       std::function<void()> fn) {
  lv_obj_t *b = button(parent, cx - size / 2, cy - size / 2, size, size, LV_RADIUS_CIRCLE, bg, "", fonts.symbol, fg, std::move(fn));
  lv_obj_t *l = text(b, symbol, fonts.symbol, fg, 0, 0);
  lv_obj_center(l);
  return b;
}
inline void header(const Rect &r, const std::string &name, const std::string &context, int reserve = 0) {
  text(root, name, fonts.title, TEXT, r.x, r.y, r.w - reserve);
  text(root, context, fonts.small, MUTED, r.x, r.y + line(fonts.title) + P(2), r.w - reserve);
}
inline lv_point_t point() {
  lv_point_t p{0, 0};
  if (lv_indev_t *indev = lv_indev_active()) lv_indev_get_point(indev, &p);
  return p;
}
// Shared drag plumbing for custom sliders: live preview, commit on release.
inline void drag(lv_obj_t *obj, std::function<void(lv_point_t, bool commit)> fn) {
  interactive(obj);
  on(obj, LV_EVENT_PRESSED, [fn](lv_event_t *) { if (blocked && blocked()) return; dragging = true; fn(point(), false); });
  on(obj, LV_EVENT_PRESSING, [fn](lv_event_t *) { if (dragging) fn(point(), false); });
  on(obj, LV_EVENT_RELEASED, [fn](lv_event_t *) {
    if (!dragging) return;
    dragging = false;
    if (activity) activity();
    fn(point(), true);
  });
  on(obj, LV_EVENT_PRESS_LOST, [](lv_event_t *) { dragging = false; });
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
inline void build_light(const Layout &l, const std::string &e, const EntityDetailState &d, const std::string &title) {
  const auto s = light_state ? light_state(e) : esphome::onx_storage::NormalLightState{};
  const bool on = s.is_on && d.available;
  const bool dimmable = s.supports_brightness();
  float level = on ? s.brightness_pct : 0;
  const float want = pending_value(e, "brightness");
  clear_pending_if(e, "brightness", level);
  if (std::isfinite(want)) level = want;
  std::string context = !d.available ? "Unavailable" : on ? (dimmable ? "On · " + pct(level) : "On") : "Off";
  if (on && s.color_temperature_kelvin > 0 && s.supports_color_temperature())
    context += " · " + std::to_string(s.color_temperature_kelvin) + " K";
  const int pw = P(52);
  header(l.head, title, context, pw + P(8));
  circle(root, l.head.right() - pw / 2, l.head.y + pw / 2 - P(2), pw, on ? strong() : CONTROL, LV_SYMBOL_POWER,
        on ? TEXT : SOFT_TEXT, [e, on]() { request(e, "light.toggle", "", on ? "off" : "on", "panel_power"); });

  // Vertical brightness slider.
  const int sw = std::min(P(132), l.vis.w), sh = l.vis.h, sx = l.vis.x + (l.vis.w - sw) / 2, sy = l.vis.y;
  lv_obj_t *track = box(root, sx, sy, sw, sh, CONTROL, P(28));
  lv_obj_set_style_clip_corner(track, true, 0);
  const int fill_h = dimmable ? int(sh * std::clamp(level, 0.0f, 100.0f) / 100) : (on ? sh : 0);
  lv_obj_t *fill = box(track, 0, sh - fill_h, sw, fill_h, accent(), 0);
  lv_obj_t *value = text(track, dimmable ? pct(level) : (on ? "On" : "Off"), fonts.big, TEXT, 0, P(14), sw, LV_TEXT_ALIGN_CENTER);
  if (dimmable && d.available) {
    drag(track, [e, track, fill, value, sh, sw](lv_point_t p, bool commit) {
      lv_area_t a;
      lv_obj_get_coords(track, &a);
      const int v = std::clamp(int(std::lround((a.y2 - p.y) * 100.0f / sh)), 1, 100);
      const int h = sh * v / 100;
      lv_obj_set_pos(fill, 0, sh - h);
      lv_obj_set_size(fill, sw, h);
      lv_label_set_text(value, pct(v).c_str());
      if (!commit) return;
      set_pending(e, "brightness", v);
      request(e, "light.turn_on", "brightness_pct", std::to_string(v), "panel_brightness");
    });
  }

  // Mode chips and swatches.
  std::vector<std::string> labels;
  std::vector<int> modes;
  if (dimmable) { labels.push_back("Brightness"); modes.push_back(0); }
  if (s.supports_color_temperature()) { labels.push_back("White"); modes.push_back(1); }
  if (s.supports_rgb()) { labels.push_back("Colour"); modes.push_back(2); }
  const int mode = light_mode ? light_mode() : 0;
  const int ch = P(44), gap = P(12);
  int y = l.ctl.y;
  if (labels.size() > 1) {
    const int sel = std::find(modes.begin(), modes.end(), mode) - modes.begin();
    chips(root, {l.ctl.x, y, l.ctl.w, ch}, labels, sel, [modes](int i) { if (set_light_mode) set_light_mode(modes[i]); });
    y += ch + gap;
  }
  if (!d.available) return;
  struct Swatch { uint32_t color; std::string service, parameter, value; };
  std::vector<Swatch> swatches;
  if (mode == 2 && s.supports_rgb()) {
    const std::pair<const char *, uint32_t> names[] = {{"red", 0xFF4040}, {"orange", 0xFF9A3C}, {"green", 0x3CCB6E},
                                                       {"blue", 0x3C7BFF}, {"purple", 0xA45CFF}};
    for (const auto &n : names) swatches.push_back({n.second, "light.turn_on", "color_name", n.first});
  } else if (s.supports_color_temperature()) {
    const int lo = s.min_color_temperature_kelvin ? s.min_color_temperature_kelvin : 2200;
    const int hi = s.max_color_temperature_kelvin > lo ? s.max_color_temperature_kelvin : 6500;
    for (int i = 0; i < 5; ++i) {
      const int k = (lo + (hi - lo) * i / 4) / 100 * 100;
      swatches.push_back({onx_light::kelvin(k), "light.turn_on", "color_temp_kelvin", std::to_string(k)});
    }
  }
  if (swatches.empty() || y + ch > l.ctl.bottom() + P(4)) return;
  const int n = swatches.size(), size = std::min(ch, (l.ctl.w - gap * (n - 1)) / n);
  const int step = n > 1 ? (l.ctl.w - size) / (n - 1) : 0;
  for (int i = 0; i < n; ++i) {
    const auto sw_ = swatches[i];
    lv_obj_t *b = button(root, l.ctl.x + i * step, y, size, size, LV_RADIUS_CIRCLE, sw_.color, "", fonts.small, TEXT,
                         [e, sw_]() { request(e, sw_.service, sw_.parameter, sw_.value, "panel_colour"); });
    const bool current = sw_.parameter == "color_temp_kelvin" && on && std::abs(s.color_temperature_kelvin - std::stoi(sw_.value)) < 150;
    if (current) {
      lv_obj_set_style_border_width(b, P(3), 0);
      lv_obj_set_style_border_color(b, lv_color_hex(TEXT), 0);
    }
  }
}

inline std::string hvac_label(const std::string &m) {
  if (m == "heat_cool") return "Auto";
  if (m == "fan_only") return "Fan";
  return label(m);
}
inline void build_climate(const Layout &l, const std::string &e, const EntityDetailState &d, const std::string &title) {
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
  lv_obj_set_style_arc_color(arc, lv_color_hex(CONTROL), LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, lv_color_hex(mode == "off" ? TRACK : accent()), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(arc, lv_color_hex(TEXT), LV_PART_KNOB);
  lv_obj_set_style_pad_all(arc, P(4), LV_PART_KNOB);
  lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(arc, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);
  lv_obj_remove_flag(arc, LV_OBJ_FLAG_SCROLLABLE);
  const std::string big = range ? fmt(num(effective(d, "target_temp_low"))) + "–" + fmt(num(effective(d, "target_temp_high"))) + "°"
                                : fmt(target) + "°";
  lv_obj_t *value = text(root, big, fonts.big, TEXT, cx - size / 2, ay + size / 2 - line(fonts.big) / 2 - P(8), size, LV_TEXT_ALIGN_CENTER);
  text(root, "Now " + fmt(num(d.current)) + " " + unit, fonts.small, SOFT_TEXT, cx - size / 2,
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
  circle(root, cx - size / 2 + bs / 2, by, bs, CONTROL, LV_SYMBOL_MINUS, TEXT, [stepper]() { stepper(-1); });
  circle(root, cx + size / 2 - bs / 2, by, bs, CONTROL, LV_SYMBOL_PLUS, TEXT, [stepper]() { stepper(1); });

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

inline void build_fan(const Layout &l, const std::string &e, const EntityDetailState &d, const std::string &title) {
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
  circle(root, l.head.right() - pw / 2, l.head.y + pw / 2 - P(2), pw, on ? strong() : CONTROL, LV_SYMBOL_POWER,
        on ? TEXT : SOFT_TEXT, [e]() { onx_controls::press(e); });
  text(root, on && std::isfinite(level) ? pct(level) : (on ? "On" : "Off"), fonts.big, TEXT, l.vis.x, l.vis.y + P(8), l.vis.w,
       LV_TEXT_ALIGN_CENTER);
  if (n) {
    const int gap = P(10), bw = (l.vis.w - gap * (n - 1)) / n, base = l.vis.bottom() - P(8);
    const int top = l.vis.y + line(fonts.big) + P(28), span = base - top;
    for (int i = 0; i < n; ++i) {
      const int h = span * (i + 2) / (n + 1);
      button(root, l.vis.x + i * (bw + gap), base - h, bw, h, P(10), i < filled ? accent() : CONTROL, "",
                           fonts.small, TEXT, [e, i, n]() {
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

inline void build_cover(const Layout &l, const std::string &e, const EntityDetailState &d, const std::string &title) {
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
  lv_obj_set_style_border_color(frame, lv_color_hex(CONTROL), 0);
  lv_obj_set_style_clip_corner(frame, true, 0);
  const int inner = fh - 2 * border, iw = fw - 2 * border;
  const int closed = known ? int(inner * (100 - std::clamp(position, 0.0f, 100.0f)) / 100) : inner / 2;
  lv_obj_t *blind = box(frame, 0, 0, iw, closed, 0x4A525B, 0);
  for (int y = P(10); y < closed - P(6); y += P(14)) box(blind, 0, y, iw, P(2), 0x3A4148, 0);
  lv_obj_t *edge = box(frame, 0, std::max(0, closed - P(4)), iw, P(4), accent(), 0);
  lv_obj_t *value = text(root, known ? pct(position) : label(d.state), fonts.big, TEXT, l.vis.x, fy + fh + P(8), l.vis.w,
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
    button(root, l.ctl.x + i * (bw + gap), l.ctl.y, bw, bh, P(14), hot ? strong() : CONTROL, b.caption, fonts.body, TEXT,
           [e, b]() { request(e, b.service, "", "", "panel_cover"); });
  }
}

inline void build_toggle(const Layout &l, const std::string &e, const EntityDetailState &d, const std::string &title) {
  const bool on = d.state == "on";
  const auto domain = e.substr(0, e.find('.'));
  header(l.head, title, d.available ? (on ? "On" : "Off") : "Unavailable");
  const int size = std::min({P(150), l.vis.w, l.vis.h - line(fonts.big) - P(16)});
  const int cx = l.vis.x + l.vis.w / 2, cy = l.vis.y + (l.vis.h - line(fonts.big) - P(12)) / 2;
  circle(root, cx, cy, size, on ? strong() : CONTROL, LV_SYMBOL_POWER, on ? TEXT : SOFT_TEXT,
        [e, domain, on]() { request(e, domain + ".toggle", "", on ? "off" : "on", "panel_power"); });
  text(root, on ? "On" : "Off", fonts.big, TEXT, l.vis.x, cy + size / 2 + P(10), l.vis.w, LV_TEXT_ALIGN_CENTER);
  // Smart plugs: power reading and graph from the configured secondary sensor.
  const int i = onx_sensor_view::index(e);
  if (i < 0 || !onx_sensor_view::configs[i].secondary[0]) return;
  const auto &s = onx_sensor_view::states[i];
  const int th = P(56);
  tile(root, {l.ctl.x, l.ctl.y, l.ctl.w, th}, s.unit == "kWh" ? "Energy" : "Power",
       s.received && s.available ? onx_detail_view::compact(s.value) + " " + s.unit : "—");
  if (onx_sensor_view::configs[i].graph && draw_graph && l.ctl.h >= th + P(70)) draw_graph(root, e, l.ctl.y + th + P(8));
}

inline void build_action(const Layout &l, const std::string &e, const EntityDetailState &d, const std::string &title) {
  const bool script = has_prefix(e, "script."), scene = has_prefix(e, "scene.");
  const bool running = script && d.state == "on";
  const std::string kind = script ? "Script" : scene ? "Scene" : "Button";
  header(l.head, title, kind + (running ? " · Running…" : ""));
  const int size = std::min({P(170), l.vis.w, l.vis.h - P(40)});
  const int cx = l.vis.x + l.vis.w / 2, cy = l.vis.y + (l.vis.h - P(30)) / 2;
  const bool ring = esphome::millis() - ran_at < 1500 || running;
  lv_obj_t *b = circle(root, cx, cy, size, strong(), LV_SYMBOL_PLAY, TEXT, [e, script]() {
    ran_at = esphome::millis();
    if (script) request(e, "script.turn_on", "", "on", "panel_run");
    else onx_controls::press(e);
  });
  if (ring) {
    lv_obj_set_style_border_width(b, P(6), 0);
    lv_obj_set_style_border_color(b, lv_color_hex(accent()), 0);
  }
  text(root, running ? "Running…" : "Tap to run", fonts.body, SOFT_TEXT, l.vis.x, cy + size / 2 + P(12), l.vis.w,
       LV_TEXT_ALIGN_CENTER);
}

inline void build_timer(const Layout &l, const std::string &e, const EntityDetailState &d, const std::string &title,
                        const onx_detail_view::View &v) {
  header(l.head, title, v.context.text);
  const int size = std::min({l.vis.w, l.vis.h, P(240)});
  const int cx = l.vis.x + l.vis.w / 2, ay = l.vis.y + (l.vis.h - size) / 2;
  lv_obj_t *arc = lv_arc_create(root);
  lv_obj_set_size(arc, size, size);
  lv_obj_set_pos(arc, cx - size / 2, ay);
  lv_arc_set_rotation(arc, 270);
  lv_arc_set_bg_angles(arc, 0, 360);
  lv_arc_set_range(arc, 0, 1000);
  lv_arc_set_value(arc, std::clamp(v.value_ring, 0, 1000));
  lv_obj_set_style_arc_width(arc, P(12), LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, P(12), LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, lv_color_hex(CONTROL), LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, lv_color_hex(accent()), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
  lv_obj_set_style_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  text(root, v.value.text, fonts.big, TEXT, cx - size / 2, ay + size / 2 - line(fonts.big) / 2 - P(6), size, LV_TEXT_ALIGN_CENTER);
  const double total = onx_timer::duration_seconds(d.timer_duration);
  if (total > 0)
    text(root, "of " + onx_timer::countdown(total), fonts.small, MUTED, cx - size / 2, ay + size / 2 + line(fonts.big) / 2,
         size, LV_TEXT_ALIGN_CENTER);
  if (!has_prefix(e, "timer.") || !d.available) return;  // Voice timers are display-only.
  const int gap = P(10), bh = P(48), half = (l.ctl.w - gap) / 2;
  const bool running = d.state == "active", paused = d.state == "paused";
  if (running || paused) {
    button(root, l.ctl.x, l.ctl.y, half, bh, bh / 2, CONTROL, "+1 min", fonts.body, TEXT,
           [e]() { request(e, "timer.change", "duration", "00:01:00", "panel_timer"); });
    button(root, l.ctl.x + half + gap, l.ctl.y, l.ctl.w - half - gap, bh, bh / 2, strong(), running ? "Pause" : "Resume",
           fonts.body, TEXT, [e, running]() { request(e, running ? "timer.pause" : "timer.start", "", "", "panel_timer"); });
    if (l.ctl.h >= 2 * bh + gap)
      button(root, l.ctl.x, l.ctl.y + bh + gap, l.ctl.w, bh, bh / 2, CONTROL, "Cancel timer", fonts.body, SOFT_TEXT,
             [e]() { request(e, "timer.cancel", "", "", "panel_timer"); });
  } else {
    button(root, l.ctl.x, l.ctl.y, l.ctl.w, bh, bh / 2, strong(), d.state == "idle" ? "Start" : "Dismiss", fonts.body, TEXT,
           [e]() { request(e, "timer.start", "", "", "panel_timer"); });
  }
}

inline void build_sensor(const Layout &l, const std::string &e, const EntityDetailState &d, const std::string &title,
                         const onx_detail_view::View &v) {
  const auto kind = onx_detail_view::sensor_class(e);
  std::string context = kind.empty() ? "Sensor" : label(kind);
  const int i = onx_sensor_view::index(e);
  if (i >= 0 && onx_sensor_view::configs[i].secondary[0]) context = v.name.text;
  header(l.head, title, context);
  const float value = num(d.state);
  const std::string reading = std::isfinite(value) ? onx_detail_view::compact(d.state) : v.value.text;
  lv_obj_t *big = text(root, reading, fonts.big, TEXT, l.vis.x, l.vis.y, l.vis.w - P(8));
  if (std::isfinite(value) && !d.unit.empty()) {
    (void) big;
    lv_point_t size;
    lv_text_get_size(&size, reading.c_str(), fonts.big, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const int w = size.x;
    text(root, " " + d.unit, fonts.body, SOFT_TEXT, l.vis.x + w, l.vis.y + line(fonts.big) - line(fonts.body) - P(4));
  }
  const bool graph = i >= 0 && onx_sensor_view::configs[i].graph;
  std::vector<onx_sensor_view::Point> history;
  if (i >= 0) history = onx_sensor_view::states[i].history(std::time(nullptr));
  int y = l.vis.y + line(fonts.big) + P(4);
  if (history.size() >= 2) {
    const float delta = history.back().value - history.front().value;
    const std::string arrow = delta > 0 ? "▲ " : delta < 0 ? "▼ " : "";
    text(root, arrow + fmt(std::fabs(delta), 1) + " in the last 12 h", fonts.small, accent(), l.vis.x, y, l.vis.w);
  } else if (graph) {
    text(root, "Collecting history…", fonts.small, MUTED, l.vis.x, y, l.vis.w);
  }
  y += line(fonts.small) + P(16);
  if (graph && draw_graph) {
    lv_obj_t *frame = box(root, l.vis.x, y, l.vis.w, std::max(P(90), l.vis.bottom() - y), CARD, P(10));
    draw_graph(frame, e, P(12));
    text(frame, "−12 h", fonts.small, MUTED, P(10), lv_obj_get_style_height(frame, LV_PART_MAIN) - line(fonts.small) - P(6));
    text(frame, "now", fonts.small, MUTED, l.vis.w - P(40), lv_obj_get_style_height(frame, LV_PART_MAIN) - line(fonts.small) - P(6));
  }
  if (history.size() >= 2) {
    float lo = history.front().value, hi = lo, sum = 0;
    for (const auto &p : history) { lo = std::min(lo, p.value); hi = std::max(hi, p.value); sum += p.value; }
    const int gap = P(10), tw = (l.ctl.w - 2 * gap) / 3, th = std::min(P(60), l.ctl.h);
    tile(root, {l.ctl.x, l.ctl.y, tw, th}, "Min", fmt(lo, 1));
    tile(root, {l.ctl.x + tw + gap, l.ctl.y, tw, th}, "Avg", fmt(sum / history.size(), 1));
    tile(root, {l.ctl.x + 2 * (tw + gap), l.ctl.y, l.ctl.w - 2 * (tw + gap), th}, "Max", fmt(hi, 1));
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
inline void build_binary(const Layout &l, const std::string &e, const EntityDetailState &d, const std::string &title) {
  const auto kind = onx_detail_view::sensor_class(e);
  const bool on = d.state == "on";
  header(l.head, title, kind.empty() ? "Binary sensor" : label(kind) + " sensor");
  const int size = std::min({P(170), l.vis.w, l.vis.h - P(40)});
  const int cx = l.vis.x + l.vis.w / 2, cy = l.vis.y + (l.vis.h - P(30)) / 2;
  lv_obj_t *disc = box(root, cx - size / 2, cy - size / 2, size, size, on ? soft() : CARD, LV_RADIUS_CIRCLE);
  lv_obj_set_style_border_width(disc, P(3), 0);
  lv_obj_set_style_border_color(disc, lv_color_hex(on ? accent() : CONTROL), 0);
  lv_obj_t *state = text(disc, binary_text(kind, d.state), fonts.body, on ? accent() : TEXT, 0, 0, size - P(16), LV_TEXT_ALIGN_CENTER);
  lv_obj_center(state);
  const auto *c = changes_for(e);
  const time_t now = std::time(nullptr);
  if (c && !c->items.empty())
    text(root, ago(c->items.front().at, now), fonts.small, MUTED, l.vis.x, cy + size / 2 + P(10), l.vis.w, LV_TEXT_ALIGN_CENTER);
  if (!c || c->items.empty()) {
    text(root, "Changes appear here while the panel is running", fonts.small, MUTED, l.ctl.x, l.ctl.y, l.ctl.w);
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

inline void build_number(const Layout &l, const std::string &e, const EntityDetailState &d, const std::string &title) {
  float value = num(onx_controls::effective(d, "state"));
  clear_pending_if(e, "value", value);
  const float want = pending_value(e, "value");
  if (std::isfinite(want)) value = want;
  const float lo = std::isfinite(d.minimum) ? d.minimum : 0, hi = std::isfinite(d.maximum) && d.maximum > lo ? d.maximum : 100;
  const float step = std::isfinite(d.step) && d.step > 0 ? d.step : 1;
  header(l.head, title, fmt(lo) + " – " + fmt(hi) + " · step " + fmt(step));
  lv_obj_t *big = text(root, fmt(value), fonts.big, TEXT, l.vis.x, l.vis.y + l.vis.h / 5, l.vis.w, LV_TEXT_ALIGN_CENTER);
  if (!d.unit.empty())
    text(root, d.unit, fonts.body, SOFT_TEXT, l.vis.x, l.vis.y + l.vis.h / 5 + line(fonts.big), l.vis.w, LV_TEXT_ALIGN_CENTER);
  // Horizontal slider: a tall invisible hit area around an 8 px track.
  const int sy = l.vis.y + l.vis.h * 64 / 100, hit = P(48), kw = P(28);
  lv_obj_t *area = box(root, l.vis.x, sy - hit / 2, l.vis.w, hit, BG, 0, LV_OPA_TRANSP);
  const int tw = l.vis.w - kw;
  box(area, kw / 2, (hit - P(8)) / 2, tw, P(8), TRACK, P(4));
  const float frac = std::isfinite(value) ? std::clamp((value - lo) / (hi - lo), 0.0f, 1.0f) : 0;
  lv_obj_t *fill = box(area, kw / 2, (hit - P(8)) / 2, int(tw * frac), P(8), accent(), P(4));
  lv_obj_t *knob = box(area, int(tw * frac), (hit - kw) / 2, kw, kw, TEXT, LV_RADIUS_CIRCLE);
  text(root, fmt(lo), fonts.small, MUTED, l.vis.x, sy + hit / 2);
  text(root, fmt(hi), fonts.small, MUTED, l.vis.x, sy + hit / 2, l.vis.w, LV_TEXT_ALIGN_RIGHT);
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
  circle(root, l.ctl.x + bs / 2, cy, bs, CONTROL, LV_SYMBOL_MINUS, TEXT, [stepper]() { stepper(-1); });
  circle(root, l.ctl.right() - bs / 2, cy, bs, CONTROL, LV_SYMBOL_PLUS, TEXT, [stepper]() { stepper(1); });
  if (l.ctl.h >= bs + P(28))
    text(root, "Swipe up / down also steps by " + fmt(step), fonts.small, MUTED, l.ctl.x, l.ctl.y + bs + P(8), l.ctl.w,
         LV_TEXT_ALIGN_CENTER);
}

inline void build_select(const Layout &l, const std::string &e, const EntityDetailState &d, const std::string &title) {
  const auto options = onx_control::options(d.options);
  const auto current = onx_controls::effective(d, "state");
  header(l.head, title, std::to_string(options.size()) + " options");
  // In landscape the list uses the right column; in portrait everything below the name.
  const Rect r = wide() ? Rect{l.ctl.x, l.ctl.y, l.ctl.w, l.ctl.h} : Rect{l.vis.x, l.vis.y, l.vis.w, H - P(16) - l.vis.y};
  lv_obj_t *list = box(root, r.x, r.y, r.w, r.h, BG, 0, LV_OPA_TRANSP);
  lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
  interactive(list);  // Scroll gestures go to LVGL.
  const int bh = P(52), gap = P(8);
  for (size_t i = 0; i < options.size(); ++i) {
    const auto option = options[i];
    const bool selected = option == current;
    lv_obj_t *b = button(list, 0, i * (bh + gap), r.w, bh, P(12), selected ? soft() : CARD, "", fonts.body, TEXT,
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
    text(b, option, fonts.body, selected ? accent() : TEXT, P(16), (bh - line(fonts.body)) / 2, r.w - P(64));
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
inline void build_weather(const std::string &e, const EntityDetailState &d, const std::string &title) {
  auto reading = [](const std::string &v, const std::string &unit) {
    return v.empty() || v == "None" || v == "unknown" || v == "unavailable" ? std::string("—") : onx_detail_view::compact(v) + unit;
  };
  const int pad = P(16);
  const Rect left = wide() ? Rect{pad, pad, W * 46 / 100 - pad, H - 2 * pad} : Rect{pad, pad, W - 2 * pad, H - 2 * pad};
  text(root, title, fonts.title, TEXT, left.x, left.y, left.w);
  int y = left.y + line(fonts.title) + P(6);
  text(root, reading(d.target, "°"), fonts.big, TEXT, left.x, y, left.w);
  y += line(fonts.big) + P(2);
  std::string summary = condition(d.state) + " · Feels " + reading(d.apparent_temperature, "°");
  if (d.forecast_count) summary += " · H " + reading(d.forecast[0].high, "°") + " L " + reading(d.forecast[0].low, "°");
  text(root, summary, fonts.small, SOFT_TEXT, left.x, y, left.w);
  y += line(fonts.small) + P(14);
  const std::string rain = d.forecast_count ? reading(d.forecast[0].rain, "%") : "—";
  const std::pair<std::string, std::string> stats[] = {
      {"Humidity", reading(d.humidity, "%")}, {"Wind", reading(d.wind_speed, " " + d.wind_unit)}, {"Rain", rain}};
  const int gap = P(8), th = P(56);
  if (wide()) {
    for (const auto &s : stats) {
      if (y + P(40) > left.bottom()) break;
      row(root, {left.x, y, left.w, P(40)}, s.first, s.second);
      y += P(40) + P(6);
    }
  } else {
    const int tw = (left.w - 2 * gap) / 3;
    for (int i = 0; i < 3; ++i) tile(root, {left.x + i * (tw + gap), y, i == 2 ? left.w - 2 * (tw + gap) : tw, th}, stats[i].first, stats[i].second);
    y += th + P(14);
  }
  const Rect list = wide() ? Rect{W * 46 / 100 + P(12), pad, W - W * 46 / 100 - P(12) - pad, H - 2 * pad}
                           : Rect{pad, y, W - 2 * pad, H - pad - y};
  lv_obj_t *panel = box(root, list.x, list.y, list.w, list.h, CARD, P(12));
  if (!d.forecast_count) {
    const bool timed_out = d.forecast_pending && esphome::millis() - d.forecast_requested_ms >= 20000;
    text(panel, d.forecast_pending && !timed_out ? "Loading forecast…" : "Forecast not available", fonts.small, MUTED, P(12),
         P(12), list.w - P(24));
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
  const int n = std::min(d.forecast_count, 5), rh = (list.h - P(8)) / n;
  const int dw = P(52), cw = wide() ? 0 : P(84), tw = P(36), bx = P(12) + dw + cw + tw + P(6);
  const int bw = list.w - bx - tw - P(18);
  for (int i = 0; i < n; ++i) {
    const auto &f = d.forecast[i];
    const int ry = P(4) + i * rh + (rh - line(fonts.small)) / 2;
    text(panel, weekday(f.date, today), fonts.small, TEXT, P(12), ry, dw);
    if (cw) text(panel, condition(f.condition), fonts.small, MUTED, P(12) + dw, ry, cw - P(4));
    text(panel, reading(f.low, "°"), fonts.small, MUTED, P(12) + dw + cw, ry, tw, LV_TEXT_ALIGN_RIGHT);
    text(panel, reading(f.high, "°"), fonts.small, TEXT, list.w - tw - P(12), ry, tw, LV_TEXT_ALIGN_RIGHT);
    const int by = P(4) + i * rh + rh / 2 - P(3);
    box(panel, bx, by, bw, P(6), CONTROL, P(3));
    const float a = num(f.low), b = num(f.high);
    if (std::isfinite(a) && std::isfinite(b) && hi > lo) {
      const int x0 = int(bw * (a - lo) / (hi - lo)), x1 = int(bw * (b - lo) / (hi - lo));
      box(panel, bx + x0, by, std::max(P(6), x1 - x0), P(6), accent(), P(3));
    }
  }
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
  std::string s = e + '|' + title + '|' + std::to_string(W) + 'x' + std::to_string(H) + '|' + std::to_string(accent());
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
  s += '|' + v.value.text + '|' + v.context.text + '|' + v.name.text + '|' + std::to_string(v.value_ring);
  if (has_prefix(e, "light.") && light_state) {
    const auto l = light_state(e);
    s += '|' + std::to_string(l.is_on) + ',' + std::to_string(l.brightness_pct) + ',' + std::to_string(l.color_temperature_kelvin) +
         ',' + std::to_string(light_mode ? light_mode() : 0);
  }
  const int i = onx_sensor_view::index(e);
  if (i >= 0) {
    const auto &st = onx_sensor_view::states[i];
    s += '|' + st.value + st.unit + std::to_string(st.history(std::time(nullptr)).size());
  }
  if (const auto *c = changes_for(e)) s += '|' + std::to_string(c->items.size()) + ',' + std::to_string(std::time(nullptr) / 60);
  if (pending.entity == e) s += '|' + std::to_string(pending_value(e, pending.key));
  s += '|' + std::to_string(esphome::millis() - ran_at < 1500);
  if (shortcut) s += '|' + shortcut(e);
  return s;
}

inline void detach_graph() {
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
  auto &t = onx_panel_ui::touch_targets;
  for (auto *o : targets) t.erase(std::remove(t.begin(), t.end(), o), t.end());
  targets.clear();
  if (root != nullptr) lv_obj_clean(root);
  actions.clear();
  dragging = false;
  built_signature.clear();
}
inline void hide() {
  if (!active) return;
  active = false;
  clear();
  if (root != nullptr) lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
}

// Draws (or keeps) the redesigned detail for `entity` inside `parent`.
inline void render(lv_obj_t *parent, const std::string &e, const std::string &title, const onx_detail_view::View &v,
                   int width, int height) {
  if (root == nullptr) {
    root = lv_obj_create(parent);
    lv_obj_remove_flag(root, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
  }
  W = width; H = height;
  lv_obj_set_pos(root, 0, 0);
  lv_obj_set_size(root, W, H);
  lv_obj_remove_flag(root, LV_OBJ_FLAG_HIDDEN);
  active = true;
  if (dragging) return;  // Never rebuild under a finger.
  const auto *d = custom_logic_entity_sub::entity_detail(e);
  const auto sig = signature(e, d, v, title);
  if (sig == built_signature) return;
  clear();
  built_signature = sig;
  auto l = layout();
  // Shortcut button along the bottom of the control column.
  const std::string action = shortcut ? shortcut(e) : "";
  const bool has_shortcut = (action.rfind("script.", 0) == 0 || action.rfind("automation.", 0) == 0) &&
                            !has_prefix(e, "weather.") && !has_prefix(e, "select.") && !has_prefix(e, "input_select.");
  if (has_shortcut) {
    const int bh = P(48), y = l.ctl.bottom() - bh;
    std::string name = shortcut_title ? shortcut_title(action) : "";
    if (name.empty()) name = action;
    button(root, l.ctl.x, y, l.ctl.w, bh, bh / 2, CONTROL, std::string(LV_SYMBOL_PLAY) + "  " + name, fonts.body, TEXT,
           []() { if (run_shortcut) run_shortcut(); });
    l.ctl.h -= bh + P(10);
    if (!wide() && l.ctl.h < P(56)) {  // Tight portrait screens give space from the visual block.
      const int need = P(56) - l.ctl.h;
      l.vis.h -= need; l.ctl.y -= need; l.ctl.h += need;
    }
  }
  if (d == nullptr || !d->received || !d->available) {
    header(l.head, title, d == nullptr || !d->received ? "Waiting for initial data" : "Unavailable");
    text(root, d == nullptr || !d->received ? "…" : "—", fonts.big, MUTED, l.vis.x, l.vis.y + l.vis.h / 3, l.vis.w, LV_TEXT_ALIGN_CENTER);
    return;
  }
  if (custom_logic_entity_sub::is_timer_detail(e)) build_timer(l, e, *d, title, v);
  else if (has_prefix(e, "light.")) build_light(l, e, *d, title);
  else if (has_prefix(e, "climate.")) build_climate(l, e, *d, title);
  else if (has_prefix(e, "fan.")) build_fan(l, e, *d, title);
  else if (has_prefix(e, "cover.")) build_cover(l, e, *d, title);
  else if (has_prefix(e, "scene.") || has_prefix(e, "script.") || has_prefix(e, "button.") || has_prefix(e, "input_button."))
    build_action(l, e, *d, title);
  else if (has_prefix(e, "binary_sensor.")) build_binary(l, e, *d, title);
  else if (has_prefix(e, "sensor.")) build_sensor(l, e, *d, title, v);
  else if (has_prefix(e, "number.") || has_prefix(e, "input_number.")) build_number(l, e, *d, title);
  else if (has_prefix(e, "select.") || has_prefix(e, "input_select.")) build_select(l, e, *d, title);
  else if (has_prefix(e, "weather.")) build_weather(e, *d, title);
  else build_toggle(l, e, *d, title);
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
