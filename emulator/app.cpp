#include <set>
// Room Companion on esphome-lvgl-emulator (https://github.com/fixtse/esphome-lvgl-emulator):
// renders the shared panel code, the same headers the firmware compiles, for
// the 2.8" (ONX2432G028) and 3.5" (ONX3248G035) panels. See README.md.
//
//   rc-emulator --model 28|35 [--scenario emulator/scenarios/demo.json] [--root .] [--out DIR] [--preview DIR] [--sdl]
// --preview writes the web preview data for each screen (see tools/preview-compare.cjs).
#include "lvgl_emu/runtime.h"
#include "esphome.h"
using namespace esphome;
#include "emu_globals.h"
#include "onx_panel_pages.h"
#include "onx_navigation.h"
#include "onx_notification_ui.h"
#include "onx_icon_state.h"
#include "onx_preview.h"

#include <ArduinoJson.h>
#include <cstdio>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>

namespace rc {
std::string root = ".";  // Repository root: fonts, assets and board profiles.

std::string read_file(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream s;
  s << in.rdbuf();
  return s.str();
}

// ------------------------------------------------------------ board profile
// Read from the same YAML the firmware builds from; the root YAML overrides
// the board profile, as in ESPHome's package merge.
struct Board {
  std::string model;
  int width{240}, height{320};
  float ui_scale{1}, font_scale{1};
  int strip_center_y{0}, strip_spacing{48}, strip_icon{20}, strip_marker{32}, strip_label_y{-1};
  std::string strip_label_font, symbol_font{"montserrat_28"};
};
Board load_board(const std::string &model) {
  const std::string name = model == "28" ? "ONX2432G028" : "ONX3248G035";
  std::string lower = name;
  for (auto &c : lower) c = std::tolower(c);
  // ESPHome's merge order: the board profile, then the shared packages' defaults
  // (which override it), then the root YAML. A board value that a shared
  // package also defines is lost, as on the device.
  auto s = lvgl_emu::substitutions(root + "/packages/room_companion/boards/" + lower + ".yaml");
  s = lvgl_emu::substitutions(root + "/packages/base/fonts.yaml", s);
  s = lvgl_emu::substitutions(root + "/packages/features/normal_mode/normal_mode_pages.yaml", s);
  s = lvgl_emu::substitutions(root + "/" + name + ".yaml", s);
  Board b;
  b.model = name;
  auto num = [&](const char *k, float d) { return s.count(k) ? std::stof(s[k]) : d; };
  b.width = num("display_width", 240); b.height = num("display_height", 320);
  b.ui_scale = num("ui_scale", 1); b.font_scale = num("font_scale", 1);
  b.strip_center_y = num("strip_center_y", b.height - 22); b.strip_spacing = num("strip_spacing", 48);
  b.strip_icon = num("strip_icon", 20); b.strip_marker = num("strip_marker", 32); b.strip_label_y = num("strip_label_y", -1);
  b.strip_label_font = s["strip_label_font"];
  if (s.count("symbol_font")) b.symbol_font = s["symbol_font"];
  return b;
}
Board board;
int ui(int v) { return std::lround(v * board.ui_scale); }
// LVGL's built-in symbol font (substitution symbol_font), as &lv_font_${symbol_font}.
const lv_font_t *symbol_font() {
  if (board.symbol_font == "montserrat_20") return &lv_font_montserrat_20;
  if (board.symbol_font != "montserrat_28") std::fprintf(stderr, "symbol font %s not built in; using montserrat_28\n", board.symbol_font.c_str());
  return &lv_font_montserrat_28;
}

// fonts_MiSans_<Weight>_<size>: the firmware's ESPHome font ids at the
// board's font_scale (packages/base/fonts.yaml).
const lv_font_t *font(const std::string &id) {
  std::smatch m;
  static const std::regex pattern(R"(fonts_MiSans_(Medium|Bold)_(\d+))");
  if (!std::regex_match(id, m, pattern)) { std::fprintf(stderr, "unknown font %s\n", id.c_str()); return &lv_font_montserrat_14; }
  return lvgl_emu::ttf(root + "/fonts/MiSans-" + std::string(m[1]) + ".ttf", std::lround(std::stoi(m[2]) * board.font_scale));
}
// The entity fallback icon (assets/entity_icons/<name>.svg), 80 px at ui_scale.
void bind_entity_icon(lv_obj_t *widget, const std::string &entity, bool active) {
  const void *src = lvgl_emu::svg(root + "/assets/entity_icons/" + onx_icon_state::fallback(entity, "") + ".svg", ui(80));
  if (src == nullptr) src = lvgl_emu::svg(root + "/assets/entity_icons/toggle-switch.svg", ui(80));
  lv_image_set_src(widget, src);
  lv_obj_set_style_image_recolor(widget, lv_color_hex(active ? 0x62B0FF : 0xFFFFFF), 0);
  lv_obj_set_style_image_recolor_opa(widget, LV_OPA_COVER, 0);
}

// ------------------------------------------------------------ scenario
// Fills an entity's detail state the way the firmware's subscriptions would.
void set_field(custom_logic_entity_sub::EntityDetailState &d, const std::string &k, JsonVariantConst v) {
  // Lists arrive from Home Assistant as Python reprs: ['off', 'heat'].
  std::string list;
  if (v.is<JsonArrayConst>()) {
    for (JsonVariantConst item : v.as<JsonArrayConst>()) list += (list.empty() ? "'" : ", '") + item.as<std::string>() + "'";
    list = "[" + list + "]";
  }
  const std::string s = v.is<JsonArrayConst>() ? list : v.is<const char *>() ? v.as<std::string>() : v.is<bool>() ? (v.as<bool>() ? "true" : "false")
                                                                                   : onx_detail_view::compact(std::to_string(v.as<double>()));
  std::map<std::string, std::string *> strings = {
      {"state", &d.state}, {"unit", &d.unit}, {"current", &d.current}, {"target", &d.target}, {"low", &d.low},
      {"high", &d.high}, {"modes", &d.modes}, {"humidity", &d.humidity}, {"volume", &d.volume},
      {"media_title", &d.media_title}, {"media_artist", &d.media_artist}, {"picture", &d.picture},
      {"brightness", &d.brightness}, {"position", &d.position}, {"apparent_temperature", &d.apparent_temperature},
      {"wind_speed", &d.wind_speed}, {"wind_unit", &d.wind_unit}, {"pressure", &d.pressure},
      {"pressure_unit", &d.pressure_unit}, {"options", &d.options}, {"presets", &d.presets}, {"preset", &d.preset},
      {"fan_modes", &d.fan_modes}, {"fan_mode", &d.fan_mode}, {"percentage", &d.percentage},
      {"oscillating", &d.oscillating}, {"direction", &d.direction}, {"sources", &d.sources}, {"source", &d.source},
      {"muted", &d.muted}, {"event_type", &d.event_type}, {"uv_index", &d.uv_index},
      {"swing_modes", &d.swing_modes}, {"swing_mode", &d.swing_mode}, {"visibility", &d.visibility},
      {"visibility_unit", &d.visibility_unit}, {"wind_bearing", &d.wind_bearing}, {"timer_duration", &d.timer_duration},
      {"timer_remaining", &d.timer_remaining}, {"timer_finishes_at", &d.timer_finishes_at}, {"tones", &d.tones}};
  if (auto it = strings.find(k); it != strings.end()) { *it->second = s; return; }
  if (k == "features") d.features = v.as<uint32_t>();
  else if (k == "minimum") d.minimum = v.as<float>();
  else if (k == "maximum") d.maximum = v.as<float>();
  else if (k == "step") d.step = v.as<float>();
  else if (k == "hue") d.light_hue = v.as<float>();
  else if (k == "saturation") d.light_saturation = v.as<float>();
  else if (k == "color_temp") d.light_temperature = v.as<bool>();
  else if (k == "timer_finishes_in") {
    // Relative fixture deadlines remain running on later validation dates.
    const time_t finish = std::time(nullptr) + v.as<int>();
    const auto utc = *std::gmtime(&finish);
    char stamp[32]; std::strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S+00:00", &utc);
    d.timer_finishes_at = stamp;
  }
  else if (k == "voice_timers") {
    d.voice_timer = true;
    d.timer_received_ms = lvgl_emu::now_ms();
    d.voice_timers.clear();
    for (JsonArrayConst timer : v.as<JsonArrayConst>())
      d.voice_timers.push_back({timer[0].as<std::string>(), timer[1].as<uint32_t>(), timer[2].as<uint32_t>(), timer[3].as<int>() != 0});
    if (!d.voice_timers.empty()) {
      const auto &timer = d.voice_timers.front();
      d.timer_name = timer.name; d.voice_duration = timer.duration; d.voice_remaining = timer.remaining;
    }
  }
  else if (k == "forecast") {
    int n = 0;
    for (JsonObjectConst day : v.as<JsonArrayConst>()) {
      if (n >= int(d.forecast.size())) break;
      auto &f = d.forecast[n++];
      f.date = day["date"] | ""; f.condition = day["condition"] | "";
      f.high = day["high"].is<const char *>() ? day["high"].as<std::string>() : onx_detail_view::compact(std::to_string(day["high"] | 0.0));
      f.low = day["low"].is<const char *>() ? day["low"].as<std::string>() : onx_detail_view::compact(std::to_string(day["low"] | 0.0));
      f.rain = day["rain"].is<const char *>() ? day["rain"].as<std::string>() : onx_detail_view::compact(std::to_string(day["rain"] | 0.0));
    }
    d.forecast_count = n;
  } else std::fprintf(stderr, "scenario: unknown entity field %s\n", k.c_str());
}

// A generated 96x96 RGB565 cover: warm and cool blobs on a dark field.
std::set<std::string> demo_artwork;
const lv_image_dsc_t &demo_cover() {
  static std::vector<uint16_t> px(96 * 96);
  static lv_image_dsc_t d{};
  if (d.data) return d;
  auto blob = [](int x, int y, int cx, int cy, int r) { const int dx = x - cx, dy = y - cy; return std::max(0, 255 - (dx * dx + dy * dy) * 255 / (r * r)); };
  for (int y = 0; y < 96; ++y)
    for (int x = 0; x < 96; ++x) {
      // Hard-edged shapes on a pale sky, like a photo cover, to show the blur.
      (void) blob;
      int r = 170, g = 200, bl = 230;                                  // sky
      if (y > 60) { r = 40; g = 90; bl = 150; }                        // sea
      if (std::abs(x - 30) < 12 && y > 20 && y < 80) { r = 120; g = 60; bl = 30; }  // figure
      if ((x - 70) * (x - 70) + (y - 30) * (y - 30) < 160) { r = 250; g = 240; bl = 200; }  // sun
      if (y > 78 && (x / 6) % 2) { r = 230; g = 220; bl = 180; }      // sand stripes
      px[y * 96 + x] = uint16_t((r >> 3) << 11 | (g >> 2) << 5 | (bl >> 3));
    }
  d.header.magic = LV_IMAGE_HEADER_MAGIC; d.header.cf = LV_COLOR_FORMAT_RGB565;
  d.header.w = d.header.h = 96; d.header.stride = 192;
  d.data_size = px.size() * 2; d.data = reinterpret_cast<const uint8_t *>(px.data());
  return d;
}

JsonDocument scenario_doc;  // Kept for the web preview data.
// Light state the firmware keeps in panel storage (by entity), from a scenario's
// "light": {"on", "brightness", "kelvin", "min_kelvin", "max_kelvin", "color", "hue"}.
std::map<std::string, esphome::onx_storage::NormalLightState> lights;
esphome::onx_storage::NormalLightState light_of(const std::string &entity) {
  auto it = lights.find(entity);
  return it == lights.end() ? esphome::onx_storage::NormalLightState{} : it->second;
}
// status: the main page with the timer/music status row ("timer" text, "media");
// centre: the notification centre opened over the main page.
struct Shot { std::string name, entity; bool detail{false}; std::string timer; bool media{false}, centre{false}; };
std::vector<Shot> load_scenario(const std::string &path) {
  JsonDocument &doc = scenario_doc;
  if (deserializeJson(doc, read_file(path))) { std::fprintf(stderr, "cannot parse %s\n", path.c_str()); std::exit(2); }
  // As the firmware's storage component: entities are configured, not loaded from NVS.
  custom_logic::normal_ui_storage = new esphome::onx_storage::OnxStorage();
  for (size_t i = 0; i < custom_logic::CONFIGURABLE_DEVICE_SLOT_COUNT; ++i) custom_logic::device_config_loaded(i) = true;
  custom_logic::load_ha_entities_if_needed();
  auto &registry = custom_logic::cached_ha_entities();
  registry.count = 0;
  int slot = 0;
  std::vector<Shot> shots;
  for (JsonObjectConst e : doc["entities"].as<JsonArrayConst>()) {
    const std::string id = e["entity_id"] | "";
    if (id.empty() || slot >= int(onx_memory::ENTRY_COUNT)) continue;
    custom_logic::cached_device_entity_id(slot) = id;
    custom_logic::note_slot_used(slot);
    auto &d = custom_logic_entity_sub::entity_details[slot];
    d = {};
    d.entity_id = id;
    d.received = true;  // "unavailable": the entity reports unavailable; omit an entity to model "waiting".
    d.available = !(e["unavailable"] | false);
    d.received_ms = lvgl_emu::now_ms();
    if (id.rfind("cover.",0)==0) {
      auto config = onx_cover_presets::defaults();
      if (e["cover_presets"].is<JsonArrayConst>()) {
        std::string raw;
        for (int v : e["cover_presets"].as<JsonArrayConst>()) { if (!raw.empty()) raw += ','; raw += std::to_string(v); }
        if (!onx_cover_presets::parse(raw,config)) { std::fprintf(stderr,"invalid cover preset scenario\n"); std::exit(2); }
      }
      config.enabled = e["cover_presets_enabled"] | true;
      onx_entity_config::cover_preset_cache.push_back({id,config});
    }
    for (JsonPairConst kv : e) {
      const std::string k = kv.key().c_str();
      if (k == "light") {
        JsonObjectConst l = kv.value();
        esphome::onx_storage::NormalLightState st;
        st.is_on = l["on"] | false;
        st.brightness_pct = l["brightness"] | 0;
        st.color_temperature_kelvin = l["kelvin"] | 0;
        st.min_color_temperature_kelvin = l["min_kelvin"] | 2000;
        st.max_color_temperature_kelvin = l["max_kelvin"] | 6500;
        st.rgb_tick = uint8_t((int(l["hue"] | 0) / esphome::onx_storage::RGB_TICK_DEGREES) % esphome::onx_storage::RGB_TICKS);
        st.capability_mask = 1 | (l["brightness"].is<int>() ? 2 : 0) | (l["kelvin"].is<int>() ? 4 : 0) | ((l["color"] | false) ? 8 : 0);
        lights[id] = st;
      } else if (k == "media_position" || k == "media_duration" || k == "lyrics" || k == "artwork") {
        // Handled below, once the state is known.
      } else if (k != "entity_id" && k != "name" && k != "unavailable" && k != "detail" && k != "cover_presets" && k != "cover_presets_enabled") set_field(d, k, kv.value());
    }
    // Media position (seconds) and synced lyrics (LRC text, as the lyrics blueprint sends).
    if (e["media_duration"].is<float>()) d.progress.set_duration(e["media_duration"].as<float>());
    if (e["media_position"].is<float>()) {
      d.progress.set_state(d.state, lvgl_emu::now_ms());
      d.progress.set_position(e["media_position"].as<float>(), lvgl_emu::now_ms());
    }
    if (e["artwork"] == "demo") demo_artwork.insert(id);
    if (e["lyrics"].is<const char *>()) onx_lyrics::set(id, d.media_title, e["lyrics"].as<std::string>());
    if (registry.count < custom_logic::MAX_HA_ENTITY_COUNT) {
      auto &r = registry.entities[registry.count++];
      std::snprintf(r.entity_id, sizeof(r.entity_id), "%s", id.c_str());
      std::snprintf(r.friendly_name, sizeof(r.friendly_name), "%s", (e["name"] | id.c_str()));
    }
    if (e["detail"] | true) shots.push_back({"detail-" + id, id, true});
    ++slot;
  }
  // Pages: the firmware's StoredPages (layouts by entity position).
  auto &stored = onx_panel_pages::stored;
  new (&stored) onx_panel_pages::StoredPages();
  auto entry_of = [&](const std::string &id) -> uint8_t {
    for (int i = 0; i < slot; ++i) if (custom_logic::cached_device_entity_id(i) == id) return i;
    return onx_panel_pages::NO_ENTRY;
  };
  for (JsonObjectConst p : doc["pages"].as<JsonArrayConst>()) {
    if (stored.count >= onx_panel_pages::MAX_PAGES) break;
    auto &page = stored.pages[stored.count];
    page = {};
    const std::string kind = p["kind"] | "single";
    page.kind = uint8_t(kind == "two" ? onx_panel_layout::Kind::TWO_CONTROLS : kind == "sensors" ? onx_panel_layout::Kind::CONTROL_SENSORS
                        : kind == "grid" ? onx_panel_layout::Kind::ACTION_GRID : onx_panel_layout::Kind::SINGLE);
    page.rows = p["rows"] | 2; page.cols = p["cols"] | 2;
    int i = 0;
    for (JsonVariantConst id : p["entities"].as<JsonArrayConst>()) if (i < onx_panel_layout::MAX_CELLS) page.entries[i++] = entry_of(id.as<std::string>());
    std::snprintf(page.name, sizeof(page.name), "%s", (p["name"] | ""));
    ++stored.count;
  }
  onx_panel_pages::loaded = true;
  onx_panel_pages::dirty = true;
  onx_panel_pages::refresh();
  custom_logic::ha_entities_loaded = true;
  // Every page after the firmware's reconcile (entities without a page get their own).
  std::vector<Shot> pages;
  for (int i = 0; i < stored.count; ++i) {
    const auto lead = onx_panel_pages::entry_entity(onx_panel_pages::lead(stored.pages[i]));
    const std::string name = stored.pages[i].name[0] ? std::string(stored.pages[i].name) : lead;
    char prefix[16];
    std::snprintf(prefix, sizeof(prefix), "page%02d-", i + 1);
    pages.push_back({prefix + name, lead, false});
  }
  pages.insert(pages.end(), shots.begin(), shots.end());
  // "screens": extra main-page shots, e.g. {"name":"status","timer":"4:32","media":true} or {"name":"centre","centre":true}.
  for (JsonObjectConst x : doc["screens"].as<JsonArrayConst>()) {
    Shot shot{x["name"] | "screen", x["entity"] | (pages.empty() ? "" : pages.front().entity.c_str()), false};
    shot.timer = x["timer"] | ""; shot.media = x["media"] | false; shot.centre = x["centre"] | false;
    pages.push_back(shot);
  }
  return pages;

}

// ------------------------------------------------------------ web preview data
// The firmware's preview API response for this screen (onx_preview.h), plus the
// page layout, so tools/preview-compare can draw panel_web.h's preview of it.
std::string preview_dir, model_name;
int shot_index = 0;
std::string safe_name(std::string s) {
  for (auto &ch : s) if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '-' && ch != '_') ch = '_';
  return s;
}
int entity_count();
void write_preview(const Shot &shot) {
  ++shot_index;
  if (preview_dir.empty()) return;
  JsonDocument out;
  out["screen"] = shot.detail ? "detail" : "main";
  out["entity"] = shot.entity;
  out["width"] = board.width; out["height"] = board.height;
  out["entities"] = scenario_doc["entities"];
  JsonArray pages = out["pages"].to<JsonArray>();
  const auto &stored = onx_panel_pages::stored;
  for (int i = 0; i < stored.count; ++i) {
    const auto &p = stored.pages[i];
    auto page = pages.add<JsonObject>();
    const auto kind = onx_panel_layout::Kind(p.kind);
    page["name"] = p.name; page["icon"] = p.icon;
    page["layout"] = kind == onx_panel_layout::Kind::TWO_CONTROLS ? "two_controls" : kind == onx_panel_layout::Kind::CONTROL_SENSORS ? "control_sensors"
                     : kind == onx_panel_layout::Kind::ACTION_GRID ? "action_grid" : "single";
    if (kind == onx_panel_layout::Kind::ACTION_GRID) { page["rows"] = p.rows; page["columns"] = p.cols; }
    auto entities = page["entities"].to<JsonArray>();
    for (int e = 0; e < onx_panel_pages::size_of(p); ++e) entities.add(onx_panel_pages::entry_entity(p.entries[e]));
  }
  onx_preview::Context c;
  c.now_epoch = uint32_t(std::time(nullptr)); c.clock_valid = true;
  c.panel_width = board.width; c.panel_height = board.height;
  c.device_count = entity_count(); c.api_connected = true;
  c.light = [](int ui) { return light_of(custom_logic::cached_device_entity_id(ui)); };
  c.light_mode = [](int) { return 0; };
  c.entity_at = [](int ui) { return custom_logic::cached_device_entity_id(ui); };
  c.type_at = [](int) { return std::string(); };
  onx_preview::build(out["data"].to<JsonObject>(), shot.entity, c);
  char index[8];
  std::snprintf(index, sizeof(index), "%02d", shot_index);
  std::ofstream file(preview_dir + "/" + model_name + "-" + index + "-" + safe_name(shot.name) + ".json");
  serializeJson(out, file);
}

// ------------------------------------------------------------ screens
int entity_count() {
  int n = 0;
  while (n < int(onx_memory::ENTRY_COUNT) && !custom_logic::cached_device_entity_id(n).empty()) ++n;
  return n;
}
void show(const Shot &shot) {
  const int w = board.width, h = board.height;
  auto bind = [](lv_obj_t *icon, const std::string &entity) { bind_entity_icon(icon, entity, false); };
  onx_panel_detail::timers.focus.clear();
  onx_notification_ui::set_timer_chip(shot.timer);
  onx_notification_ui::set_media_chip(shot.media);
  if (onx_notification_ui::panel_open && !shot.centre) onx_notification_ui::close_panel();
  if (shot.detail) {
    // A detail shot represents tapping that timer's row in the multi-timer list.
    if (custom_logic_entity_sub::is_timer_detail(shot.entity))
      for (const auto &timer : onx_panel_detail::all_timers())
        if (timer.entity == shot.entity) { onx_panel_detail::timers.focus = timer.key; break; }
    onx_navigation::hide();
    onx_notification_ui::set_bell_visible(false);
    onx_panel_pages::render(shot.entity, w, h, true, bind);
  } else {
    onx_panel_pages::render(shot.entity, w, h, false, bind);
    // As show_normal_device_entry_page: the dock lists the carousel and Settings.
    const int total = entity_count();
    onx_navigation::render(onx_entity_config::carousel_entries(total), onx_panel_pages::configured(shot.entity),
                           [total](lv_obj_t *icon, int index, bool active) {
      if (index == total) lv_image_set_src(icon, lvgl_emu::svg(root + "/assets/panel/cog-outline.svg", ui(80)));
      else bind_entity_icon(icon, custom_logic::cached_device_entity_id(index), active);
      lv_obj_set_style_image_recolor(icon, lv_color_hex(active ? onx_display_settings::accent() : onx_display_settings::foreground()), 0);
      lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, 0);
      lv_image_set_scale(icon, onx_navigation::icon_size() * 256 / ui(80));
    });
    onx_notification_ui::set_bell_visible(true);
  }
  if (shot.centre && !onx_notification_ui::panel_open) onx_notification_ui::open_panel();
  onx_notification_ui::refresh();
  write_preview(shot);
}

// As panel.yaml's panel_ui_setup, notifications.yaml and the board profile.
void setup() {
  const int w = board.width, h = board.height;
  onx_panel_ui::native_width = onx_panel_ui::width = w;
  onx_panel_ui::native_height = onx_panel_ui::height = h;
  onx_panel_ui::time_font = font("fonts_MiSans_Medium_12");
  onx_panel_pages::setup(font("fonts_MiSans_Medium_14"), font("fonts_MiSans_Medium_24"), font("fonts_MiSans_Medium_32"));
  onx_panel_detail::fonts = {font("fonts_MiSans_Medium_12"), font("fonts_MiSans_Medium_15"), font("fonts_MiSans_Bold_18"),
                             font("fonts_MiSans_Medium_32"), symbol_font(), font("fonts_MiSans_Medium_64"),
                             font("fonts_MiSans_Medium_12"), font("fonts_MiSans_Bold_14")};
  onx_panel_detail::light_state = light_of;
  // "artwork": "demo" gives a player a generated cover (no downloads here).
  onx_panel_detail::artwork_source = [](const std::string &entity) -> const void * {
    return demo_artwork.count(entity) ? &demo_cover() : nullptr;
  };
  // As panel.yaml: the configured entities that are timers.
  onx_panel_detail::timer_entities = []() {
    std::vector<std::string> out;
    for (int i = 0; i < entity_count(); ++i)
      if (custom_logic_entity_sub::is_timer_detail(custom_logic::cached_device_entity_id(i))) out.push_back(custom_logic::cached_device_entity_id(i));
    return out;
  };
  onx_panel_detail::light_wheel = lvgl_emu::image(root + "/assets/panel/color-wheel-openhapanel.png", ui(220));
  onx_panel_detail::light_mode_icons = {lvgl_emu::image(root + "/assets/light_mode_icon_bright.svg", ui(20)),
                                        lvgl_emu::image(root + "/assets/light_mode_icon_cct.svg", ui(20)),
                                        lvgl_emu::image(root + "/assets/light_mode_icon_color.svg", ui(20))};
  onx_panel_detail::settings_icon = lvgl_emu::svg(root + "/assets/panel/cog-outline.svg", ui(72));
  onx_navigation::configure_strip(w, h, board.strip_center_y, board.strip_spacing, board.strip_marker, board.strip_icon,
                                  board.strip_label_y, board.strip_label_font.empty() ? nullptr : font(board.strip_label_font));
  onx_notification_ui::Fonts nf;
  nf.title = font("fonts_MiSans_Bold_16"); nf.body = font("fonts_MiSans_Medium_14");
  nf.small = font("fonts_MiSans_Medium_12"); nf.icon = font("fonts_MiSans_Medium_20"); nf.symbol = symbol_font();
  onx_notification_ui::bell_image = lvgl_emu::svg(root + "/assets/panel/bell-outline.svg", ui(20));
  // As notifications.yaml: quick-tile and status-row images.
  onx_notification_ui::clock_tile_image = lvgl_emu::svg(root + "/assets/panel/screen-sleep.svg", ui(24));
  onx_notification_ui::preset_tile_image = lvgl_emu::svg(root + "/assets/panel/preset-layers.svg", ui(24));
  onx_notification_ui::rotate_tile_image = lvgl_emu::svg(root + "/assets/panel/screen-rotation.svg", ui(22));
  onx_notification_ui::music_image = lvgl_emu::svg(root + "/assets/entity_icons/music-note.svg", ui(18));
  onx_notification_ui::init(w, h, nf, {});
  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(onx_display_settings::background()), 0);
}
}  // namespace rc

lvgl_emu::App lvgl_emu_app(const lvgl_emu::Args &args) {
  rc::root = args.get("root", ".");
  const std::string model = args.get("model", "28");
  const std::string scenario = args.get("scenario", rc::root + "/emulator/scenarios/demo.json");
  rc::board = rc::load_board(model);
  rc::model_name = model;
  // --landscape 1: the panel turned a quarter (native 480x320 or 320x240), as
  // the orientation setting does; shots are named 28L-/35L-.
  if (args.get("landscape", "") == "1") {
    const int old_h = rc::board.height;
    std::swap(rc::board.width, rc::board.height);
    rc::board.strip_center_y = rc::board.height - (old_h - rc::board.strip_center_y);
    rc::model_name = model + "L";
  }
  rc::preview_dir = args.get("preview");
  if (!rc::preview_dir.empty()) std::system(("mkdir -p '" + rc::preview_dir + "'").c_str());
  std::printf("%s: %dx%d ui_scale=%.4f font_scale=%.4f dock y=%d icon=%d marker=%d symbols=%s\n", rc::board.model.c_str(),
              rc::board.width, rc::board.height, rc::board.ui_scale, rc::board.font_scale, rc::board.strip_center_y,
              rc::board.strip_icon, rc::board.strip_marker, rc::board.symbol_font.c_str());
  lvgl_emu::App app;
  app.width = rc::board.width;
  app.height = rc::board.height;
  app.name = rc::model_name;
  app.setup = rc::setup;
  app.screens = [scenario]() {
    std::vector<lvgl_emu::Screen> screens;
    for (const auto &shot : rc::load_scenario(scenario)) screens.push_back({shot.name, [shot] { rc::show(shot); }});
    return screens;
  };
  return app;
}
