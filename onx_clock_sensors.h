#pragma once
// Optional sensors that replace the weather temperature and humidity on the
// clock view. Empty entity: use the weather entity's attributes.
#include "custom_logic_entity_sub.h"
#include "onx_subscriptions.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace onx_clock_sensors {
struct Config {
  uint32_t magic{0x4F4E5843};
  char temperature[129]{}, humidity[129]{};
};
struct Reading { std::string value, unit; };
inline Config config;
inline Reading temperature, humidity;
inline bool loaded = false;
inline esphome::ESPPreferenceObject preference;

inline bool valid(const std::string &entity) {
  return entity.empty() || (entity.size() <= 128 && entity.rfind("sensor.", 0) == 0 &&
                            custom_logic_entity_sub::is_valid_entity_id(entity));
}
inline void load() {
  if (loaded || esphome::global_preferences == nullptr) return;
  preference = esphome::global_preferences->make_preference<Config>(esphome::fnv1_hash("onx.clock_sensors.v1"));
  Config saved{};
  if (preference.load(&saved) && saved.magic == 0x4F4E5843) {
    saved.temperature[128] = saved.humidity[128] = 0;
    if (valid(saved.temperature) && valid(saved.humidity)) config = saved;
  }
  loaded = true;
}
// Numbers show at most one decimal: "21.46" -> "21.5", "45.0" -> "45".
inline std::string format(const std::string &raw) {
  if (raw.empty() || raw == "unknown" || raw == "unavailable") return "";
  char *end = nullptr;
  const float value = std::strtof(raw.c_str(), &end);
  if (end == raw.c_str() || !std::isfinite(value)) return raw;
  char out[24];
  snprintf(out, sizeof(out), "%.1f", static_cast<double>(value));
  std::string text = out;
  if (text.size() > 2 && text.compare(text.size() - 2, 2, ".0") == 0) text.resize(text.size() - 2);
  return text == "-0" ? "0" : text;
}
inline void bind(esphome::api::APIServer *api, size_t slot, const std::string &entity, Reading *reading) {
  const auto who = onx_subs::owner(onx_subs::Kind::WEATHER, slot);
  *reading = Reading{};
  if (entity.empty()) { onx_subs::registry.release(who); return; }
  onx_subs::registry.release(who, &entity);
  onx_subs::registry.acquire(api, who, entity, "", [reading](esphome::StringRef v) { reading->value = format(v.str()); });
  onx_subs::registry.acquire(api, who, entity, "unit_of_measurement",
                             [reading](esphome::StringRef v) { reading->unit = v.str(); });
}
inline void subscribe(esphome::api::APIServer *api) {
  load();
  if (api == nullptr) return;
  bind(api, 1, config.temperature, &temperature);
  bind(api, 2, config.humidity, &humidity);
}
// Empty values leave the setting unchanged; "none" clears it.
inline bool save(esphome::api::APIServer *api, const std::string &temp, const std::string &hum) {
  load();
  auto next = config;
  auto apply = [](const std::string &value, char (&out)[129]) {
    if (value.empty()) return true;
    const std::string entity = value == "none" ? "" : value;
    if (!valid(entity)) return false;
    snprintf(out, sizeof(out), "%s", entity.c_str());
    return true;
  };
  if (!apply(temp, next.temperature) || !apply(hum, next.humidity)) return false;
  if (std::string(next.temperature) == config.temperature && std::string(next.humidity) == config.humidity) return true;
  if (!preference.save(&next) || !esphome::global_preferences->sync()) return false;
  config = next;
  subscribe(api);
  return true;
}
inline std::string temperature_text(const std::string &weather_value, const std::string &weather_unit) {
  if (config.temperature[0]) {
    return (temperature.value.empty() ? std::string("--") : temperature.value) + temperature.unit;
  }
  const auto unit = weather_unit.empty() ? std::string("°C") : weather_unit;
  return (weather_value.empty() || weather_value == "unknown" ? std::string("--") : weather_value) + unit;
}
inline std::string humidity_text(const std::string &weather_value) {
  if (config.humidity[0]) {
    return (humidity.value.empty() ? std::string("--") : humidity.value) + humidity.unit;
  }
  return (weather_value.empty() || weather_value == "unknown" ? std::string("--") : weather_value) + "%";
}
}  // namespace onx_clock_sensors
