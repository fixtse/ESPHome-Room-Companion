#pragma once
// Named control presets.
//
// A preset is a snapshot of all configuration entries in the same shape the configuration
// page saves (entity, name, press action, type, icon, secondary sensor, timer
// visibility, gestures and playlists). Presets live in their own NVS namespace
// and only their names stay in RAM; a preset's slots are read when it is applied.
//
// Applying a preset compares each stored slot with the slot as it is now and
// queues only the slots that differ through the configuration page's regular
// save handler, one per loop. That keeps validation, persistence, subscriptions
// and memory release identical to editing the slots by hand, without a reboot
// and without blocking the main loop.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#ifndef ONX_PRESETS_TEST
#include "onx_runtime_diagnostics.h"
#define ONX_PRESET_TRACE(name, entity) onx_runtime::record(name, entity)
#include "onx_entity_config.h"
#include "esphome/components/onx_config_web/onx_config_web.h"
#include "esphome/components/select/select.h"
#endif
#ifdef USE_ESP32
#include "nvs.h"
#else
#include <map>
#endif

#ifndef ONX_PRESET_TRACE
#define ONX_PRESET_TRACE(name, entity) ((void) 0)
#endif

namespace onx_presets {
static const char *const TAG = "onx_presets";
constexpr size_t MAX_PRESETS = 8;
constexpr size_t MAX_NAME = 32;
constexpr size_t MAX_BYTES = 32768;
constexpr size_t SLOT_COUNT = onx_memory::ENTRY_COUNT;
// Shown by the Home Assistant select when the slots match no stored preset.
inline const char *const CUSTOM = "Custom";
inline const char *const NAMESPACE = "onx_presets";

struct Summary {
  std::string name;
  uint8_t key{0};
  uint8_t configured{0};
  uint32_t bytes{0};
};

inline std::vector<Summary> summaries;
inline std::string active;     // Last preset applied completely; empty after manual edits.
inline std::string applying;   // Preset whose slots are still queued.
inline size_t apply_total{0}, apply_failed{0};
inline bool loaded{false};
inline esphome::onx_config_web::ConfigWeb *web{nullptr};
inline esphome::select::Select *selector{nullptr};
inline std::vector<std::string> option_storage;
inline std::function<void()> on_options_changed;

// ---- storage -------------------------------------------------------------
// Preset documents (up to 32 KB) live in PSRAM: plain std::string uses internal
// RAM only, which is far too fragmented for them and aborts on failure.
using Blob = custom_logic::PsramString;
inline const char *blob_text(const Blob &blob) { return blob.c_str(); }
// Serializes a document straight into a PSRAM buffer.
inline bool serialize_blob(const JsonDocument &doc, Blob &out) {
  const size_t size = measureJson(doc);
  out.clear();
  out.resize(size + 1);
  if (out.size() != size + 1) return false;
  serializeJson(doc, &out[0], size + 1);
  out.resize(size);
  return true;
}
inline std::string key_name(uint8_t key) { return "p" + std::to_string(key); }
#ifdef USE_ESP32
template<typename S> inline bool store_read(const std::string &key, S &out) {
  nvs_handle_t handle;
  if (nvs_open(NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;
  size_t size = 0;
  bool ok = nvs_get_blob(handle, key.c_str(), nullptr, &size) == ESP_OK && size <= MAX_BYTES + 64;
  if (ok) {
    out.assign(size, '\0');
    ok = out.size() == size && nvs_get_blob(handle, key.c_str(), &out[0], &size) == ESP_OK;
    out.resize(ok ? size : 0);
  }
  nvs_close(handle);
  return ok;
}
template<typename S> inline bool store_write(const std::string &key, const S &value) {
  nvs_handle_t handle;
  if (nvs_open(NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
  esp_err_t err = nvs_set_blob(handle, key.c_str(), value.data(), value.size());
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  if (err != ESP_OK) ESP_LOGW(TAG, "Could not store %s (%u bytes): %s", key.c_str(), unsigned(value.size()), esp_err_to_name(err));
  return err == ESP_OK;
}
inline bool store_erase(const std::string &key) {
  nvs_handle_t handle;
  if (nvs_open(NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
  esp_err_t err = nvs_erase_key(handle, key.c_str());
  if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return err == ESP_OK;
}
#else
inline std::map<std::string, std::string> memory_store;
template<typename S> inline bool store_read(const std::string &key, S &out) {
  auto it = memory_store.find(key);
  if (it == memory_store.end()) return false;
  out.assign(it->second.data(), it->second.size());
  return true;
}
template<typename S> inline bool store_write(const std::string &key, const S &value) {
  memory_store[key] = std::string(value.data(), value.size());
  return true;
}
inline bool store_erase(const std::string &key) { memory_store.erase(key); return true; }
#endif

// ---- names ---------------------------------------------------------------
inline std::string trim(const std::string &value) {
  const auto begin = value.find_first_not_of(" \t");
  if (begin == std::string::npos) return "";
  const auto end = value.find_last_not_of(" \t");
  return value.substr(begin, end - begin + 1);
}
inline bool same_name(const std::string &a, const std::string &b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  return true;
}
inline bool valid_name(const std::string &name) {
  if (name.empty() || name.size() > MAX_NAME || name != trim(name) || same_name(name, CUSTOM)) return false;
  for (unsigned char ch : name)
    if (ch < 0x20 || ch == 0x7F) return false;
  return true;
}
inline const Summary *find(const std::string &name) {
  for (const auto &item : summaries)
    if (same_name(item.name, name)) return &item;
  return nullptr;
}

// ---- Home Assistant select -----------------------------------------------
inline void publish_select() {
  if (selector == nullptr) return;
  const std::string &shown = !applying.empty() ? applying : active;
  size_t index = 0;
  for (size_t i = 1; i < option_storage.size(); ++i)
    if (same_name(option_storage[i], shown)) index = i;
  selector->publish_state(index);
}
// Rebuilds the select's options from the stored names. The option pointers stay
// valid until the next rebuild, which replaces them in the same call.
inline void refresh_select() {
  if (selector == nullptr) return;
  ONX_PRESET_TRACE("preset_select", std::string());
  std::vector<std::string> next;
  next.reserve(summaries.size() + 1);
  next.emplace_back(CUSTOM);
  for (const auto &item : summaries) next.push_back(item.name);
  const bool changed = next != option_storage;
  if (changed) {
    option_storage = std::move(next);
    esphome::FixedVector<const char *> options;
    options.init(option_storage.size());
    for (const auto &name : option_storage) options.push_back(name.c_str());
    selector->traits.set_options(options);
  }
  publish_select();
  // Home Assistant reads a select's options when it connects.
  if (changed && on_options_changed) on_options_changed();
}

inline void persist_active() {
  if (active.empty()) store_erase("active");
  else store_write("active", active);
}

// ---- snapshot ------------------------------------------------------------
inline uint8_t configured_count(JsonArrayConst slots) {
  uint8_t count = 0;
  for (JsonObjectConst slot : slots)
    if (!(slot["entity"] | std::string()).empty()) ++count;
  return count;
}

inline void load() {
  if (loaded) return;
  loaded = true;
  summaries.clear();
  for (uint8_t key = 0; key < MAX_PRESETS; ++key) {
    Blob raw;
    if (!store_read(key_name(key), raw)) continue;
    auto doc = custom_logic::make_psram_json_document();
    if (deserializeJson(doc, blob_text(raw), raw.size()) || !doc["slots"].is<JsonArrayConst>()) {
      ESP_LOGW(TAG, "Ignoring unreadable preset %s", key_name(key).c_str());
      continue;
    }
    const std::string name = doc["name"] | std::string();
    if (!valid_name(name) || find(name) != nullptr) continue;
    summaries.push_back(Summary{name, key, configured_count(doc["slots"].as<JsonArrayConst>()), uint32_t(raw.size())});
  }
  std::string stored;
  active = store_read("active", stored) && find(stored) != nullptr ? find(stored)->name : "";
}

// Pages (onx_panel_pages.h) reference entries by index, so a preset carries its
// own page list. Presets without one (older backups) get one page per entity.
inline std::function<void(JsonArray)> encode_pages;
inline std::function<void(const std::string &)> apply_pages;
inline std::string pending_pages;
inline void encode_current(JsonArray slots) {
  onx_entity_config::load_icons();
  onx_entity_config::load_playlists();
  onx_entity_config::load_gestures();
  custom_logic::load_ha_entities_if_needed();
  for (size_t i = 0; i < SLOT_COUNT; ++i) onx_entity_config::encode_slot(i, slots.add<JsonObject>());
}

// Builds the save request the configuration page would send for one slot.
inline bool slot_query(size_t index, JsonObjectConst slot, esphome::onx_config_web::Query &query) {
  query = esphome::onx_config_web::Query{};
  query.op = "save";
  query.slot = static_cast<int>(index);
  query.entity = slot["entity"] | std::string();
  query.action = slot["action"] | std::string();
  query.name = slot["name"] | std::string();
  query.area = slot["area"] | std::string();
  query.action_name = slot["actionName"] | std::string();
  query.type = slot["typeOption"] | std::string();
  query.icon = slot["icon"] | std::string();
  query.icon_override = slot["iconOverride"] | std::string("0");
  query.secondary = slot["secondary"] | std::string();
  query.secondary_text = slot["secondaryText"] | std::string("{value}");
  query.sensor_graph = slot["sensorGraph"] | std::string("0");
  query.timer_hidden = slot["timerHidden"] | std::string("0");  // Registered timer: no page of its own.
  // Canonical gestures and playlists: fixed key order and defaults, so a backup
  // that omits or reorders them compares equal to the same saved slot.
  {
    JsonDocument canonical;
    JsonObjectConst gestures = slot["gestures"].as<JsonObjectConst>();
    for (const char *key : {"cw", "ccw", "holdCw", "holdCcw", "feedback", "popup", "responses", "pressResponses", "events", "step"}) {
      const std::string fallback = std::strcmp(key, "popup") == 0 || std::strcmp(key, "step") == 0 ? "1" :
          (std::strcmp(key, "responses") == 0 || std::strcmp(key, "pressResponses") == 0 || std::strcmp(key, "events") == 0) ? "0" : "";
      canonical[key] = gestures.isNull() ? fallback : (gestures[key] | fallback);
    }
    serializeJson(canonical, query.gestures);
  }
  {
    JsonDocument canonical;
    JsonArray list = canonical.to<JsonArray>();
    for (JsonObjectConst entry : slot["playlists"].as<JsonArrayConst>()) {
      auto item = list.add<JsonObject>();
      item["name"] = entry["name"] | std::string();
      item["action"] = entry["action"] | std::string();
    }
    serializeJson(canonical, query.playlists);
  }
  // The same bounds the HTTP endpoint enforces before reaching the handler.
  return query.entity.size() <= 128 && query.action.size() <= 128 && query.name.size() <= 192 &&
         query.area.size() <= 192 && query.action_name.size() <= 192 && query.type.size() <= 32 &&
         query.icon.size() <= 80 && query.icon_override.size() <= 1 && query.secondary.size() <= 128 &&
         query.secondary_text.size() <= 160 && query.sensor_graph.size() <= 1 && query.timer_hidden.size() <= 1 &&
         query.gestures.size() <= 1600 && query.playlists.size() <= 2048;
}

// Two slots are the same when the save requests they produce are the same, so
// key order or extra informational fields in a stored preset do not matter.
inline bool same_query(const esphome::onx_config_web::Query &a, const esphome::onx_config_web::Query &b) {
  return a.entity == b.entity && a.action == b.action && a.name == b.name && a.area == b.area &&
         a.action_name == b.action_name && a.type == b.type && a.icon == b.icon && a.icon_override == b.icon_override &&
         a.secondary == b.secondary && a.secondary_text == b.secondary_text && a.sensor_graph == b.sensor_graph &&
         a.timer_hidden == b.timer_hidden && a.gestures == b.gestures && a.playlists == b.playlists;
}

// ---- operations ----------------------------------------------------------
// Each returns an empty string on success or a message for the user.
// Stores a {"slots":[...]} document under a name, replacing a preset with the
// same name. mark_active records that the panel's controls match it now.
inline std::string store_document(const std::string &raw_name, JsonDocument &doc, bool mark_active) {
  load();
  const std::string requested = trim(raw_name);
  if (!valid_name(requested)) return "Use a preset name of 1-32 characters other than \"Custom\"";
  const Summary *existing = find(requested);
  // Updating keeps the stored spelling; rename_preset() changes it.
  const std::string name = existing != nullptr ? existing->name : requested;
  uint8_t key = 0;
  if (existing != nullptr) {
    key = existing->key;
  } else {
    if (summaries.size() >= MAX_PRESETS) return "All 8 presets are in use; delete one first";
    while (std::any_of(summaries.begin(), summaries.end(), [key](const Summary &s) { return s.key == key; })) ++key;
  }
  if (!doc["slots"].is<JsonArrayConst>()) return "A preset needs a list of slots";
  ONX_PRESET_TRACE("preset_store", name);
  doc["name"] = name;
  if (doc.overflowed()) return "Not enough memory for this preset";
  const uint8_t configured = configured_count(doc["slots"].as<JsonArrayConst>());
  Blob raw;
  if (!serialize_blob(doc, raw)) return "Not enough memory to store this preset";
  if (raw.size() > MAX_BYTES) return "These controls are too large to store as a preset";
  if (!store_write(key_name(key), raw)) return "Could not store the preset; device storage may be full";
  if (existing != nullptr) {
    for (auto &item : summaries)
      if (item.key == key) item = Summary{name, key, configured, uint32_t(raw.size())};
  } else {
    summaries.push_back(Summary{name, key, configured, uint32_t(raw.size())});
    // Same order as after a reboot, which loads presets by storage key.
    std::sort(summaries.begin(), summaries.end(), [](const Summary &a, const Summary &b) { return a.key < b.key; });
  }
  if (mark_active && applying.empty()) {
    active = name;
    persist_active();
  } else if (!mark_active && same_name(active, name)) {
    // Replaced from a backup: the panel's controls may no longer match it.
    active.clear();
    persist_active();
  }
  ESP_LOGI(TAG, "Saved preset \"%s\" (%u controls, %u bytes)", name.c_str(), configured, unsigned(raw.size()));
  refresh_select();
  return "";
}

inline std::string save_current(const std::string &raw_name) {
  ONX_PRESET_TRACE("preset_capture", raw_name);
  auto doc = custom_logic::make_psram_json_document();
  encode_current(doc["slots"].to<JsonArray>());
  if (encode_pages) encode_pages(doc["pages"].to<JsonArray>());
  if (doc.overflowed()) return "Not enough memory to capture the current controls";
  return store_document(raw_name, doc, true);
}

// The stored preset ({"name":...,"slots":[...]}) for backups, in chunks so no
// response needs a preset-sized buffer in internal RAM.
constexpr size_t EXPORT_CHUNK = 3072;
inline std::string export_chunk(const std::string &raw_name, size_t offset) {
  load();
  const Summary *summary = find(trim(raw_name));
  if (summary == nullptr) return "";
  Blob raw;
  if (!store_read(key_name(summary->key), raw) || offset > raw.size()) return "";
  size_t length = std::min(EXPORT_CHUNK, raw.size() - offset);
  // Never split a UTF-8 sequence: end before a continuation byte.
  while (length > 0 && offset + length < raw.size() && (static_cast<uint8_t>(raw[offset + length]) & 0xC0) == 0x80) --length;
  return onx_entity_config::build_json([&](JsonObject root) {
    root["name"] = summary->name;
    root["total"] = raw.size();
    root["offset"] = offset;
    root["next"] = offset + length;
    root["data"] = std::string(raw.data() + offset, length);
  });
}

// Restoring a preset from a backup arrives in sequential chunks, because one
// configuration request is limited to 8 KB.
inline std::string upload_name;
inline Blob upload_buffer;
inline size_t upload_total{0};
inline std::string import_chunk(const std::string &raw_name, size_t offset, size_t total, const std::string &data,
                                bool &complete) {
  complete = false;
  const std::string name = trim(raw_name);
  if (offset == 0) {
    upload_name.clear();
    Blob().swap(upload_buffer);
    if (!valid_name(name)) return "Use a preset name of 1-32 characters other than \"Custom\"";
    if (total == 0 || total > MAX_BYTES) return "This preset is too large to store";
    if (find(name) == nullptr && summaries.size() >= MAX_PRESETS) return "All 8 presets are in use; delete one first";
    upload_name = name;
    upload_total = total;
    upload_buffer.reserve(total);
  } else if (name != upload_name || offset != upload_buffer.size() || total != upload_total) {
    return "Preset upload was interrupted; import it again";
  }
  if (upload_buffer.size() + data.size() > upload_total) return "Preset upload is larger than announced";
  upload_buffer.append(data.data(), data.size());
  if (upload_buffer.size() < upload_total) return "";
  complete = true;
  Blob raw;
  raw.swap(upload_buffer);
  upload_name.clear();
  auto doc = custom_logic::make_psram_json_document();
  if (deserializeJson(doc, blob_text(raw), raw.size()) || !doc["slots"].is<JsonArray>()) return "The preset in this backup is unreadable";
  JsonArray slots = doc["slots"].as<JsonArray>();
  if (slots.size() > SLOT_COUNT) return "The preset in this backup has too many slots";
  for (size_t i = 0; i < slots.size(); ++i) {
    esphome::onx_config_web::Query query;
    if (!slots[i].is<JsonObject>() || !slot_query(i, slots[i].as<JsonObjectConst>(), query))
      return "Slot " + std::to_string(i + 1) + " of the preset in this backup is invalid";
  }
  return store_document(name, doc, false);
}

inline std::string remove_preset(const std::string &raw_name) {
  load();
  const Summary *existing = find(trim(raw_name));
  if (existing == nullptr) return "Preset not found";
  const uint8_t key = existing->key;
  const std::string name = existing->name;
  if (!store_erase(key_name(key))) return "Could not delete the preset";
  summaries.erase(std::remove_if(summaries.begin(), summaries.end(), [key](const Summary &s) { return s.key == key; }),
                  summaries.end());
  if (same_name(active, name)) { active.clear(); persist_active(); }
  ESP_LOGI(TAG, "Deleted preset \"%s\"", name.c_str());
  refresh_select();
  return "";
}

inline std::string rename_preset(const std::string &raw_from, const std::string &raw_to) {
  load();
  const Summary *existing = find(trim(raw_from));
  const std::string to = trim(raw_to);
  if (existing == nullptr) return "Preset not found";
  if (!valid_name(to)) return "Use a preset name of 1-32 characters other than \"Custom\"";
  const Summary *clash = find(to);
  if (clash != nullptr && clash->key != existing->key) return "Another preset already uses that name";
  Blob raw;
  if (!store_read(key_name(existing->key), raw)) return "Could not read the preset";
  auto doc = custom_logic::make_psram_json_document();
  if (deserializeJson(doc, blob_text(raw), raw.size())) return "The stored preset is unreadable";
  const std::string from = existing->name;
  doc["name"] = to;
  if (!serialize_blob(doc, raw)) return "Not enough memory to store the preset";
  if (!store_write(key_name(existing->key), raw)) return "Could not store the preset";
  for (auto &item : summaries)
    if (item.key == existing->key) { item.name = to; item.bytes = raw.size(); }
  if (same_name(active, from)) { active = to; persist_active(); }
  if (same_name(applying, from)) applying = to;
  refresh_select();
  return "";
}

inline void finish(const std::string &name) {
  if (!same_name(applying, name)) return;  // A newer apply replaced this one.
  if (apply_failed == 0) {
    active = applying;
    ESP_LOGI(TAG, "Preset \"%s\" applied (%u slots changed)", active.c_str(), unsigned(apply_total));
  } else {
    active.clear();
    ESP_LOGW(TAG, "Preset \"%s\" applied with %u of %u slots failing", applying.c_str(), unsigned(apply_failed),
             unsigned(apply_total));
  }
  if (apply_pages) apply_pages(pending_pages);
  pending_pages.clear();
  applying.clear();
  persist_active();
  publish_select();
}

inline std::string apply(const std::string &raw_name) {
  load();
  const Summary *summary = find(trim(raw_name));
  if (summary == nullptr) return "Preset not found";
  if (web == nullptr) return "Presets are not ready yet";
  const std::string name = summary->name;
  ONX_PRESET_TRACE("preset_apply", name);
  Blob raw;
  if (!store_read(key_name(summary->key), raw)) return "Could not read the preset";
  auto stored = custom_logic::make_psram_json_document();
  if (deserializeJson(stored, blob_text(raw), raw.size()) || !stored["slots"].is<JsonArrayConst>()) return "The stored preset is unreadable";
  raw.clear();
  raw.shrink_to_fit();
  JsonArrayConst slots = stored["slots"].as<JsonArrayConst>();
  if (slots.size() > SLOT_COUNT) return "The stored preset has too many slots";

  std::vector<esphome::onx_config_web::Query> queries;
  {
    auto current = custom_logic::make_psram_json_document();
    JsonArray now = current.to<JsonArray>();
    encode_current(now);
    for (size_t i = 0; i < SLOT_COUNT; ++i) {
      // Slots missing from an older preset are cleared.
      JsonObjectConst wanted = i < slots.size() ? slots[i].as<JsonObjectConst>() : JsonObjectConst();
      esphome::onx_config_web::Query query;
      if (wanted.isNull() || custom_logic::is_retired_panel_gpio(wanted["entity"] | std::string())) {
        if ((now[i]["entity"] | std::string()).empty()) continue;
        query.op = "save";
        query.slot = static_cast<int>(i);
        query.icon_override = "0";
        query.secondary_text = "{value}";
        query.sensor_graph = query.timer_hidden = "0";
        queries.push_back(std::move(query));
        continue;
      }
      if (!slot_query(i, wanted, query)) return "Slot " + std::to_string(i + 1) + " of the preset is invalid";
      esphome::onx_config_web::Query present;
      slot_query(i, now[i].as<JsonObjectConst>(), present);
      if (same_query(query, present)) continue;
      queries.push_back(std::move(query));
    }
  }
  pending_pages.clear();
  {
    auto pages = custom_logic::make_psram_json_document();
    if (stored["pages"].is<JsonArrayConst>()) pages["pages"] = stored["pages"];
    else pages["pages"].to<JsonArray>();
    serializeJson(pages, pending_pages);
  }
  web->clear_internal();  // A newer apply replaces an unfinished one.
  applying = name;
  apply_total = queries.size();
  apply_failed = 0;
  for (auto &query : queries) web->enqueue_internal(std::move(query));
  esphome::onx_config_web::Query done;
  done.op = "preset_done";
  done.name = name;
  web->enqueue_internal(std::move(done));
  ESP_LOGI(TAG, "Applying preset \"%s\": %u slots change", name.c_str(), unsigned(apply_total));
  publish_select();
  return "";
}

// Results of the queued slot saves.
inline void on_internal_result(const esphome::onx_config_web::Query &query, const std::string &result) {
  if (query.op != "save" || applying.empty()) return;
  if (result.find("\"error\"") != std::string::npos) {
    ++apply_failed;
    ESP_LOGW(TAG, "Preset \"%s\" slot %d: %s", applying.c_str(), query.slot + 1, result.c_str());
  }
}

// A slot was saved by hand; the controls no longer match a stored preset.
inline void mark_custom() {
  if (!applying.empty()) return;
  if (active.empty()) return;
  active.clear();
  persist_active();
  publish_select();
}

// Selecting from Home Assistant or the API.
inline std::string request(const std::string &name) {
  if (name == CUSTOM) { publish_select(); return ""; }
  const auto error = apply(name);
  if (!error.empty()) {
    ESP_LOGW(TAG, "Cannot apply preset \"%s\": %s", name.c_str(), error.c_str());
    publish_select();
  }
  return error;
}

inline void attach(esphome::onx_config_web::ConfigWeb *config_web, esphome::select::Select *preset_select,
                   std::function<void()> options_changed) {
  web = config_web;
  selector = preset_select;
  load();
  if (web != nullptr) web->set_internal_result(on_internal_result);
  refresh_select();
  on_options_changed = std::move(options_changed);
}

inline std::string list_json() {
  load();
  return onx_entity_config::build_json([](JsonObject root) {
    root["active"] = active;
    root["applying"] = applying;
    root["remaining"] = web != nullptr && !applying.empty() ? web->internal_pending() : 0;
    root["capacity"] = MAX_PRESETS;
    root["max_name"] = MAX_NAME;
    JsonArray items = root["presets"].to<JsonArray>();
    for (const auto &summary : summaries) {
      auto item = items.add<JsonObject>();
      item["name"] = summary.name;
      item["controls"] = summary.configured;
      item["bytes"] = summary.bytes;
    }
  });
}
}  // namespace onx_presets
