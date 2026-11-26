#pragma once
// Idle clock layout for the rectangular panels.
//
// The shared standby page places its widgets for a round 240 px screen. This
// rearranges them for the full rectangle and adds what the panel already knows:
//   portrait:  status icons (left) and notification count (right) on top, a
//              large clock and date, current weather with humidity, today's
//              high / low / rain, a forecast strip and a now-playing card
//   landscape: clock, date and now-playing on the left; weather, today and a
//              forecast list in a right-hand column
// Optional parts (forecast, now playing) hide when there is no data, and the
// clock block moves to the middle of the free space. Timers are not shown here:
// the timer screen covers the idle clock while one runs.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <string>
#include <vector>
#include "lvgl.h"
#include "esphome/core/hal.h"
#include "onx_geom.h"
#include "onx_render.h"
#include "onx_display_settings.h"
#include "onx_panel_ui.h"
#include "custom_logic_entity_sub.h"
#include "onx_clock_sensors.h"

namespace onx_idle_clock {
using onx_geom::S;

// ------------------------------------------------------------ forecast data
// Kept apart from the weather page's detail state: the idle weather entity need
// not be on any page.
struct Day { std::string date, condition, high, low, rain; };
inline std::array<Day, 6> days{};
inline int day_count = 0;
inline std::string forecast_entity;
inline uint32_t forecast_ms = 0, requested_ms = 0;
inline bool pending = false;
constexpr uint32_t REFRESH_MS = 30u * 60u * 1000u, RETRY_MS = 5u * 60u * 1000u, TIMEOUT_MS = 30000;

// True when a new forecast request should start for `entity` (a weather.* id).
inline bool forecast_due(const std::string &entity, uint32_t now) {
  if (entity.rfind("weather.", 0) != 0) { day_count = 0; forecast_entity.clear(); return false; }
  if (entity != forecast_entity) { forecast_entity = entity; day_count = 0; forecast_ms = 0; pending = false; requested_ms = 0; }
  if (pending && now - requested_ms < TIMEOUT_MS) return false;
  if (requested_ms != 0 && now - requested_ms < (day_count ? REFRESH_MS : RETRY_MS)) return false;
  pending = true;
  requested_ms = now;
  return true;
}
inline void forecast_received(const std::string &entity) {
  if (entity != forecast_entity) return;
  pending = false;
  forecast_ms = esphome::millis();
}
inline void forecast_failed(const std::string &entity) {
  if (entity == forecast_entity) pending = false;
}

// ------------------------------------------------------------ text helpers
inline std::string condition_text(const std::string &state) {
  static const char *const names[][2] = {
      {"clear-night", "Clear"}, {"cloudy", "Cloudy"}, {"exceptional", "Exceptional"}, {"fog", "Fog"},
      {"hail", "Hail"}, {"lightning", "Thunderstorm"}, {"lightning-rainy", "Storm & rain"},
      {"partlycloudy", "Partly cloudy"}, {"pouring", "Pouring"}, {"rainy", "Rainy"}, {"snowy", "Snowy"},
      {"snowy-rainy", "Sleet"}, {"sunny", "Sunny"}, {"windy", "Windy"}, {"windy-variant", "Windy"}};
  for (const auto &n : names)
    if (state == n[0]) return n[1];
  return "";
}
// "2026-11-20..." -> "Fri"; empty when the date cannot be read.
inline std::string weekday(const std::string &date) {
  int y = 0, m = 0, d = 0;
  if (date.size() < 10 || std::sscanf(date.c_str(), "%4d-%2d-%2d", &y, &m, &d) != 3 || m < 1 || m > 12 || d < 1 || d > 31)
    return "";
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  static const char *const names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  return names[(y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7];
}
inline std::string degrees(const std::string &v) { return v.empty() ? "--" : v + "\xC2\xB0"; }
inline int line(const lv_font_t *f) { return f ? lv_font_get_line_height(f) : 16; }
inline int text_width(const char *s, const lv_font_t *font) {
  lv_point_t size;
  lv_text_get_size(&size, s, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return size.x;
}
inline void set_text(lv_obj_t *label, const std::string &value) {
  if (label != nullptr && std::strcmp(lv_label_get_text(label), value.c_str()) != 0) lv_label_set_text(label, value.c_str());
}
inline void at(lv_obj_t *obj, int x, int y) { onx_panel_ui::place(obj, LV_ALIGN_TOP_LEFT, x, y); }
inline void show(lv_obj_t *obj, bool visible) { onx_panel_ui::show(obj, visible); }
inline void color(lv_obj_t *obj, uint32_t c) { onx_render::style_text_color(obj, lv_color_hex(c), 0); }
// Setters that leave unchanged objects alone (no style refresh or redraw).
inline void size(lv_obj_t *obj, int w, int h) {
  if (obj == nullptr) return;
  if (lv_obj_get_style_width(obj, LV_PART_MAIN) != w) lv_obj_set_width(obj, w);
  if (lv_obj_get_style_height(obj, LV_PART_MAIN) != h) lv_obj_set_height(obj, h);
}
inline void width(lv_obj_t *obj, int w) { onx_panel_ui::set_width(obj, w); }
inline void align_text(lv_obj_t *obj, lv_text_align_t a) {
  if (obj != nullptr && lv_obj_get_style_text_align(obj, LV_PART_MAIN) != a) lv_obj_set_style_text_align(obj, a, 0);
}

// ------------------------------------------------------------ widgets
struct Base {  // The shared standby page's widgets.
  lv_obj_t *wifi_on{}, *wifi_off{}, *ha_on{}, *ha_off{};
  std::vector<lv_obj_t *> weather_icons;
  lv_obj_t *temp_icon{}, *temp{}, *humidity_icon{}, *humidity{}, *date{}, *time{};
};
struct Fonts {
  const lv_font_t *tiny{}, *small{}, *body{}, *value{};
  const lv_font_t *clock{}, *clock_big{}, *clock_ampm{}, *date{}, *date_big{};
};
struct Column { lv_obj_t *day{}, *icon{}, *high{}, *low{}; };

inline Base base;
inline Fonts fonts;
// Weather condition -> its image (the standby page's icon set).
inline std::function<const void *(const std::string &)> condition_image;
inline lv_obj_t *condition = nullptr, *today = nullptr, *humidity_caption = nullptr, *forecast = nullptr;
inline std::array<Column, 5> columns{};
inline lv_obj_t *media = nullptr, *media_title = nullptr, *media_sub = nullptr, *media_bar = nullptr;
inline uint32_t last_update = 0;

inline lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t c) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
  lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
  lv_label_set_text(l, "");
  return l;
}
inline lv_obj_t *plain(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  return o;
}

inline void create(lv_obj_t *page) {
  if (condition != nullptr) return;
  const uint32_t muted = onx_display_settings::muted_text(), fg = onx_display_settings::foreground();
  condition = label(page, fonts.small, muted);
  humidity_caption = label(page, fonts.small, muted);
  lv_label_set_text(humidity_caption, "Humidity");
  today = label(page, fonts.small, muted);
  forecast = plain(page);
  for (auto &c : columns) {
    c.day = label(forecast, fonts.small, muted);
    c.icon = lv_image_create(forecast);
    lv_obj_remove_flag(c.icon, LV_OBJ_FLAG_CLICKABLE);
    lv_image_set_inner_align(c.icon, LV_IMAGE_ALIGN_CONTAIN);
    c.high = label(forecast, fonts.body, fg);
    c.low = label(forecast, fonts.small, muted);
  }
  media = plain(page);
  lv_obj_set_style_bg_opa(media, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(media, S(12), 0);
  media_title = label(media, fonts.body, fg);
  media_sub = label(media, fonts.tiny, muted);
  for (auto *l : {media_title, media_sub}) lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_height(media_title, line(fonts.body));
  lv_obj_set_height(media_sub, line(fonts.tiny));
  media_bar = lv_bar_create(media);
  lv_obj_set_style_bg_opa(media_bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_bar_set_range(media_bar, 0, 1000);
  lv_obj_set_style_radius(media_bar, S(2), LV_PART_MAIN);
  lv_obj_set_style_radius(media_bar, S(2), LV_PART_INDICATOR);
  lv_obj_remove_flag(media_bar, LV_OBJ_FLAG_CLICKABLE);
  // The temperature and humidity read as the main values on this layout.
  for (auto *l : {base.temp, base.humidity})
    if (l != nullptr) lv_obj_set_style_text_font(l, fonts.value, 0);
}

// The media player to show: playing first, then paused.
inline const custom_logic_entity_sub::EntityDetailState *now_playing() {
  const custom_logic_entity_sub::EntityDetailState *paused = nullptr;
  for (const auto &d : custom_logic_entity_sub::entity_details) {
    if (d.entity_id.rfind("media_player.", 0) != 0 || !d.available || d.media_title.empty()) continue;
    if (d.state == "playing") return &d;
    if (d.state == "paused" && paused == nullptr) paused = &d;
  }
  return paused;
}

// Called from the panel's tick while the idle clock shows; `force` relays out
// at once (screen entered), otherwise at most twice a second.
inline void update(lv_obj_t *page, int W, int H, const std::string &weather_state, const std::string &today_date,
                   bool force) {
  if (page == nullptr || base.time == nullptr) return;
  const uint32_t now = esphome::millis();
  if (!force && now - last_update < 500) return;
  last_update = now;
  create(page);
  const bool wide = W > H;
  const uint32_t fg = onx_display_settings::foreground(), muted = onx_display_settings::muted_text();
  const int m = S(16);

  // ---- data
  const Day *td = nullptr;
  std::vector<const Day *> ahead;
  for (int i = 0; i < day_count; ++i) {
    if (days[i].date.compare(0, 10, today_date) == 0) td = &days[i];
    else if (today_date.empty() || days[i].date.substr(0, 10) > today_date) ahead.push_back(&days[i]);
  }
  if (td == nullptr && today_date.empty() && !ahead.empty()) { td = ahead.front(); ahead.erase(ahead.begin()); }
  std::string today_text;
  if (td != nullptr) {
    today_text = "H " + degrees(td->high) + "  \xC2\xB7  L " + degrees(td->low);
    if (!td->rain.empty()) today_text += "  \xC2\xB7  Rain " + td->rain + "%";
  }
  set_text(today, today_text);
  set_text(condition, condition_text(weather_state));
  const auto *player = now_playing();

  // ---- status row
  const int icon_y = S(10);
  // Centered like the main screen's status icons (normal_mode_pages.yaml).
  for (auto *o : {base.wifi_on, base.wifi_off}) onx_panel_ui::place(o, LV_ALIGN_TOP_MID, -S(13), icon_y);
  for (auto *o : {base.ha_on, base.ha_off}) onx_panel_ui::place(o, LV_ALIGN_TOP_MID, S(13), icon_y);

  // ---- what to show (Device settings → Clock & idle screen)
  const uint8_t parts = onx_clock_sensors::parts();
  const bool show_weather = parts & onx_clock_sensors::WEATHER;
  const bool show_today = (parts & onx_clock_sensors::TODAY) && !today_text.empty();
  const int days_max = std::min<int>(ahead.size(), wide ? int(columns.size()) : (W >= S(300) ? 5 : 4));
  const bool show_forecast = (parts & onx_clock_sensors::FORECAST) && days_max > 0;
  const bool show_media = (parts & onx_clock_sensors::MEDIA) && player != nullptr;
  // The standby refresh toggles the weather widgets' hidden flags each second,
  // so turned-off weather is faded out rather than hidden.
  auto fade = [](lv_obj_t *o, bool on) { if (o) onx_render::style_opa(o, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0); };
  for (auto *o : base.weather_icons) fade(o, show_weather);
  for (auto *o : {base.temp_icon, base.temp, base.humidity_icon, base.humidity}) fade(o, show_weather);
  show(condition, show_weather);
  show(today, show_today);
  if (!show_weather) show(humidity_caption, false);

  // ---- now playing card
  const bool any_side = show_weather || show_today || show_forecast;
  const int split = any_side ? W * 58 / 100 : W;
  const int card_h = S(50), card_w = wide ? split - m - (any_side ? S(8) : m) : W - 2 * m;
  show(media, show_media);
  if (show_media) {
    size(media, card_w, card_h);
    at(media, m, H - m - card_h + S(4));
    onx_render::style_bg_color(media, lv_color_hex(onx_display_settings::surface()), 0);
    const int pad = S(12), inner = card_w - 2 * pad;
    width(media_title, inner);
    width(media_sub, inner);
    at(media_title, pad, S(5));
    std::string sub = player->media_artist;
    if (player->state == "paused") sub = sub.empty() ? "Paused" : sub + "  \xC2\xB7  Paused";
    set_text(media_title, player->media_title);
    set_text(media_sub, sub);
    color(media_title, fg);
    color(media_sub, muted);
    at(media_sub, pad, S(5) + line(fonts.body));
    auto progress = player->progress;  // view() updates its draw cache; keep the shared one intact.
    const auto pv = progress.view(now, double(std::time(nullptr)), true);
    show(media_bar, pv.visible);
    size(media_bar, inner, S(3));
    at(media_bar, pad, card_h - S(8));
    lv_bar_set_value(media_bar, pv.permille, LV_ANIM_OFF);
    onx_render::style_bg_color(media_bar, lv_color_hex(onx_display_settings::control_surface()), LV_PART_MAIN);
    onx_render::style_bg_color(media_bar, lv_color_hex(onx_display_settings::accent()), LV_PART_INDICATOR);
  }

  // ---- clock fonts: the larger set when it fits the space it is given
  const bool ampm = onx_display_settings::datetime.hour_format == 1 && onx_display_settings::datetime.show_ampm;
  const char *time_text = lv_label_get_text(base.time);
  auto clock_font = [&](bool big, int max_w) {
    if (ampm) return fonts.clock_ampm;
    if (big && fonts.clock_big && text_width(time_text, fonts.clock_big) <= max_w) return fonts.clock_big;
    return fonts.clock;
  };
  auto date_font = [&](const lv_font_t *tf) { return tf == fonts.clock_big ? fonts.date_big : fonts.date; };
  auto clock_h = [&](const lv_font_t *tf) { return line(tf) - line(tf) / 7 + line(date_font(tf)); };
  auto apply_clock = [&](const lv_font_t *tf) {
    onx_render::style_text_font(base.time, tf, 0);
    onx_render::style_text_font(base.date, date_font(tf), 0);
  };
  const int weather_icon = S(49);
  for (auto *l : {condition, humidity_caption, today}) color(l, muted);
  const int top0 = S(34);
  const int bottom = H - m - (show_media ? card_h + S(8) : 0);

  // Forecast cells: 2 = large icons, 1 = icons, 0 = high and low only.
  auto fc_h = [&](int mode) {
    return mode == 0 ? line(fonts.small) + line(fonts.body)
                     : line(fonts.small) + (mode == 2 ? S(42) : S(32)) + S(4) + line(fonts.body) + line(fonts.small);
  };
  auto draw_forecast_columns = [&](int x, int y, int w, int n, int mode) {
    const int cw = w / n, icon = mode == 2 ? S(40) : S(30);
    size(forecast, w, fc_h(mode));
    at(forecast, x, y);
    for (int i = 0; i < int(columns.size()); ++i) {
      auto &c = columns[i];
      const bool on = i < n;
      for (auto *o : {c.day, c.high}) { show(o, on); if (on) { width(o, cw); align_text(o, LV_TEXT_ALIGN_CENTER); } }
      show(c.low, on && mode > 0);
      show(c.icon, on && mode > 0);
      if (!on) continue;
      if (mode > 0) { width(c.low, cw); align_text(c.low, LV_TEXT_ALIGN_CENTER); }
      const Day &d = *ahead[i];
      set_text(c.day, weekday(d.date));
      set_text(c.high, mode > 0 ? degrees(d.high) : degrees(d.high) + " " + degrees(d.low));
      set_text(c.low, degrees(d.low));
      color(c.day, muted); color(c.high, fg); color(c.low, muted);
      int cy = 0;
      at(c.day, i * cw, cy); cy += line(fonts.small);
      if (mode > 0) {
        const void *src = condition_image ? condition_image(d.condition) : nullptr;
        if (lv_image_get_src(c.icon) != src) lv_image_set_src(c.icon, src);
        size(c.icon, icon, icon);
        at(c.icon, i * cw + (cw - icon) / 2, cy + S(2));
        cy += icon + S(4);
      }
      at(c.high, i * cw, cy); cy += line(fonts.body);
      at(c.low, i * cw, cy);
    }
  };

  if (!wide) {
    // ---- portrait: pick the largest sizes that fit, then share out the rest
    const int tx = m + weather_icon + S(10);
    auto row_fits = [&](const lv_font_t *f) {
      return tx + S(20) + text_width(lv_label_get_text(base.temp), f) + S(14) + S(20) +
             text_width(lv_label_get_text(base.humidity), f) <= W - m;
    };
    const lv_font_t *vf = row_fits(fonts.value) ? fonts.value : fonts.body;
    auto weather_h = [&](const lv_font_t *f) { return std::max(weather_icon, line(f) + line(fonts.small)); };
    bool big = true;
    int mode = 2;
    bool forecast_on = show_forecast;
    auto needed = [&]() {
      int h = clock_h(clock_font(big, W - 2 * m)), k = 0;
      if (show_weather) { h += weather_h(vf); ++k; }
      if (show_today) { h += line(fonts.small); ++k; }
      if (forecast_on) { h += fc_h(mode); ++k; }
      return h + k * S(8);
    };
    const int avail = bottom - top0;
    // Shrink in order: forecast icons, clock size, weather value size, forecast.
    while (needed() > avail) {
      if (forecast_on && mode > 1) --mode;
      else if (big) big = false;
      else if (vf == fonts.value) vf = fonts.body;
      else if (forecast_on && mode > 0) --mode;
      else if (forecast_on) forecast_on = false;
      else break;
    }
    const lv_font_t *tf = clock_font(big, W - 2 * m);
    apply_clock(tf);
    for (auto *l : {base.temp, base.humidity}) onx_render::style_text_font(l, vf, 0);
    // Free space becomes even gaps between the blocks (capped), the rest centres them.
    const int blocks = 1 + show_weather + show_today + forecast_on;
    const int content = needed() - (blocks - 1) * S(8);
    const int free = std::max(0, avail - content);
    const int gap = std::clamp(free / (blocks + 1), S(8), S(40));
    int y = top0 + std::max(0, (free - gap * (blocks - 1)) / 2);
    onx_panel_ui::place(base.time, LV_ALIGN_TOP_MID, 0, y);
    onx_panel_ui::place(base.date, LV_ALIGN_TOP_MID, 0, y + line(tf) - line(tf) / 7);
    y += clock_h(tf) + gap;
    if (show_weather) {
      const int wh = weather_h(vf);
      for (auto *o : base.weather_icons) at(o, m, y + (wh - weather_icon) / 2);
      const int vy = y + (wh - line(vf) - line(fonts.small)) / 2;
      at(base.temp_icon, tx, vy + (line(vf) - S(16)) / 2);
      at(base.temp, tx + S(20), vy);
      at(condition, tx, vy + line(vf));
      const int hw = text_width(lv_label_get_text(base.humidity), vf);
      onx_panel_ui::place(base.humidity, LV_ALIGN_TOP_RIGHT, -m, vy);
      onx_panel_ui::place(base.humidity_icon, LV_ALIGN_TOP_RIGHT, -m - hw - S(4), vy + (line(vf) - S(16)) / 2);
      onx_panel_ui::place(humidity_caption, LV_ALIGN_TOP_RIGHT, -m, vy + line(vf));
      show(humidity_caption, tx + text_width(lv_label_get_text(condition), fonts.small) + S(12) +
                                 text_width("Humidity", fonts.small) <= W - m);
      y += wh + gap;
    }
    if (show_today) {
      onx_panel_ui::place(today, LV_ALIGN_TOP_MID, 0, y);
      y += line(fonts.small) + gap;
    }
    show(forecast, forecast_on);
    if (forecast_on) draw_forecast_columns(m, y, W - 2 * m, days_max, mode);
    return;
  }

  // ---- landscape: clock (and media) on the left, a weather column on the right.
  // With no weather parts the clock takes the whole width.
  const int rx = split + S(8), rw = W - rx - m;
  const lv_font_t *tf = clock_font(true, split - 2 * m);
  apply_clock(tf);
  for (auto *l : {base.temp, base.humidity}) onx_render::style_text_font(l, fonts.value, 0);
  const int ch = clock_h(tf);
  const int top = std::max(top0, top0 + (bottom - top0 - ch) / 2);
  const int time_w = text_width(time_text, tf);
  const int cx = std::max(m, (split - time_w) / 2);
  if (any_side) {
    at(base.time, cx, top);
    at(base.date, cx + S(4), top + line(tf) - line(tf) / 7);
  } else {
    onx_panel_ui::place(base.time, LV_ALIGN_TOP_MID, 0, top);
    onx_panel_ui::place(base.date, LV_ALIGN_TOP_MID, 0, top + line(tf) - line(tf) / 7);
  }
  // Right column: measure, then spread the blocks over the height.
  const int rh = std::max(S(26), line(fonts.body) + S(4));
  const int weather_block = show_weather ? std::max(weather_icon, line(fonts.value) + line(fonts.small)) + S(6) + line(fonts.value) : 0;
  const int today_block = show_today ? line(fonts.small) : 0;
  int fixed = weather_block + today_block;
  const int rows = show_forecast ? std::min(days_max, std::max(0, (H - m - top0 - fixed - S(16)) / rh)) : 0;
  const int blocks = (show_weather ? 1 : 0) + (show_today ? 1 : 0) + (rows > 0 ? 1 : 0);
  const int content = fixed + rows * rh;
  const int gap = blocks > 1 ? std::clamp((H - m - top0 - content) / (blocks + 1), S(8), S(28)) : 0;
  int y = top0 + std::max(0, (H - m - top0 - content - gap * (blocks - 1)) / 2);
  if (show_weather) {
    for (auto *o : base.weather_icons) at(o, rx, y);
    const int tx = rx + weather_icon + S(8);
    at(base.temp_icon, tx, y + (line(fonts.value) - S(16)) / 2);
    at(base.temp, tx + S(20), y);
    at(condition, tx, y + line(fonts.value));
    y += std::max(weather_icon, line(fonts.value) + line(fonts.small)) + S(6);
    at(base.humidity_icon, rx, y + (line(fonts.value) - S(16)) / 2);
    at(base.humidity, rx + S(20), y);
    const int hw = text_width(lv_label_get_text(base.humidity), fonts.value);
    at(humidity_caption, rx + S(26) + hw, y + line(fonts.value) - line(fonts.small) - S(2));
    show(humidity_caption, rx + S(26) + hw + text_width("Humidity", fonts.small) <= W - m);
    y += line(fonts.value) + gap;
  }
  if (show_today) {
    at(today, rx, y);
    width(today, rw);
    if (lv_label_get_long_mode(today) != LV_LABEL_LONG_DOT) lv_label_set_long_mode(today, LV_LABEL_LONG_DOT);
    y += line(fonts.small) + gap;
  }
  show(forecast, rows > 0);
  if (rows > 0) {
    size(forecast, rw, rows * rh);
    at(forecast, rx, y);
  }
  for (int i = 0; i < int(columns.size()); ++i) {
    auto &c = columns[i];
    const bool on = i < rows;
    for (auto *o : {c.day, c.icon, c.high, c.low}) show(o, on);
    if (!on) continue;
    const Day &d = *ahead[i];
    for (auto *o : {c.day, c.high, c.low}) { width(o, LV_SIZE_CONTENT); align_text(o, LV_TEXT_ALIGN_LEFT); }
    set_text(c.day, weekday(d.date));
    set_text(c.high, degrees(d.high));
    set_text(c.low, degrees(d.low));
    color(c.day, muted); color(c.high, fg); color(c.low, muted);
    const int ry = i * rh;
    at(c.day, 0, ry + (rh - line(fonts.small)) / 2);
    const void *src = condition_image ? condition_image(d.condition) : nullptr;
    if (lv_image_get_src(c.icon) != src) lv_image_set_src(c.icon, src);
    size(c.icon, S(22), S(22));
    at(c.icon, S(44), ry + (rh - S(22)) / 2);
    const int lw = text_width(lv_label_get_text(c.low), fonts.small);
    onx_panel_ui::place(c.low, LV_ALIGN_TOP_RIGHT, 0, ry + (rh - line(fonts.small)) / 2);
    onx_panel_ui::place(c.high, LV_ALIGN_TOP_RIGHT, -lw - S(10), ry + (rh - line(fonts.body)) / 2);
  }
}

// ------------------------------------------------------------ photo overlay
// The slideshow and ImmichFrame screens keep the idle clock's information in a
// band at the bottom: time and date on the left, current weather on the right
// and the playing media above them. The parts follow the clock settings.
// Styles (select "Clock on photos"): a large clock (the band above), a small
// clock (smaller time and date, a thinner band) or the photo with only a
// small time in a corner pill.
enum PhotoOverlay { OVERLAY_LARGE = 0, OVERLAY_SMALL = 1, OVERLAY_PHOTO_ONLY = 2 };
inline int photo_overlay = OVERLAY_LARGE;
inline void update_photo_overlay(int W, int H, const std::string &weather_state) {
  using namespace onx_panel_ui;
  if (slide_bar == nullptr || base.time == nullptr) return;
  const bool small = photo_overlay == OVERLAY_SMALL, only = photo_overlay == OVERLAY_PHOTO_ONLY;
  const int m = small || only ? S(10) : S(12);
  const bool ampm = onx_display_settings::datetime.hour_format == 1 && onx_display_settings::datetime.show_ampm;
  const lv_font_t *tf = only ? fonts.value
                        : small ? fonts.clock_ampm
                        : ampm || fonts.clock == nullptr ? fonts.clock_ampm : fonts.clock;
  if (tf != nullptr) onx_render::style_text_font(slide_time, tf, 0);
  // Photos always carry white text over the dark fade, whatever the panel
  // theme. Set directly (not via the themed helpers) so a light background
  // can never turn the overlay dark; only the notification badge follows it.
  auto white = [](lv_obj_t *l, uint32_t c) {
    lv_style_value_t v;
    if (lv_obj_get_local_style_prop(l, LV_STYLE_TEXT_COLOR, &v, 0) == LV_STYLE_RES_FOUND &&
        (lv_color_to_u32(v.color) & 0xFFFFFF) == c) return;
    lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
  };
  white(slide_time, 0xFFFFFF); white(slide_temp, 0xFFFFFF);
  white(slide_date, 0xE0E0E0); white(slide_media, 0xE0E0E0);
  // The fade band and the letterbox stay black on every theme.
  auto black = [](lv_obj_t *o, lv_style_prop_t prop) {
    lv_style_value_t v;
    if (lv_obj_get_local_style_prop(o, prop, &v, 0) == LV_STYLE_RES_FOUND && (lv_color_to_u32(v.color) & 0xFFFFFF) == 0) return;
    v.color = lv_color_hex(0x000000);
    lv_obj_set_local_style_prop(o, prop, v, 0);
  };
  black(slide_bar, LV_STYLE_BG_COLOR); black(slide_bar, LV_STYLE_BG_GRAD_COLOR); black(slide_layer, LV_STYLE_BG_COLOR);
  // Photo only: the time alone in a translucent pill, no fade band.
  show(slide_date, !only);
  if (only) {
    show(slide_media, false);
    show(slide_temp, false);
    show(slide_weather, false);
    const int ph = S(8), pv = S(4), tw = text_width(lv_label_get_text(slide_time), tf), th = line(tf);
    lv_obj_set_style_bg_main_opa(slide_bar, LV_OPA_50, 0);
    lv_obj_set_style_bg_grad_opa(slide_bar, LV_OPA_50, 0);
    lv_obj_set_style_radius(slide_bar, S(10), 0);
    size(slide_bar, tw + 2 * ph, th + 2 * pv);
    at(slide_bar, m, H - m - th - 2 * pv);
    at(slide_time, ph, pv);
    return;
  }
  lv_obj_set_style_bg_main_opa(slide_bar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_grad_opa(slide_bar, LV_OPA_70, 0);
  lv_obj_set_style_radius(slide_bar, 0, 0);
  const lv_font_t *df = small ? fonts.small : fonts.date;
  onx_render::style_text_font(slide_date, df, 0);
  set_text(slide_date, lv_label_get_text(base.date));
  if (lv_label_get_long_mode(slide_date) != LV_LABEL_LONG_DOT) lv_label_set_long_mode(slide_date, LV_LABEL_LONG_DOT);

  const uint8_t parts = onx_clock_sensors::parts();
  const std::string temp = base.temp ? lv_label_get_text(base.temp) : "";
  const bool weather = (parts & onx_clock_sensors::WEATHER) && !weather_state.empty() &&
                       weather_state != "unknown" && weather_state != "unavailable" && !temp.empty();
  const auto *player = (parts & onx_clock_sensors::MEDIA) ? now_playing() : nullptr;
  std::string media;
  if (player != nullptr) {
    media = player->media_title;
    if (!player->media_artist.empty()) media += "  \xC2\xB7  " + player->media_artist;
    if (player->state == "paused") media += "  \xC2\xB7  Paused";
  }

  // The clock line is the time font's height minus its empty descent, as on the idle clock.
  const int time_h = line(tf) - line(tf) / 7, date_h = std::max(line(df), small ? S(20) : S(28)), media_h = line(fonts.small);
  const int body = time_h + date_h + (media.empty() ? 0 : media_h + S(6));
  const int bar_h = std::min(H / 2, body + 2 * m + (small ? S(18) : S(36)));  // headroom for the fade
  size(slide_bar, W, bar_h);
  at(slide_bar, 0, H - bar_h);
  const int date_y = bar_h - m - date_h, time_y = date_y - time_h;
  at(slide_time, m, time_y - line(tf) / 14);
  at(slide_date, m + S(2), date_y + (date_h - line(df)) / 2);

  show(slide_media, !media.empty());
  if (!media.empty()) {
    set_text(slide_media, media);
    width(slide_media, W - 2 * m);
    at(slide_media, m, time_y - S(6) - media_h);
  }

  // Weather sits right-aligned on the date line, under the clock, so the large
  // digits never run into it.
  show(slide_temp, weather);
  show(slide_weather, weather);
  if (weather) {
    set_text(slide_temp, temp);
    onx_render::style_text_font(slide_temp, df, 0);
    const int icon = small ? S(20) : S(28), tw = text_width(temp.c_str(), df);
    const int cy = date_y + date_h / 2;
    at(slide_temp, W - m - tw, cy - line(df) / 2);
    const void *src = condition_image ? condition_image(weather_state) : nullptr;
    if (lv_image_get_src(slide_weather) != src) lv_image_set_src(slide_weather, src);
    size(slide_weather, icon, icon);
    at(slide_weather, W - m - tw - S(4) - icon, cy - icon / 2);
    width(slide_date, std::max(S(40), W - 3 * m - tw - icon));
  } else {
    width(slide_date, W - 2 * m);
  }
}

}  // namespace onx_idle_clock
