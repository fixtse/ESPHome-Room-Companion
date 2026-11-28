#pragma once
// The configuration page's "preview" data: what the panel shows for an entity
// (and the rest of its page), as JSON for the web previews (panel_web.h).
// Shared by the firmware (mode_router.yaml) and the emulator, so web previews
// can be checked against device renders from the same state.
#include <cmath>
#include <functional>
#include <string>
#include <ArduinoJson.h>
#include "onx_lyrics.h"
#include "esphome/components/onx_storage/onx_storage.h"

namespace onx_preview {
// What the preview needs from the running device (ESPHome globals).
struct Context {
  uint32_t now_epoch{0};
  bool clock_valid{false};
  int panel_width{0}, panel_height{0};
  int device_count{0};
  bool api_connected{false};
  std::string ha_host;
  int detail_light_mode{0};
  std::function<esphome::onx_storage::NormalLightState(int ui)> light;
  std::function<int(int ui)> light_mode;  // The light detail's mode when that slot's detail is open, else 0.
  std::function<std::string(int ui)> entity_at, type_at;
};

inline void build(JsonObject root, const std::string &query_entity, const Context &c) {
  const auto clock = esphome::ESPTime::from_epoch_local(c.now_epoch);
  root["clock_epoch"] = c.clock_valid ? double(clock.timestamp) : 0;
  #ifdef USE_ROOM_COMPANION
  root["panel_width"] = c.panel_width; root["panel_height"] = c.panel_height;
  #endif
  JsonArray slots = root["slots"].to<JsonArray>();
  auto encode = [&](JsonObject target, const onx_detail_view::View &view) {
    target["layout"] = view.layout; target["available"] = view.available;
    target["icon"] = view.icon; target["accent"] = view.accent;
    target["icon_x"] = view.icon_x; target["icon_y"] = view.icon_y; target["icon_size"] = view.icon_size;
    target["value_ring"] = view.value_ring;
    target["light_mode"] = view.light_mode; target["selector_value"] = view.selector_value;
    JsonArray lines = target["lines"].to<JsonArray>();
    for (const auto *line : {&view.badge, &view.value, &view.context, &view.hint, &view.name}) {
      auto item = lines.add<JsonObject>();
      item["text"] = line->text; item["x"] = line->x; item["y"] = line->y;
      item["size"] = line->size; item["width"] = line->width; item["color"] = line->color;
    }
  };
  #ifdef USE_ROOM_COMPANION
  // Any member returns its whole page, so selecting a second member keeps
  // the other positions' live data.
  const auto *page = onx_panel_pages::info(query_entity);
  #endif
  for (size_t i = 0; i < (int)custom_logic::CONFIGURABLE_DEVICE_SLOT_COUNT; ++i) {
    const auto entity = custom_logic::cached_device_entity_id(i);
    if (entity.empty()) continue;
    #ifdef USE_ROOM_COMPANION
    const bool member = entity == query_entity ||
        (page && std::find(page->entries.begin(), page->entries.end(), i) != page->entries.end());
    if (!member) continue;
    #else
    if (entity != query_entity) continue;
    #endif
    const auto title = custom_logic::ha_entity_title_from_registry_cache(entity);
    auto view = onx_detail_view::build(entity, title);
    onx_entity_config::customize_control_view(custom_logic::normal_ui_slot_for_device_config_slot(i),entity,view);
    if(entity.rfind("light.",0)==0) {
      const int ui=custom_logic::normal_ui_slot_for_device_config_slot(i);
      const int mode=c.light_mode(ui);
      view=onx_detail_view::light_view(entity,title,c.light(ui),mode);
    }
    #ifdef USE_ROOM_COMPANION
    if (custom_logic_entity_sub::is_timer_detail(entity)) view=onx_panel_pages::timer_view(entity,title);
    #endif
    auto slot = slots.add<JsonObject>();
    slot["entity"] = entity; encode(slot, view);
    #ifdef USE_ROOM_COMPANION
    // The action-grid tile's state line, so the editor preview matches the panel.
    slot["grid_badge"] = onx_panel_pages::grid_badge(entity, onx_panel_pages::item(entity, false), custom_logic_entity_sub::entity_detail(entity));
    #endif
    // History for every entry with a graph (sensors, smart-plug switches).
    if(view.layout=="graph" || (onx_sensor_view::index(entity)>=0 && onx_sensor_view::configs[onx_sensor_view::index(entity)].graph)) {
      const int index=onx_sensor_view::index(entity);
      if(index>=0) {
        const uint32_t now=c.now_epoch;
        auto points=slot["graph"].to<JsonArray>();
        for(const auto &p:onx_sensor_view::states[index].history(now)) {auto point=points.add<JsonObject>();point["t"]=p.epoch;point["value"]=p.value;}
        const auto history=onx_sensor_view::states[index].history(now);
        if(!history.empty()) {
          float low=history.front().value,high=low;
          for(const auto &point:history){low=std::min(low,point.value);high=std::max(high,point.value);}
          const auto labels=onx_sensor_view::axis_labels(low,high);
          slot["graph_min_label"]=labels.first;slot["graph_max_label"]=labels.second;
        }
        slot["graph_now"]=now;
      }
    }
    JsonObject carousel = slot["carousel"].to<JsonObject>();
    encode(carousel, onx_detail_view::carousel(entity, title));
    auto navigator=carousel["navigator"].to<JsonArray>();
    const int ui_slot=custom_logic::normal_ui_slot_for_device_config_slot(i);
    const int total=c.device_count;
    const auto entries=onx_entity_config::carousel_entries(total);
    const auto current=std::find(entries.begin(),entries.end(),ui_slot);
    if(current!=entries.end()) {
      const int rank=current-entries.begin();
      const int visible = std::min<int>(entries.size(), onx_navigation::VISIBLE_ICONS);
      const int first = std::clamp(rank - visible / 2, 0, int(entries.size()) - visible);
      for (int n = first; n < first + visible; ++n) {
        const int index=entries[n];
        auto item=navigator.add<JsonObject>();
        item["visible"]=true;item["selected"]=index==ui_slot;item["offset"]=n-rank;
        const auto neighbour=index==total?std::string(""):c.entity_at(index);
        auto icon=onx_icon_state::selected_icon(neighbour);
        if(icon.empty())icon="mdi:"+onx_icon_state::fallback(neighbour,c.type_at(index));
        #ifdef USE_ROOM_COMPANION
        if(const auto *page=onx_panel_pages::led_by(neighbour);page) {
          if(const auto ref=onx_panel_pages::icon_entity(*page);!ref.empty()) {
            icon=onx_icon_state::selected_icon(ref);
            if(icon.empty())icon="mdi:"+onx_icon_state::fallback(ref);
          } else if(!onx_panel_pages::entity_icon(page->icon)) {
            if(page->icon[0])icon=page->icon;
            else if(onx_panel_pages::Kind(page->kind)==onx_panel_pages::Kind::ACTION_GRID)icon="mdi:view-grid-outline";
          }
        }
        item["entity"]=neighbour;
        #endif
        item["icon"]=index==total?"mdi:cog":icon;
      }
    }
    if (const auto *detail = custom_logic_entity_sub::entity_detail(entity)) {
      #ifdef USE_ROOM_COMPANION
      auto panel = slot["panel"].to<JsonObject>();
      panel["state"] = detail->state; panel["unit"] = detail->unit;
      panel["temperature"] = detail->target; panel["current"] = detail->current;
      panel["humidity"] = detail->humidity; panel["feels_like"] = detail->apparent_temperature;
      panel["wind"] = detail->wind_speed; panel["wind_unit"] = detail->wind_unit;
      panel["pressure"] = detail->pressure; panel["pressure_unit"] = detail->pressure_unit;
      panel["visibility"] = detail->visibility; panel["visibility_unit"] = detail->visibility_unit;
      panel["uv"] = detail->uv_index; panel["title"] = detail->media_title; panel["artist"] = detail->media_artist;
      if (onx_media::has_playback(detail->state) && onx_lyrics::shown(entity, detail->media_title)) {
        auto projection = detail->progress;
        const auto pv = projection.view(esphome::millis(), double(std::time(nullptr)), detail->available);
        const int li = pv.visible ? onx_lyrics::index_at(pv.seconds) : -1;
        auto lyric = panel["lyrics"].to<JsonArray>();
        std::string cur = onx_lyrics::line(li);
        if (cur.empty() && li >= 0) cur = "\xC2\xB7 \xC2\xB7 \xC2\xB7";
        lyric.add(onx_lyrics::line(li - 1)); lyric.add(cur); lyric.add(onx_lyrics::line(li + 1));
      }
      panel["forecast_pending"] = detail->forecast_waiting(esphome::millis());
      if(entity.rfind("light.",0)==0) {
        const auto light=c.light(custom_logic::normal_ui_slot_for_device_config_slot(i));
        panel["brightness"] = light.brightness_pct; panel["dimmable"] = light.supports_brightness();
        panel["light_on"] = light.is_on; panel["kelvin"] = light.color_temperature_kelvin;
        panel["kelvin_min"] = light.min_color_temperature_kelvin; panel["kelvin_max"] = light.max_color_temperature_kelvin;
        panel["supports_ct"] = light.supports_color_temperature(); panel["supports_rgb"] = light.supports_rgb();
        panel["hue"] = std::isfinite(detail->light_hue) ? detail->light_hue : light.hue_degrees();
        panel["saturation"] = std::isfinite(detail->light_saturation) ? detail->light_saturation : 100.0f;
        panel["hs_known"] = std::isfinite(detail->light_hue) && std::isfinite(detail->light_saturation);
        panel["light_temperature"] = detail->light_temperature;
        panel["light_mode"] = c.detail_light_mode;
      }
      // Everything the panel's own screens read (onx_panel_detail.h), so the
      // web previews draw the same controls.
      panel["available"] = detail->available; panel["received"] = detail->received; panel["features"] = detail->features;
      panel["low"] = detail->low; panel["high"] = detail->high; panel["modes"] = detail->modes;
      panel["presets"] = detail->presets; panel["preset"] = detail->preset;
      panel["fan_modes"] = detail->fan_modes; panel["swing_modes"] = detail->swing_modes;
      panel["hvac"] = onx_controls::effective(*detail,"state");
      panel["preset_mode"] = onx_controls::effective(*detail,"preset_mode");
      panel["fan_mode"] = onx_controls::effective(*detail,"fan_mode");
      panel["swing_mode"] = onx_controls::effective(*detail,"swing_mode");
      panel["target_low"] = onx_controls::effective(*detail,"target_temp_low");
      panel["target_high"] = onx_controls::effective(*detail,"target_temp_high");
      panel["percentage"] = onx_controls::effective(*detail,"percentage");
      panel["oscillating"] = onx_controls::effective(*detail,"oscillating");
      panel["direction"] = onx_controls::effective(*detail,"direction");
      if (entity.rfind("cover.",0)==0) {
        const auto cover = onx_entity_config::cover_presets(entity);
        panel["cover_presets"] = onx_cover_presets::encode(cover);
        panel["cover_presets_enabled"] = cover.enabled ? "1" : "0";
      }
      panel["position"] = detail->position; panel["options"] = detail->options;
      panel["event_type"] = detail->event_type;
      panel["battery_level"] = detail->battery_level; panel["status"] = detail->status;
      panel["motion_detection"] = detail->motion_detection;
      panel["changed_by"] = detail->changed_by; panel["code_format"] = detail->code_format;
      panel["code_arm_required"] = detail->code_arm_required;
      if (entity.rfind("vacuum.", 0) == 0) {
        panel["fan_speed"] = onx_controls::effective(*detail, "fan_speed");
        const auto *rooms = onx_panel_detail::vacuum_rooms ? onx_panel_detail::vacuum_rooms(entity) : nullptr;
        if (onx_vacuum_rooms::usable(rooms)) panel["rooms"] = rooms->count;
      }
      if(std::isfinite(detail->progress.duration))panel["duration"] = detail->progress.duration;
      panel["value"] = onx_controls::effective(*detail,"state");
      if(std::isfinite(detail->minimum))panel["minimum"] = detail->minimum;
      if(std::isfinite(detail->maximum))panel["maximum"] = detail->maximum;
      if(std::isfinite(detail->step))panel["step"] = detail->step;
      panel["sensor_class"] = onx_detail_view::sensor_class(entity);
      panel["wind_bearing"] = detail->wind_bearing;
      if(const auto *log=onx_panel_detail::changes_for(entity)) {
        auto changes=panel["changes"].to<JsonArray>();
        for(const auto &c:log->items){auto item=changes.add<JsonObject>();item["state"]=c.state;item["at"]=uint32_t(c.at);}
      }
      auto forecast=panel["forecast"].to<JsonArray>();
      for(int f=0;f<detail->forecast_count;++f) {
        auto day=forecast.add<JsonObject>();
        day["date"]=detail->forecast[f].date;day["condition"]=detail->forecast[f].condition;
        day["high"]=detail->forecast[f].high;day["low"]=detail->forecast[f].low;day["rain"]=detail->forecast[f].rain;
      }
      auto actions=panel["actions"].to<JsonArray>();
      if(const auto *list=onx_entity_config::playlists_for_ui(custom_logic::normal_ui_slot_for_device_config_slot(i),entity))
        for(int a=0;a<list->count;++a)actions.add(list->entries[a].name);
      const int sensor=onx_sensor_view::index(entity);
      if(sensor>=0)panel["history_range"]=onx_sensor_view::states[sensor].range_seconds;
      // Smart plugs: the configured power/energy reading for the switch card.
      if(sensor>=0 && entity.rfind("switch.",0)==0 && onx_sensor_view::configs[sensor].secondary[0]) {
        const auto &reading=onx_sensor_view::states[sensor];
        if(reading.received && reading.available) {
          panel["power"]=onx_detail_view::compact(reading.value);panel["power_unit"]=reading.unit;
        }
      }
      if(sensor>=0 && entity.rfind("camera.",0)==0 && onx_sensor_view::configs[sensor].secondary[0])
        panel["ptz"]=onx_sensor_view::configs[sensor].secondary;
      if(entity.rfind("media_player.",0)==0 && (detail->features & 4))
        panel["volume"]=onx_controls::effective(*detail,"volume_level");
      #endif
      if (custom_logic_entity_sub::is_timer_detail(entity)) {
        time_t finish = 0;
        const bool has_finish = custom_logic_entity_sub::parse_ha_datetime_epoch(detail->timer_finishes_at, &finish);
        auto timer = slot["timer"].to<JsonObject>();
        timer["state"] = detail->state;
        timer["finishes_at"] = detail->timer_finishes_at;
        timer["remaining"] = detail->timer_remaining;
        timer["duration"] = detail->voice_timer ? onx_timer::duration_argument(uint32_t(detail->voice_duration)) : detail->timer_duration;
        if (detail->voice_timer) {
          timer["voice"] = true;
          timer["remaining"] = onx_timer::duration_argument(uint32_t(detail->voice_remaining));
          if (detail->state == "active") timer["deadline_seconds_left"] = std::max(0.0,
              detail->voice_remaining - (esphome::millis() - detail->timer_received_ms) / 1000.0);
        }
        if (has_finish && c.clock_valid) timer["deadline_seconds_left"] = double(finish) - clock.timestamp;
      }
      const auto now = esphome::ESPTime::from_epoch_local(c.now_epoch);
      auto projection = detail->progress;
      const auto progress = projection.view(esphome::millis(), c.clock_valid ? static_cast<double>(now.timestamp) : NAN,
          c.api_connected && detail->available);
      slot["progress"] = progress.visible ? progress.permille : -1;
      slot["playing"] = detail->state == "playing";
      slot["paused"] = detail->state == "paused";
      const auto overlay = onx_media::transport.view(entity, detail->state, esphome::millis());
      slot["overlay"] = onx_media::overlay_name(overlay);
      slot["overlay_path"] = onx_media::overlay_path(overlay);
      slot["pause_path"] = onx_media::overlay_path(onx_media::Overlay::PAUSE);
      slot["overlay_ms"] = onx_media::transport.remaining_ms(entity, esphome::millis());
      slot["ring_color"] = onx_display_settings::current.media_color;
      slot["artwork"] = (entity.rfind("camera.", 0) == 0 && detail->available) || onx_media::has_playback(detail->state)
          ? onx_media::artwork_url(c.ha_host, detail->picture) : "";
    }
  }
}
}  // namespace onx_preview
