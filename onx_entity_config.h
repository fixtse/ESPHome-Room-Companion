#pragma once
#include <vector>
#include <algorithm>
#include <cstdint>
#include "custom_logic.h"
#include "onx_gesture.h"
#include "onx_cover_presets.h"
#include "onx_detail_view.h"
#include "onx_config_rules.h"
#include "onx_icon_state.h"
#include "onx_display_settings.h"
#include "onx_sensor_view.h"
#include "esphome/components/onx_config_web/onx_config_web.h"
#include "esphome/components/onx_config_web/request_diagnostics.h"

namespace onx_entity_config {
using custom_logic::MAX_HA_ENTITY_ID_LENGTH;
// Web configuration can legitimately exceed ESPHome's generic 5 KB serializer
// limit. Allocate its document in PSRAM, measure first, and return complete JSON
// or an explicit error, never a truncated successful response.
template<typename F> inline std::string build_json(F &&fill) {
  auto doc = custom_logic::make_psram_json_document();
  fill(doc.template to<JsonObject>());
  if (doc.overflowed()) return "{\"error\":\"Not enough memory to build configuration response\"}";
  const size_t size = measureJson(doc);
  if (size > 131072) return "{\"error\":\"Configuration response exceeds size limit\"}";
  // Serialize into the PSRAM response buffer; ConfigWeb sends it without a copy.
  auto &body = esphome::onx_config_web::large_body();
  body.clear();
  body.resize(size + 1);
  if (body.size() != size + 1) {
    esphome::onx_config_web::Body().swap(body);
    return "{\"error\":\"Not enough memory to build configuration response\"}";
  }
  serializeJson(doc, &body[0], size + 1);
  body.resize(size);
  return esphome::onx_config_web::LARGE_BODY_MARKER;
}

inline esphome::ESPPreferenceObject behavior_preference;
inline esphome::ESPPreferenceObject hold_cue_preference;
inline esphome::ESPPreferenceObject assist_mute_media_preference;
inline esphome::ESPPreferenceObject display_preference, background_preference;
inline esphome::ESPPreferenceObject orientation_preference;
inline esphome::ESPPreferenceObject idle_preference;
inline esphome::ESPPreferenceObject datetime_preference;
inline bool display_settings_loaded{false};
inline void load_display_settings() {
  if (display_settings_loaded || esphome::global_preferences == nullptr) return;
  display_preference = esphome::global_preferences->make_preference<onx_display_settings::Settings>(esphome::fnv1_hash("onx.display.v1"));
  onx_display_settings::Settings saved{};
  if (display_preference.load(&saved) && saved.magic == 0x4F4E5844 && saved.media_color <= 0xFFFFFF && saved.standby_scope <= 2)
    onx_display_settings::current = saved;
  idle_preference=esphome::global_preferences->make_preference<onx_display_settings::IdleSettings>(esphome::fnv1_hash("onx.idle_screen.v1"));
  onx_display_settings::IdleSettings idle_saved{};
  if(idle_preference.load(&idle_saved) && idle_saved.magic==0x4F4E5849 && idle_saved.media<=1)onx_display_settings::idle=idle_saved;
  datetime_preference = esphome::global_preferences->make_preference<onx_display_settings::DateTimeSettings>(esphome::fnv1_hash("onx.datetime.v2"));
  onx_display_settings::DateTimeSettings date_saved{};
  if (datetime_preference.load(&date_saved) && date_saved.magic == 0x4F4E5854 && date_saved.date_format <= 5 && date_saved.hour_format <= 1 && date_saved.show_ampm <= 1)
    onx_display_settings::datetime = date_saved;
  else {
    struct LegacyDate {uint32_t magic;uint8_t date_format,hour_format;};
    auto old=esphome::global_preferences->make_preference<LegacyDate>(esphome::fnv1_hash("onx.datetime.v1"));
    LegacyDate legacy{};
    if(old.load(&legacy) && legacy.magic==0x4F4E5854 && legacy.date_format<=2 && legacy.hour_format<=1) {
      onx_display_settings::datetime.date_format=legacy.date_format;onx_display_settings::datetime.hour_format=legacy.hour_format;
    }
  }
  orientation_preference=esphome::global_preferences->make_preference<onx_display_settings::Orientation>(esphome::fnv1_hash("onx.orientation.v1"));
  onx_display_settings::Orientation orientation_saved{};
  if(orientation_preference.load(&orientation_saved) && orientation_saved.magic==0x4F4E584F && orientation_saved.turns<=3)
    onx_display_settings::orientation=orientation_saved;
  behavior_preference=esphome::global_preferences->make_preference<onx_display_settings::BehaviorSettings>(esphome::fnv1_hash("onx.display_behavior.v1"));
  onx_display_settings::BehaviorSettings notification_saved{};
  if(behavior_preference.load(&notification_saved) && notification_saved.magic==0x4F4E584E && notification_saved.cover_timers<=1 && notification_saved.clock_seconds<=1)
    onx_display_settings::behavior=notification_saved;
  hold_cue_preference=esphome::global_preferences->make_preference<onx_display_settings::HoldCue>(esphome::fnv1_hash("onx.hold_cue.v1"));
  onx_display_settings::HoldCue hold_saved{};
  if(hold_cue_preference.load(&hold_saved) && hold_saved.magic==0x4F4E5848 && hold_saved.show<=1)
    onx_display_settings::hold_cue=hold_saved;
  assist_mute_media_preference=esphome::global_preferences->make_preference<onx_display_settings::AssistMuteMedia>(esphome::fnv1_hash("onx.assist_mute_media.v1"));
  onx_display_settings::AssistMuteMedia assist_mute_saved{};
  if(assist_mute_media_preference.load(&assist_mute_saved) && assist_mute_saved.magic==0x4F4E5841 && assist_mute_saved.enabled<=1)
    onx_display_settings::assist_mute_media=assist_mute_saved;
  background_preference=esphome::global_preferences->make_preference<onx_display_settings::BackgroundSettings>(esphome::fnv1_hash("onx.background.v1"));
  onx_display_settings::BackgroundSettings background_saved{};
  if(background_preference.load(&background_saved) && background_saved.magic==0x4F4E5842 && background_saved.color<=0xFFFFFF)
    onx_display_settings::background_settings=background_saved;
  display_settings_loaded = true;
}
inline bool save_display_settings(const std::string &color, const std::string &scope, const std::string &date_format, const std::string &hour_format, bool show_ampm,const std::string &idle_view="",const std::string &orientation="",const std::string &notifications_cover_timers="",const std::string &clock_seconds_ring="",const std::string &background_color="") {
  load_display_settings();
  auto background_next=onx_display_settings::background_settings;
  if(!background_color.empty() && !onx_display_settings::parse_color(background_color,background_next.color))return false;
  auto notification_next=onx_display_settings::behavior;
  if(!notifications_cover_timers.empty()) {
    if(notifications_cover_timers!="0" && notifications_cover_timers!="1")return false;
    notification_next.cover_timers=notifications_cover_timers=="1";
  }
  if(!clock_seconds_ring.empty()) {
    if(clock_seconds_ring!="0" && clock_seconds_ring!="1")return false;
    notification_next.clock_seconds=clock_seconds_ring=="1";
  }
  auto orientation_next=onx_display_settings::orientation;
  if(!orientation.empty() && !onx_display_settings::parse_orientation(orientation,orientation_next.turns))return false;
  auto idle_next=onx_display_settings::idle;
  if(!idle_view.empty()) {
    if(idle_view!="clock" && idle_view!="media")return false;
    idle_next.media=idle_view=="media";
  }
  onx_display_settings::Settings next{};
  if (!display_settings_loaded || !onx_display_settings::parse_color(color, next.media_color) ||
      !onx_display_settings::parse_scope(scope, next.standby_scope)) return false;
  onx_display_settings::DateTimeSettings date_next{};
  if (!onx_display_settings::parse_date_format(date_format, date_next.date_format) ||
      !onx_display_settings::parse_hour_format(hour_format, date_next.hour_format)) return false;
  date_next.show_ampm = show_ampm ? 1 : 0;
  if (background_next.color==onx_display_settings::background_settings.color && next.media_color == onx_display_settings::current.media_color && next.standby_scope == onx_display_settings::current.standby_scope &&
      date_next.date_format == onx_display_settings::datetime.date_format && date_next.hour_format == onx_display_settings::datetime.hour_format && date_next.show_ampm == onx_display_settings::datetime.show_ampm && idle_next.media==onx_display_settings::idle.media && orientation_next.turns==onx_display_settings::orientation.turns && notification_next.cover_timers==onx_display_settings::behavior.cover_timers && notification_next.clock_seconds==onx_display_settings::behavior.clock_seconds) return true;
  if (!background_preference.save(&background_next) || !behavior_preference.save(&notification_next) || !orientation_preference.save(&orientation_next) || !idle_preference.save(&idle_next) || !datetime_preference.save(&date_next) || !display_preference.save(&next) || !esphome::global_preferences->sync()) return false;
  onx_display_settings::background_settings=background_next;
  onx_display_settings::behavior=notification_next;
  onx_display_settings::current = next; onx_display_settings::datetime = date_next; onx_display_settings::idle=idle_next; onx_display_settings::orientation=orientation_next; return true;
}

inline bool save_background_color(const std::string &value) {
  load_display_settings();
  auto next=onx_display_settings::background_settings;
  if(!display_settings_loaded||!onx_display_settings::parse_color(value,next.color))return false;
  if(next.color==onx_display_settings::background_settings.color)return true;
  if(!background_preference.save(&next)||!esphome::global_preferences->sync())return false;
  onx_display_settings::background_settings=next;return true;
}

inline bool save_hold_cue(const std::string &value) {
  if(value.empty())return true;
  if(value!="0" && value!="1")return false;
  load_display_settings();
  auto next=onx_display_settings::hold_cue;next.show=value=="1";
  if(next.show==onx_display_settings::hold_cue.show)return true;
  if(!hold_cue_preference.save(&next) || !esphome::global_preferences->sync())return false;
  onx_display_settings::hold_cue=next;return true;
}

inline bool save_assist_mute_media(const std::string &value) {
  if(value.empty())return true;
  if(value!="0" && value!="1")return false;
  load_display_settings();
  auto next=onx_display_settings::assist_mute_media;next.enabled=value=="1";
  if(next.enabled==onx_display_settings::assist_mute_media.enabled)return true;
  if(!assist_mute_media_preference.save(&next) || !esphome::global_preferences->sync())return false;
  onx_display_settings::assist_mute_media=next;return true;
}

// Keep subscriptions and compact runtime indices intact; hide only carousel entries.
struct TimerVisibility { char entity[129]{}; bool hidden{false}; };
inline onx_memory::PagedArray<TimerVisibility,onx_memory::ENTRY_COUNT> timer_visibility{};
inline onx_memory::PagedArray<esphome::ESPPreferenceObject,onx_memory::ENTRY_COUNT> timer_visibility_preferences{};
inline size_t timer_visibility_loaded_limit=0;
inline void load_timer_visibility_slot(size_t i) {
  if(esphome::global_preferences==nullptr || i>=onx_memory::ENTRY_COUNT)return;
  timer_visibility_preferences[i]=esphome::global_preferences->make_preference<TimerVisibility>(
      esphome::fnv1_hash("onx.timer.visibility.v1."+std::to_string(i)));
  TimerVisibility saved{};
  if(timer_visibility_preferences[i].load(&saved)) {
    saved.entity[128]=0;
    if(onx_config::valid_entity(saved.entity))timer_visibility[i]=saved;
  }
}
inline void load_timer_visibility() {
  if(esphome::global_preferences==nullptr)return;
  const size_t limit=custom_logic::active_slot_limit();
  for(size_t i=timer_visibility_loaded_limit;i<limit;++i)load_timer_visibility_slot(i);
  if(limit>timer_visibility_loaded_limit)timer_visibility_loaded_limit=limit;
}
inline bool timer_hidden(size_t slot,const std::string &entity) {
  load_timer_visibility();
  if(slot>=onx_memory::ENTRY_COUNT || entity.empty())return false;
  const auto *tv=timer_visibility.find(slot);
  return tv!=nullptr && tv->hidden && entity==tv->entity;
}
inline bool save_timer_visibility(size_t slot,const std::string &entity,bool hidden) {
  load_timer_visibility();
  if(esphome::global_preferences==nullptr || slot>=onx_memory::ENTRY_COUNT)return false;
  if(slot>=timer_visibility_loaded_limit) {
    timer_visibility_preferences[slot]=esphome::global_preferences->make_preference<TimerVisibility>(
        esphome::fnv1_hash("onx.timer.visibility.v1."+std::to_string(slot)));
  }
  const auto *existing=timer_visibility.find(slot);
  if(!hidden && (existing==nullptr || !existing->hidden))return true;
  TimerVisibility next{};
  snprintf(next.entity,sizeof(next.entity),"%s",entity.c_str());next.hidden=hidden;
  if(!timer_visibility_preferences[slot].save(&next)||!esphome::global_preferences->sync())return false;
  timer_visibility[slot]=next;
  if(!entity.empty())custom_logic::note_slot_used(slot);
  return true;
}
inline bool timer_hidden_ui(int ui_slot) {
  const size_t limit=custom_logic::active_slot_limit();
  for(size_t slot=0;slot<limit;++slot)
    if(custom_logic::normal_ui_slot_for_device_config_slot(slot)==static_cast<size_t>(ui_slot))
      return timer_hidden(slot,custom_logic::cached_device_entity_id(slot));
  return false;
}
// Carousel stops are pages (onx_panel_pages.h installs page_order): the UI
// slot of each page's first entity, in page order. Other entries are only
// reachable from their page. Without pages, every non-hidden entry is a stop.
inline std::vector<int> (*page_order)()=nullptr;
inline std::vector<int> carousel_order(int total) {
  std::vector<int> out;
  if(page_order) {for(int i:page_order())if(i>=0 && i<total)out.push_back(i);return out;}
  for(int i=0;i<total;++i)if(!timer_hidden_ui(i))out.push_back(i);
  return out;
}
inline bool carousel_hidden(int ui_slot) {
  const auto order=carousel_order(INT32_MAX);
  return std::find(order.begin(),order.end(),ui_slot)==order.end();
}
inline int carousel_selection(int selected,int total) {
  if(selected==total)return total;
  const auto order=carousel_order(total);
  if(std::find(order.begin(),order.end(),selected)!=order.end())return selected;
  return order.empty()?total:order.front();
}
inline int carousel_step(int selected,int total,int direction) {
  return onx_config::step_carousel(carousel_order(total),selected,total,direction);
}
inline std::vector<int> carousel_entries(int total) {
  auto entries=carousel_order(total);
  entries.push_back(total);  // Settings is always reachable.
  return entries;
}
// A missing neighbour is -1; the settings gear remains the final landmark.
inline int carousel_neighbour(int selected,int total,int direction) {
  const auto order=carousel_order(total);
  if(selected==total)return direction<0 && !order.empty()?order.back():-1;
  const auto at=std::find(order.begin(),order.end(),selected);
  if(at==order.end())return -1;
  const int next=int(at-order.begin())+direction;
  return next<0?-1:next>=int(order.size())?total:order[next];
}
inline int carousel_position(int index) {
  const auto order=carousel_order(INT32_MAX);
  const auto at=std::find(order.begin(),order.end(),index);
  return at==order.end()?int(order.size()):int(at-order.begin());
}

inline size_t sensor_configs_loaded_limit=0;
inline onx_memory::PagedArray<esphome::ESPPreferenceObject,onx_memory::ENTRY_COUNT> sensor_preferences{};
inline void load_sensor_config_slot(size_t i) {
  if(esphome::global_preferences==nullptr || i>=onx_memory::ENTRY_COUNT)return;
  const auto key="onx.sensor.v1."+std::to_string(i);
  sensor_preferences[i]=esphome::global_preferences->make_preference<onx_sensor_view::Config>(esphome::fnv1_hash(key.c_str()));
  onx_sensor_view::Config saved{};
  if(sensor_preferences[i].load(&saved)) {
    saved.primary[128]=saved.secondary[128]=saved.text[160]=0;
    if((!saved.primary[0]||onx_config::valid_entity(saved.primary)) && (!saved.secondary[0]||onx_config::valid_entity(saved.secondary)) && saved.graph<=onx_sensor_view::GRAPH_MAX) {
      if(std::string(saved.primary).rfind("switch.",0)==0 && saved.secondary[0])saved.graph|=onx_sensor_view::GRAPH_ON;
      onx_sensor_view::configs[i]=saved;
      onx_sensor_view::states[i].bind(onx_sensor_view::configs[i]);
    }
  }
}
inline void load_sensor_configs() {
  if(esphome::global_preferences==nullptr)return;
  const size_t limit=custom_logic::active_slot_limit();
  for(size_t i=sensor_configs_loaded_limit;i<limit;++i)load_sensor_config_slot(i);
  if(limit>sensor_configs_loaded_limit)sensor_configs_loaded_limit=limit;
}
inline bool save_sensor_config(size_t i,const std::string &primary,const std::string &secondary,const std::string &text,uint8_t graph) {
  load_sensor_configs();if(esphome::global_preferences==nullptr||i>=onx_memory::ENTRY_COUNT)return false;
  uint8_t eff_graph=graph&onx_sensor_view::GRAPH_MAX;
  if(!(eff_graph&onx_sensor_view::GRAPH_ON))eff_graph=0;
  if(secondary.empty())eff_graph&=~onx_sensor_view::GRAPH_SECONDARY;
  if(primary.rfind("switch.",0)==0)eff_graph=secondary.empty()?0:(eff_graph|onx_sensor_view::GRAPH_ON|onx_sensor_view::GRAPH_SECONDARY);
  const auto *existing=onx_sensor_view::configs.find(i);
  const std::string norm_text=text.empty()?"{value}":text;
  if(secondary.empty() && !eff_graph && norm_text=="{value}" &&
     (existing==nullptr || (!existing->secondary[0] && !existing->graph)))return true;
  if(i>=sensor_configs_loaded_limit) {
    const auto key="onx.sensor.v1."+std::to_string(i);
    sensor_preferences[i]=esphome::global_preferences->make_preference<onx_sensor_view::Config>(esphome::fnv1_hash(key.c_str()));
  }
  onx_sensor_view::Config next{};
  snprintf(next.primary,sizeof(next.primary),"%s",primary.c_str());snprintf(next.secondary,sizeof(next.secondary),"%s",secondary.c_str());
  snprintf(next.text,sizeof(next.text),"%s",norm_text.c_str());next.graph=eff_graph;
  if(!sensor_preferences[i].save(&next)||!esphome::global_preferences->sync())return false;
  onx_sensor_view::configs[i]=next;onx_sensor_view::states[i].bind(next);
  if(!primary.empty())custom_logic::note_slot_used(i);
  return true;
}

struct CoverPresetEntry { std::string entity; onx_cover_presets::Config config; };
struct StoredCoverPresets { uint32_t magic{0x4F4E5843}; char entity[129]{}; onx_cover_presets::Config config; };
inline std::vector<CoverPresetEntry> cover_preset_cache;
inline onx_cover_presets::Config cover_presets(const std::string &entity) {
  for (const auto &entry : cover_preset_cache) if (entry.entity == entity) return entry.config;
  auto config = entity.rfind("cover.",0) == 0 ? onx_cover_presets::defaults() : onx_cover_presets::Config{};
  if (entity.rfind("cover.",0) != 0 || !esphome::global_preferences) return config;
  auto pref = esphome::global_preferences->make_preference<StoredCoverPresets>(esphome::fnv1_hash("onx.cover.presets.v1." + entity));
  StoredCoverPresets stored;
  if (pref.load(&stored) && stored.magic == 0x4F4E5843 && stored.config.count <= onx_cover_presets::MAX) {
    stored.entity[128] = 0;
    onx_cover_presets::Config checked;
    if (entity == stored.entity && onx_cover_presets::parse(onx_cover_presets::encode(stored.config), checked)) { config = checked; config.enabled = stored.config.enabled; }
  }
  cover_preset_cache.push_back({entity,config});
  return config;
}
inline bool save_cover_presets(const std::string &entity, const onx_cover_presets::Config &config) {
  if (entity.rfind("cover.",0) != 0) return config.count == 0;
  if (!esphome::global_preferences) return false;
  StoredCoverPresets stored;
  snprintf(stored.entity,sizeof(stored.entity),"%s",entity.c_str()); stored.config = config;
  auto pref = esphome::global_preferences->make_preference<StoredCoverPresets>(esphome::fnv1_hash("onx.cover.presets.v1." + entity));
  if (!pref.save(&stored) || !esphome::global_preferences->sync()) return false;
  for (auto &entry : cover_preset_cache) if (entry.entity == entity) { entry.config = config; return true; }
  cover_preset_cache.push_back({entity,config}); return true;
}

inline size_t playlists_loaded_limit=0;
inline onx_memory::PagedArray<esphome::ESPPreferenceObject,onx_memory::ENTRY_COUNT> playlist_preferences{};
inline void load_playlists() {
  if(esphome::global_preferences==nullptr) return;
  const size_t limit=custom_logic::active_slot_limit();
  for(size_t i=playlists_loaded_limit;i<limit;++i) {
    playlist_preferences[i]=esphome::global_preferences->make_preference<onx_playlist::Config>(esphome::fnv1_hash("onx.playlists.v1."+std::to_string(i)));
    onx_playlist::Config saved{};
    if(!playlist_preferences[i].load(&saved) || saved.magic!=0x4F4E5851 || saved.count>onx_playlist::MAX_ENTRIES) continue;
    saved.entity[128]=0;
    bool valid=onx_config::valid_entity(saved.entity);
    for(auto &entry:saved.entries) {
      entry.name[48]=entry.action[128]=0;
    }
    for(size_t j=0;j<saved.count;++j) valid=valid && saved.entries[j].name[0] && onx_playlist::action_valid(saved.entries[j].action);
    if(valid) onx_playlist::configs[i]=saved;
  }
  if(limit>playlists_loaded_limit)playlists_loaded_limit=limit;
}
inline bool parse_playlists(const std::string &entity,const std::string &raw,onx_playlist::Config &next) {
  if(raw.empty()) return true; // Old clients clear the list when saving a slot.
  auto doc=custom_logic::make_psram_json_document();
  if(deserializeJson(doc,raw) || !doc.is<JsonArray>() || doc.size()>onx_playlist::MAX_ENTRIES) return false;
  custom_logic::copy_string_to_fixed_buffer(entity,next.entity);
  for(JsonObject entry:doc.as<JsonArray>()) {
    if(!entry["name"].is<std::string>() || !entry["action"].is<std::string>()) return false;
    const auto name=entry["name"].as<std::string>(),action=entry["action"].as<std::string>();
    if(name.empty() || name.size()>48 || name.find_first_of("\r\n")!=std::string::npos || !onx_playlist::action_valid(action)) return false;
    auto &target=next.entries[next.count++];
    custom_logic::copy_string_to_fixed_buffer(name,target.name);
    custom_logic::copy_string_to_fixed_buffer(action,target.action);
  }
  return true;
}
inline bool save_playlists(size_t slot,const onx_playlist::Config &next) {
  load_playlists();
  if(esphome::global_preferences==nullptr || slot>=onx_memory::ENTRY_COUNT) return false;
  const auto *existing=onx_playlist::configs.find(slot);
  if(next.count==0 && (existing==nullptr || existing->count==0)) return true;
  if(slot>=playlists_loaded_limit) {
    playlist_preferences[slot]=esphome::global_preferences->make_preference<onx_playlist::Config>(esphome::fnv1_hash("onx.playlists.v1."+std::to_string(slot)));
  }
  if(!playlist_preferences[slot].save(&next) || !esphome::global_preferences->sync()) return false;
  onx_playlist::configs[slot]=next;
  if(next.entity[0])custom_logic::note_slot_used(slot);
  onx_playlist::browser.clear();
  return true;
}
inline const onx_playlist::Config *playlists_for_ui(int ui_slot,const std::string &entity) {
  load_playlists();
  const size_t limit=std::min(custom_logic::active_slot_limit(),onx_playlist::configs.allocated_limit());
  for(size_t i=0;i<limit;++i) {
    const auto *config=onx_playlist::configs.find(i);
    if(config && config->count && entity==config->entity &&
       custom_logic::normal_ui_slot_for_device_config_slot(i)==static_cast<size_t>(ui_slot)) return config;
  }
  return nullptr;
}
inline size_t gestures_loaded_limit=0;
inline onx_memory::PagedArray<esphome::ESPPreferenceObject,onx_memory::ENTRY_COUNT> gesture_preferences{};
inline void load_gestures() {
  if(esphome::global_preferences==nullptr)return;
  const size_t limit=custom_logic::active_slot_limit();
  for(size_t i=gestures_loaded_limit;i<limit;++i) {
    gesture_preferences[i]=esphome::global_preferences->make_preference<onx_gesture::Config>(esphome::fnv1_hash("onx.gestures.v2."+std::to_string(i)));
    onx_gesture::Config saved{};
    if(!gesture_preferences[i].load(&saved)) {
      if(i>=40)continue;
      auto legacy_preference=esphome::global_preferences->make_preference<onx_gesture::LegacyConfig>(esphome::fnv1_hash("onx.gestures.v1."+std::to_string(i)));
      onx_gesture::LegacyConfig legacy{};
      if(!legacy_preference.load(&legacy))continue;
      static_cast<onx_gesture::LegacyConfig &>(saved)=legacy;
    }
    if(saved.magic!=0x4F4E5847)continue;
    saved.entity[128]=saved.feedback[128]=0;
    bool valid=(onx_config::valid_entity(saved.entity) || custom_logic::is_local_hmi_gpio_switch_preset_option(saved.entity)) && (!saved.feedback[0] || onx_config::valid_entity(saved.feedback)) &&
        saved.press_responses<=1 && saved.popup<=1 && saved.responses<=1 && saved.events<=1 && saved.step>=1 && saved.step<=100;
    for(auto &action:saved.actions) {action[128]=0;valid=valid && onx_config::valid_action(action);}
    if(valid)onx_gesture::configs[i]=saved;
  }
  if(limit>gestures_loaded_limit)gestures_loaded_limit=limit;
}
inline bool parse_gestures(const std::string &entity,const std::string &raw,onx_gesture::Config &next) {
  snprintf(next.entity,sizeof(next.entity),"%s",entity.c_str());
  if(raw.empty())return true;
  auto doc=custom_logic::make_psram_json_document();
  if(deserializeJson(doc,raw) || !doc.is<JsonObject>())return false;
  const char *keys[]={"cw","ccw","holdCw","holdCcw"};
  for(size_t i=0;i<4;++i) {
    const std::string action=doc[keys[i]] | "";
    if(!onx_config::valid_action(action))return false;
    snprintf(next.actions[i],sizeof(next.actions[i]),"%s",action.c_str());
  }
  const std::string feedback=doc["feedback"] | "";
  if(!feedback.empty() && !onx_config::valid_entity(feedback))return false;
  snprintf(next.feedback,sizeof(next.feedback),"%s",feedback.c_str());
  for(const auto key:{"popup","responses","events","pressResponses"}) {
    const std::string value=doc[key] | (std::string(key)=="popup"?"1":"0");
    if(value!="0" && value!="1")return false;
    if(std::string(key)=="popup")next.popup=value=="1";
    if(std::string(key)=="responses")next.responses=value=="1";
    if(std::string(key)=="pressResponses")next.press_responses=value=="1";
    if(std::string(key)=="events")next.events=value=="1";
  }
  unsigned step=1;
  const std::string step_text=doc["step"] | "1";
  if(!onx_config::integer_in_range(step_text,1,100,step))return false;
  next.step=step;return true;
}
inline bool save_gestures(size_t slot,const onx_gesture::Config &next) {
  load_gestures();if(esphome::global_preferences==nullptr || slot>=onx_memory::ENTRY_COUNT)return false;
  const bool is_default=!next.actions[0][0] && !next.actions[1][0] && !next.actions[2][0] && !next.actions[3][0] &&
      !next.feedback[0] && next.popup==1 && next.responses==0 && next.press_responses==0 && next.events==0 && next.step==1;
  const auto *existing=onx_gesture::configs.find(slot);
  const bool existing_default=existing==nullptr || (!existing->actions[0][0] && !existing->actions[1][0] && !existing->actions[2][0] &&
      !existing->actions[3][0] && !existing->feedback[0] && existing->popup==1 && existing->responses==0 &&
      existing->press_responses==0 && existing->events==0 && existing->step==1);
  if(is_default && existing_default)return true;
  if(slot>=gestures_loaded_limit) {
    gesture_preferences[slot]=esphome::global_preferences->make_preference<onx_gesture::Config>(esphome::fnv1_hash("onx.gestures.v2."+std::to_string(slot)));
  }
  if(!gesture_preferences[slot].save(&next) || !esphome::global_preferences->sync())return false;
  const bool same=std::string(next.entity)==onx_gesture::configs[slot].entity && std::string(next.feedback)==onx_gesture::configs[slot].feedback;
  const uint32_t next_token=onx_gesture::states[slot].token+1;
  onx_gesture::configs[slot]=next;
  if(!same)onx_gesture::states[slot]={};
  onx_gesture::states[slot].token=next_token;onx_gesture::states[slot].feedback_ms=0;
  if(next.entity[0])custom_logic::note_slot_used(slot);
  onx_gesture::clear_pending();return true;
}
inline int gesture_slot_for_ui(int ui_slot) {
  load_gestures();
  const size_t limit=std::min(custom_logic::active_slot_limit(),onx_gesture::configs.allocated_limit());
  for(size_t i=0;i<limit;++i) {
    const auto *cfg=onx_gesture::configs.find(i);
    if(cfg && cfg->entity[0] && custom_logic::cached_device_entity_id(i)==cfg->entity &&
       custom_logic::normal_ui_slot_for_device_config_slot(i)==static_cast<size_t>(ui_slot))return int(i);
  }
  return -1;
}

struct PressBinding {
  char source[MAX_HA_ENTITY_ID_LENGTH + 1]{};
  char action[MAX_HA_ENTITY_ID_LENGTH + 1]{};
};
struct StoredPressBindingSlot {
  uint32_t magic{0x4F4E5850};
  PressBinding binding{};
};
static onx_memory::PagedArray<PressBinding, onx_memory::ENTRY_COUNT> bindings{};
static onx_memory::PagedArray<esphome::ESPPreferenceObject, onx_memory::ENTRY_COUNT> binding_preferences{};
static size_t bindings_loaded_limit = 0;
static bool legacy_bindings_checked = false;
inline void load() {
  if (esphome::global_preferences == nullptr) return;
  if (!legacy_bindings_checked) {
    legacy_bindings_checked = true;
    // Migrate legacy monolithic press binding tables (v3: 40, v2: 20, v1: 10) into RAM;
    // per-slot v4 keys below override any slot that was saved after upgrade.
    struct V3Bindings { uint32_t magic; PressBinding slots[40]; };
    auto v3_pref = esphome::global_preferences->make_preference<V3Bindings>(esphome::fnv1_hash("onx.press_bindings.v3"));
    auto v3_buf = custom_logic::make_ram_allocated<V3Bindings>();
    size_t legacy_count = 0;
    if (v3_buf && v3_pref.load(v3_buf.get()) && v3_buf->magic == 0x4F4E5850) {
      legacy_count = 40;
    } else {
      struct V2Bindings { uint32_t magic; PressBinding slots[20]; };
      auto v2_pref = esphome::global_preferences->make_preference<V2Bindings>(esphome::fnv1_hash("onx.press_bindings.v2"));
      auto v2_buf = custom_logic::make_ram_allocated<V2Bindings>();
      if (v3_buf && v2_buf && v2_pref.load(v2_buf.get()) && v2_buf->magic == 0x4F4E5850) {
        std::copy(std::begin(v2_buf->slots), std::end(v2_buf->slots), std::begin(v3_buf->slots));
        legacy_count = 20;
      } else {
        struct LegacyBindings { uint32_t magic; PressBinding slots[10]; };
        auto v1_pref = esphome::global_preferences->make_preference<LegacyBindings>(esphome::fnv1_hash("onx.press_bindings.v1"));
        auto v1_buf = custom_logic::make_ram_allocated<LegacyBindings>();
        if (v3_buf && v1_buf && v1_pref.load(v1_buf.get()) && v1_buf->magic == 0x4F4E5850) {
          std::copy(std::begin(v1_buf->slots), std::end(v1_buf->slots), std::begin(v3_buf->slots));
          legacy_count = 10;
        }
      }
    }
    if (v3_buf && legacy_count > 0) {
      for (size_t i = 0; i < legacy_count; ++i) {
        auto &s = v3_buf->slots[i];
        s.source[MAX_HA_ENTITY_ID_LENGTH] = '\0'; s.action[MAX_HA_ENTITY_ID_LENGTH] = '\0';
        if (s.action[0] && (onx_config::valid_entity(s.source) || custom_logic::is_local_hmi_gpio_switch_preset_option(s.source)) &&
            onx_config::valid_press_action(s.source, s.action)) {
          bindings[i] = s;
        }
      }
    }
  }
  const size_t limit = custom_logic::active_slot_limit();
  for (size_t i = bindings_loaded_limit; i < limit; ++i) {
    binding_preferences[i] = esphome::global_preferences->make_preference<StoredPressBindingSlot>(
        esphome::fnv1_hash("onx.press.v4." + std::to_string(i)));
    StoredPressBindingSlot stored{};
    if (binding_preferences[i].load(&stored) && stored.magic == 0x4F4E5850) {
      stored.binding.source[MAX_HA_ENTITY_ID_LENGTH] = '\0';
      stored.binding.action[MAX_HA_ENTITY_ID_LENGTH] = '\0';
      if ((!stored.binding.source[0] || onx_config::valid_entity(stored.binding.source) ||
           custom_logic::is_local_hmi_gpio_switch_preset_option(stored.binding.source)) &&
          onx_config::valid_press_action(stored.binding.source, stored.binding.action)) {
        bindings[i] = stored.binding;
      } else {
        bindings[i] = {};
      }
    }
  }
  if (limit > bindings_loaded_limit) bindings_loaded_limit = limit;
}
inline std::string action_for_slot(size_t slot) {
  load();
  if (slot >= onx_memory::ENTRY_COUNT) return "";
  const auto *b = bindings.find(slot);
  if (b == nullptr || !b->action[0]) return "";
  custom_logic::load_device_config_if_needed(slot);
  return onx_config::binding_matches(b->source, custom_logic::cached_device_entity_id(slot)) ? b->action : "";
}
inline std::string action_for_ui(int ui_slot) {
  if (ui_slot < 0 || ui_slot >= int(onx_memory::ENTRY_COUNT)) return "";
  const size_t limit = custom_logic::active_slot_limit();
  for (size_t slot = 0; slot < limit; ++slot)
    if (custom_logic::normal_ui_slot_for_device_config_slot(slot) == static_cast<size_t>(ui_slot)) return action_for_slot(slot);
  return "";
}
inline bool save_action(size_t slot, const std::string &entity, const std::string &action) {
  load();
  if (esphome::global_preferences == nullptr || slot >= onx_memory::ENTRY_COUNT || !onx_config::valid_press_action(entity, action)) return false;
  const auto *existing = bindings.find(slot);
  if (action.empty() && (existing == nullptr || !existing->action[0])) return true;
  if (existing != nullptr && entity == existing->source && action == existing->action) return true;
  if (slot >= bindings_loaded_limit) {
    binding_preferences[slot] = esphome::global_preferences->make_preference<StoredPressBindingSlot>(
        esphome::fnv1_hash("onx.press.v4." + std::to_string(slot)));
  }
  StoredPressBindingSlot stored{};
  custom_logic::copy_string_to_fixed_buffer(entity, stored.binding.source);
  custom_logic::copy_string_to_fixed_buffer(action, stored.binding.action);
  if (!binding_preferences[slot].save(&stored) || !esphome::global_preferences->sync()) return false;
  bindings[slot] = stored.binding;
  if (!entity.empty()) custom_logic::note_slot_used(slot);
  return true;
}
inline void customize_control_view(int ui_slot,const std::string &entity,onx_detail_view::View &view) {
  const auto press=action_for_ui(ui_slot);
  if(entity.rfind("switch.",0)==0 && view.layout=="graph" && !press.empty())view.name.text=press=="none"?"Press: no action":"Press: custom action";
  if(entity.rfind("climate.",0)==0 && !press.empty())
    view.hint.text=press=="none"?"Press: no action":"Press: custom action";
  const int slot=gesture_slot_for_ui(ui_slot);
  if(slot<0)return;
  const auto &config=onx_gesture::configs[slot];
  if((entity.rfind("sensor.",0)==0 || entity.rfind("script.",0)==0 || entity.rfind("automation.",0)==0) &&
      config.feedback[0] && onx_gesture::states[slot].received) {
    const auto &state=onx_gesture::states[slot];
    view.value.text=state.value+(state.unit.empty()?"":" "+state.unit);
    view.value_ring=-1;
    if(state.unit=="%")onx_detail_view::set_value_ring(view,state.value,0,100);
  }
}
inline std::string error(const char *message) {
  return build_json([&](JsonObject root) { root["error"] = message; });
}
struct StoredIcon {
  uint32_t magic{0x4F4E5849};
  char entity[129]{}, icon[81]{};
  bool registry_override{false};
};
inline size_t icons_loaded_limit{0};
inline onx_memory::PagedArray<esphome::ESPPreferenceObject, onx_memory::ENTRY_COUNT> icon_preferences{};
inline void load_icons() {
  if (esphome::global_preferences == nullptr) return;
  const size_t limit = custom_logic::active_slot_limit();
  for (size_t i = icons_loaded_limit; i < limit; ++i) {
    icon_preferences[i] = esphome::global_preferences->make_preference<StoredIcon>(
        esphome::fnv1_hash("onx.entity_icon.v1." + std::to_string(i)));
    StoredIcon stored;
    if (icon_preferences[i].load(&stored) && stored.magic == 0x4F4E5849) {
      stored.entity[128] = 0; stored.icon[80] = 0;
      if (onx_config::valid_entity(stored.entity) && (stored.icon[0] == 0 || onx_icon_state::valid_icon(stored.icon))) {
        auto &meta = onx_icon_state::metadata[i];
        meta.bind(stored.entity); meta.configured = stored.icon; meta.registry_override = stored.registry_override;
      }
    }
  }
  if (limit > icons_loaded_limit) icons_loaded_limit = limit;
}
inline bool save_icon(size_t slot, const std::string &entity, const std::string &icon, bool override) {
  load_icons();
  if (esphome::global_preferences == nullptr || slot >= onx_memory::ENTRY_COUNT || (!icon.empty() && !onx_icon_state::valid_icon(icon))) return false;
  const auto *existing = onx_icon_state::metadata.find(slot);
  if (icon.empty() && !override && (existing == nullptr || (existing->configured.empty() && !existing->registry_override))) return true;
  if (existing != nullptr && existing->entity == entity && existing->configured == icon && existing->registry_override == override) return true;
  if (slot >= icons_loaded_limit) {
    icon_preferences[slot] = esphome::global_preferences->make_preference<StoredIcon>(
        esphome::fnv1_hash("onx.entity_icon.v1." + std::to_string(slot)));
  }
  StoredIcon stored;
  custom_logic::copy_string_to_fixed_buffer(entity, stored.entity);
  custom_logic::copy_string_to_fixed_buffer(icon, stored.icon);
  stored.registry_override = override;
  if (!icon_preferences[slot].save(&stored) || !esphome::global_preferences->sync()) return false;
  auto &meta = onx_icon_state::metadata[slot];
  meta.bind(entity); meta.configured = icon; meta.registry_override = override;
  if (!entity.empty()) custom_logic::note_slot_used(slot);
  return true;
}
// Highest active slot count to serialize (trimmed of trailing empty slots).
inline size_t serialized_slot_count() {
  const size_t limit = custom_logic::active_slot_limit();
  size_t last = 0;
  for (size_t i = 0; i < limit; ++i) {
    custom_logic::load_device_config_if_needed(i);
    if (!custom_logic::cached_device_entity_id(i).empty()) last = i + 1;
  }
  return std::max<size_t>(1, last);
}
// One slot as the configuration page saves it. Presets store the same shape so
// applying one goes through the regular, validated save path. Empty slots and
// default-empty optional sections are omitted to keep JSON compact at 200+ entries.
inline void encode_slot(size_t i, JsonObject slot) {
  custom_logic::load_device_config_if_needed(i);
  const std::string &entity = custom_logic::cached_device_entity_id(i);
  slot["entity"] = entity;
  if (entity.empty()) return;
  if (const auto *control = onx_gesture::configs.find(i); control != nullptr && entity == control->entity) {
    const bool non_default = control->actions[0][0] || control->actions[1][0] || control->actions[2][0] ||
        control->actions[3][0] || control->feedback[0] || !control->popup || control->responses ||
        control->press_responses || control->events || control->step != 1;
    if (non_default) {
      auto gestures = slot["gestures"].to<JsonObject>();
      const char *gesture_keys[] = {"cw", "ccw", "holdCw", "holdCcw"};
      for (size_t j = 0; j < 4; ++j) gestures[gesture_keys[j]] = control->actions[j];
      gestures["feedback"] = control->feedback;
      gestures["popup"] = control->popup ? "1" : "0";
      gestures["responses"] = control->responses ? "1" : "0";
      gestures["pressResponses"] = control->press_responses ? "1" : "0";
      gestures["events"] = control->events ? "1" : "0";
      gestures["step"] = std::to_string(control->step);
    }
  }
  if (entity.rfind("cover.", 0) == 0) {
    const auto cp = cover_presets(entity);
    slot["coverPresetsEnabled"] = cp.enabled ? "1" : "0";
    slot["coverPresets"] = onx_cover_presets::encode(cp);
  }
  if (timer_hidden(i, entity)) slot["timerHidden"] = "1";
  if (const auto *playlist = onx_playlist::configs.find(i); playlist != nullptr && entity == playlist->entity && playlist->count > 0) {
    JsonArray playlists = slot["playlists"].to<JsonArray>();
    for (size_t j = 0; j < playlist->count; ++j) {
      auto entry = playlists.add<JsonObject>();
      entry["name"] = playlist->entries[j].name;
      entry["action"] = playlist->entries[j].action;
    }
  }
  if (const auto *sensor = onx_sensor_view::configs.find(i); sensor != nullptr && entity == sensor->primary) {
    if (sensor->secondary[0]) {
      slot["secondary"] = sensor->secondary;
      slot["secondaryText"] = sensor->text;
    }
    if (sensor->graph) slot["sensorGraph"] = std::to_string(sensor->graph);
  }
  if (const auto *icon = onx_icon_state::metadata.find(i); icon != nullptr && icon->entity == entity) {
    if (!icon->configured.empty()) slot["icon"] = icon->configured;
    if (icon->registry_override) slot["iconOverride"] = "1";
  }
  const std::string title = custom_logic::ha_entity_title_from_registry_cache(entity);
  if (!title.empty()) slot["name"] = title;
  for (size_t j = 0; j < custom_logic::cached_ha_entities().count; ++j) {
    const auto &entry = custom_logic::cached_ha_entities().entities[j];
    if (entity == entry.entity_id) {
      if (entry.area_name[0]) slot["area"] = entry.area_name;
      break;
    }
  }
  const std::string act = action_for_slot(i);
  if (!act.empty()) {
    slot["action"] = act;
    const std::string act_name = custom_logic::ha_entity_title_from_registry_cache(act);
    if (!act_name.empty()) slot["actionName"] = act_name;
  }
  const std::string &ctype = custom_logic::cached_device_custom_type(i);
  if (!ctype.empty()) slot["type"] = ctype;
  const std::string domain = custom_logic::ha_domain_from_entity_id(entity);
  const std::string type_opt = domain == "light"
      ? custom_logic::light_type_option_from_custom_mask(custom_logic::cached_device_custom_light_capability_mask(i))
      : domain == "cover"
      ? (custom_logic::cached_device_custom_light_capability_mask(i) == 0 ? "Cover (Preset Off)" : "Cover (Preset On)")
      : ctype == "plug" ? "Plug" : "";
  if (!type_opt.empty()) slot["typeOption"] = type_opt;
}
inline std::string state(const std::string &host = "", bool wifi_connected = false, bool ha_connected = false, float wifi_rssi = NAN, const std::string &artwork_error = "") {
  custom_logic::load_ha_entities_if_needed();
  load_icons();
  load_playlists();
  load_gestures();
  return build_json([&](JsonObject root) {
    root["wifi_connected"] = wifi_connected;
    root["ha_connected"] = ha_connected;
    root["wifi_rssi_dbm"] = wifi_rssi;
    root["wifi_disconnects_since_boot"] = onx_runtime::wifi_disconnects;
    root["last_wifi_disconnect_ms"] = onx_runtime::last_wifi_disconnect_ms;
    root["artwork_error"] = artwork_error;
    root["count"] = custom_logic::cached_ha_entities().count;
    std::string url = custom_logic::build_ha_http_state_url(host, "sensor.placeholder");
    const auto path = url.find("/api/states/");
    if (path != std::string::npos) url.resize(path);
    root["ha_url"] = url;
    custom_logic::load_standby_weather_config_if_needed();
    root["weather"] = custom_logic::cached_standby_weather_entity_id;
    root["capacity"] = custom_logic::MAX_HA_ENTITY_COUNT;
#ifdef ESPHOME_PROJECT_VERSION
    root["firmware"] = ESPHOME_PROJECT_VERSION;
#endif
    root["uptime_ms"] = millis();
    root["reset_reason_text"] = onx_runtime::reset_name();
    auto previous=root["last_event_before_boot"].to<JsonObject>();
    previous["available"]=onx_runtime::previous.magic==0x4F4E5852;
    previous["name"]=onx_runtime::previous.name;
    previous["entity"]=onx_runtime::previous.entity;
    if (onx_runtime::previous_panic_details.message[0]) {
      previous["panic_message"] = onx_runtime::previous_panic_details.message;
    }
    using namespace esphome::onx_config_web;
    if (previous_request.magic == REQUEST_DIAGNOSTICS_MAGIC) {
      auto request = previous["http_request"].to<JsonObject>();
      request["route"] = route_name(previous_request.route);
      request["method"] = method_name(previous_request.method);
      request["uptime_ms"] = previous_request.at_ms;
      request["entry_stack_min_free_bytes"] = previous_request.entry_stack_bytes;
      request["completed"] = previous_request.completed;
      if (previous_request.completed) request["exit_stack_min_free_bytes"] = previous_request.exit_stack_bytes;
    }
    previous["uptime_ms"]=onx_runtime::previous.at_ms;
    previous["internal_heap_free"]=onx_runtime::previous.free_heap;
    auto art=root["artwork_stats"].to<JsonObject>();
    art["attempts"]=onx_media::stat_attempts;art["ok"]=onx_media::stat_ok;art["failed"]=onx_media::stat_failed;
    art["timeouts"]=onx_media::stat_timeouts;art["last_ms"]=onx_media::stat_last_ms;art["slowest_ms"]=onx_media::stat_slowest_ms;
    art["fail_streak"]=onx_media::artwork_retry.failures;
    art["last_failure"]=onx_media::stat_last_failure;
    auto fetch=art["last_fetch"].to<JsonObject>();const auto &f=onx_media::last_fetch;
    fetch["host"]=f.host;fetch["https"]=f.https;fetch["stack_psram"]=f.stack_psram;fetch["cached"]=f.cached;fetch["connect_attempts"]=f.attempts;fetch["open_ms"]=f.open_ms;
    fetch["read_ms"]=f.read_ms;fetch["bytes"]=f.bytes;fetch["heap_free_start"]=f.heap_free_start;
    fetch["heap_largest_start"]=f.heap_largest_start;fetch["heap_free_after_open"]=f.heap_free_after_open;
    if (onx_media::refresh_diagnostics) {
      auto cam=root["artwork_debug"].to<JsonObject>();
      cam["main_entity"]=onx_media::main_entity;cam["wait"]=onx_media::artwork_wait_reason;
      cam["seq"]=onx_media::camera_refresh_seq;cam["live_showing"]=onx_media::live_camera_showing;
      cam["live_entity"]=onx_media::live_camera_entity;cam["since_end_ms"]=esphome::millis()-onx_media::last_artwork_end_ms;
      cam["ready"]=onx_media::report_url(onx_media::ready_url);cam["candidate"]=onx_media::report_url(onx_media::candidate_url);
      cam["requested"]=onx_media::report_url(onx_media::requested_url);cam["gates"]=onx_media::refresh_diagnostics();
    }
    root["artwork_status"]=onx_media::downloading?"downloading":!onx_media::ready_url.empty()?"ready":onx_media::artwork_wait_reason.empty()?"not_loaded":onx_media::artwork_wait_reason.c_str();
#ifdef USE_ESP32
    root["internal_heap_free"]=heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    root["internal_heap_minimum"]=heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    root["internal_heap_largest_block"]=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    root["psram_free"]=heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
#ifdef USE_ESP32
    root["reset_reason"] = static_cast<int>(esp_reset_reason());
#endif
    root["max_slots"] = onx_memory::ENTRY_COUNT;
    JsonArray slots = root["slots"].to<JsonArray>();
    const size_t n_slots = serialized_slot_count();
    for (size_t i = 0; i < n_slots; ++i) encode_slot(i, slots.add<JsonObject>());
  });
}
inline bool save_selected_metadata(const esphome::onx_config_web::Query &query) {
  custom_logic::load_ha_entities_if_needed();
  auto next = custom_logic::make_ram_allocated<custom_logic::StoredHaEntities>();
  if (!next) return false;
  next->magic = custom_logic::HA_ENTITIES_MAGIC;
  const auto &old = custom_logic::cached_ha_entities();
  auto add = [&](const std::string &entity) {
    if (!onx_config::valid_entity(entity)) return;
    for (size_t i = 0; i < next->count; ++i) if (entity == next->entities[i].entity_id) return;
    if (next->count >= custom_logic::MAX_HA_ENTITY_COUNT) return;
    auto &item = next->entities[next->count++];
    for (size_t i = 0; i < old.count; ++i) if (entity == old.entities[i].entity_id) item = old.entities[i];
    custom_logic::copy_string_to_fixed_buffer(entity, item.entity_id);
    if (entity == query.entity) {
      custom_logic::copy_string_to_fixed_buffer(query.name, item.friendly_name);
      custom_logic::copy_string_to_fixed_buffer(query.area, item.area_name);
    }
    if (entity == query.action) custom_logic::copy_string_to_fixed_buffer(query.action_name, item.friendly_name);
  };
  const size_t limit = custom_logic::active_slot_limit();
  for (size_t i = 0; i < limit; ++i) {
    custom_logic::load_device_config_if_needed(i);
    add(custom_logic::cached_device_entity_id(i));
    add(action_for_slot(i));
  }
  custom_logic::load_standby_weather_config_if_needed();
  add(custom_logic::cached_standby_weather_entity_id);
  return custom_logic::save_ha_entities(*next);
}
}  // namespace onx_entity_config

namespace onx_entity_config {
inline void sync_select(size_t slot, esphome::select::Select *select) {
  custom_logic::apply_device_config_options(slot, select);
  const auto &entity = custom_logic::cached_device_entity_id(slot);
  const auto &options = select->traits.get_options();
  for (size_t i = 0; i < options.size(); ++i) {
    if (custom_logic::extract_entity_id_from_device1_option(options[i]) == entity) { select->publish_state(i); return; }
  }
  // A manually entered entity can be used even if HA's cached list omitted it.
  esphome::FixedVector<const char *> updated;
  updated.init(options.size() + 1);
  for (const auto *option : options) updated.push_back(option);
  updated.push_back(entity.c_str());
  select->traits.set_options(updated);
  select->publish_state(updated.size() - 1);
}
}  // namespace onx_entity_config
