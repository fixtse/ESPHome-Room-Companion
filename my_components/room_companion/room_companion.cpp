#include "room_companion.h"

#include "esphome/core/log.h"
#include "esphome/components/json/json_util.h"
#include "esphome/components/wifi/wifi_component.h"

// Power state (copied into the build by `esphome: includes:`).
#include "onx_power_state.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

#include "nvs.h"

namespace esphome::room_companion {

static const char *const TAG = "room_panel";
static const char *const NAMESPACE = "rc";
static const char *const KEY = "stream";
static constexpr int CORNER = 56;
static constexpr uint32_t CORNER_HOLD_MS = 1200;
static constexpr uint32_t STREAM_GRACE_MS = 3000;

// ---------------------------------------------------------------- validation
static std::string trim(const std::string &value) {
  size_t a = 0, b = value.size();
  while (a < b && std::isspace(static_cast<unsigned char>(value[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(value[b - 1]))) --b;
  return value.substr(a, b - a);
}
static bool valid_server(const std::string &value) {
  if (value.empty()) return true;
  const auto colon = value.rfind(':');
  if (colon == std::string::npos || colon == 0 || value.size() > 96) return false;
  for (size_t i = 0; i < colon; ++i) {
    const char c = value[i];
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-')) return false;
  }
  const std::string port = value.substr(colon + 1);
  if (port.empty() || port.size() > 5) return false;
  for (char c : port)
    if (c < '0' || c > '9') return false;
  const int number = std::atoi(port.c_str());
  return number > 0 && number <= 65535;
}
static bool valid_url(const std::string &value) {
  if (value.empty()) return true;
  if (value.size() > 256) return false;
  if (value.rfind("http://", 0) != 0 && value.rfind("https://", 0) != 0 && value != "self-test") return false;
  for (char c : value)
    if (static_cast<unsigned char>(c) <= 0x20 || c == '"' || c == '<' || c == '>') return false;
  return true;
}

// ---------------------------------------------------------------- NVS
static bool nvs_read(std::string &out) {
  nvs_handle_t handle;
  if (nvs_open(NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;
  size_t size = 0;
  bool ok = nvs_get_str(handle, KEY, nullptr, &size) == ESP_OK && size > 0;
  if (ok) {
    out.assign(size, '\0');
    ok = nvs_get_str(handle, KEY, &out[0], &size) == ESP_OK;
    if (ok) out.resize(size - 1);
  }
  nvs_close(handle);
  return ok;
}
static bool nvs_write(const std::string &value) {
  nvs_handle_t handle;
  if (nvs_open(NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
  bool ok = nvs_set_str(handle, KEY, value.c_str()) == ESP_OK && nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return ok;
}

// ---------------------------------------------------------------- touch
void CornerListener::update(const touchscreen::TouchPoints_t &points) { parent_->on_points_(points); }
void CornerListener::release() { parent_->on_release_(); }

void RoomPanel::on_points_(const touchscreen::TouchPoints_t &points) {
  if (!stream_owner_) {
    corner_since_ms_.store(0);
    return;
  }
  bool in_corner = false;
  for (const auto &point : points)
    if (point.x < CORNER && point.y < CORNER) in_corner = true;
  if (!in_corner) {
    corner_since_ms_.store(0);
    return;
  }
  const uint32_t now = millis();
  const uint32_t since = corner_since_ms_.load();
  if (since == 0) {
    corner_since_ms_.store(now == 0 ? 1 : now);
  } else if (now - since >= CORNER_HOLD_MS) {
    corner_since_ms_.store(0);
    corner_requested_.store(true);
  }
}
void RoomPanel::on_release_() { corner_since_ms_.store(0); }

// ---------------------------------------------------------------- lifecycle
void RoomPanel::setup() {
  global_panel = this;
  load_();
  apply_(true, settings_);
}

void RoomPanel::dump_config() {
  ESP_LOGCONFIG(TAG, "Room panel: dashboard server %s", settings_.server.empty() ? "(none)" : settings_.server.c_str());
}

void RoomPanel::loop() {
  const uint32_t now = millis();
  const bool wifi_ok = wifi::global_wifi_component != nullptr && wifi::global_wifi_component->is_connected();
  // The stream's tasks, socket and decoder buffers start only once the dashboard is
  // opened; a configured but unused dashboard costs no internal RAM (memory-budget.md).
  if (wifi_ok && dashboard_open_ && stream_ != nullptr && !stream_->is_started() && stream_->has_server())
    stream_->start();
  if (corner_requested_.exchange(false)) close_dashboard();
  update_owner_(now);
}

// ---------------------------------------------------------------- settings
void RoomPanel::load_() {
  std::string json;
  if (!nvs_read(json)) return;
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return;
  const std::string server = doc["server"] | std::string();
  const std::string url = doc["url"] | std::string();
  const float scale = doc["scale"] | 1.0f;
  const std::string side = doc["toolbar_side"] | std::string("right");
  settings_.dock_pin = doc["dock_pin"] | std::string("dashboard");
  if (side == "left" || side == "right") settings_.toolbar_side = side;
  if (valid_server(server)) settings_.server = server;
  if (valid_url(url)) settings_.url = url;
  if (std::isfinite(scale) && scale >= 0.5f && scale <= 4.0f) settings_.scale = scale;
}

bool RoomPanel::save_() { return nvs_write(settings_json_()); }

std::string RoomPanel::settings_json_() const {
  return json::build_json([&](JsonObject root) {
    root["server"] = settings_.server;
    root["url"] = settings_.url;
    root["scale"] = settings_.scale;
    root["dock_pin"] = settings_.dock_pin;
    root["toolbar_side"] = settings_.toolbar_side;
  });
}

void RoomPanel::apply_(bool boot, const StreamSettings &previous) {
  if (stream_ != nullptr) {
    if (boot || !stream_->is_started()) {
      if (!settings_.server.empty()) stream_->set_server(settings_.server);
      stream_->set_scale(settings_.scale);
      boot_server_ = settings_.server;
      boot_scale_ = settings_.scale;
    } else {
      // The WebSocket address and zoom are fixed once the stream runs.
      restart_required_ = settings_.server != boot_server_ || std::fabs(settings_.scale - boot_scale_) > 0.001f;
    }
    if (boot || settings_.url != previous.url) {
      if (settings_.url.empty() || !stream_->open_url(settings_.url)) stream_->set_url(settings_.url);
    }
  }
  publish_();
}

void RoomPanel::publish_() {
  if (url_text_ != nullptr && url_text_->state != settings_.url) url_text_->publish_state(settings_.url);
  if (dashboard_switch_ != nullptr && dashboard_switch_->state != dashboard_open_)
    dashboard_switch_->publish_state(dashboard_open_);
}

void RoomPanel::set_dashboard_url(const std::string &url) {
  const std::string value = trim(url);
  if (!valid_url(value) || value == settings_.url) {
    publish_();
    return;
  }
  const StreamSettings previous = settings_;
  settings_.url = value;
  save_();
  apply_(false, previous);
}

// ---------------------------------------------------------------- dashboard & ownership
void RoomPanel::open_dashboard() {
  if (stream_ == nullptr || settings_.server.empty() || settings_.url.empty()) {
    message_trigger_.trigger("Set the dashboard server and address on the panel's web page (Visit).");
    publish_();
    return;
  }
  dashboard_open_ = true;
  if (!stream_->is_connected()) message_trigger_.trigger("Connecting to the dashboard server...");
  publish_();
  update_owner_(millis());
}

void RoomPanel::close_dashboard() {
  dashboard_open_ = false;
  publish_();
  update_owner_(millis());
}

void RoomPanel::set_overlay(Overlay overlay, bool on) {
  const uint8_t before = overlays_;
  overlays_ = on ? (overlays_ | overlay) : (overlays_ & ~overlay);
  if (before != overlays_) update_owner_(millis());
}

void RoomPanel::update_owner_(uint32_t now) {
  if (stream_ == nullptr || lvgl_ == nullptr) return;
  const bool asleep = onx_power::sleeping || onx_power::waking;
  bool want = dashboard_open_ && !asleep && overlays_ == 0 && stream_->is_started();
  if (want && !stream_->is_connected()) {
    // Keep the last dashboard frame through a short reconnect.
    if (stream_owner_) {
      if (stream_lost_ms_ == 0) stream_lost_ms_ = now == 0 ? 1 : now;
      want = now - stream_lost_ms_ < STREAM_GRACE_MS;
    } else {
      want = false;
    }
  } else {
    stream_lost_ms_ = 0;
  }
  give_display_to_(want);
}

void RoomPanel::give_display_to_(bool stream) {
  if (stream == stream_owner_) return;
  if (stream) {
    lvgl_->set_paused(true, false);
    stream_->set_active(true);
  } else {
    stream_->set_active(false);
    // Low-power sleep keeps LVGL paused itself.
    if (!onx_power::sleeping) lvgl_->set_paused(false, false);
  }
  stream_owner_ = stream;
  ESP_LOGI(TAG, "Display owner: %s", stream ? "dashboard stream" : "native UI");
  owner_trigger_.trigger(stream);
}

std::string RoomPanel::stream_status() const {
  if (stream_ == nullptr || settings_.server.empty()) return "not configured";
  if (!stream_->is_started()) return dashboard_open_ ? "waiting for Wi-Fi" : "starts when opened";
  if (!stream_->is_connected()) return "connecting";
  return stream_owner_ ? "showing" : "ready";
}

// ---------------------------------------------------------------- web
std::string RoomPanel::state_json_() const {
  return json::build_json([&](JsonObject root) {
    root["server"] = settings_.server;
    root["url"] = settings_.url;
    root["scale"] = settings_.scale;
    root["dock_pin"] = settings_.dock_pin;
    root["toolbar_side"] = settings_.toolbar_side;
    root["status"] = stream_status();
    root["restart_required"] = restart_required_;
  });
}

std::string RoomPanel::handle_web(const std::string &op, const std::string &data) {
  if (op == "panel_state") return state_json_();
  if (op != "panel_save") return "";
  JsonDocument doc;
  if (deserializeJson(doc, data) != DeserializationError::Ok) return "{\"error\":\"Invalid dashboard settings\"}";
  StreamSettings next = settings_;
  next.server = trim(doc["server"] | settings_.server);
  next.url = trim(doc["url"] | settings_.url);
  next.dock_pin = doc["dock_pin"] | settings_.dock_pin;
  if (next.dock_pin.size()>128) return "{\"error\":\"Invalid pinned page\"}";
  next.toolbar_side = doc["toolbar_side"] | settings_.toolbar_side;
  if (next.toolbar_side != "left" && next.toolbar_side != "right")
    return "{\"error\":\"Toolbar position must be left or right\"}";
  const float scale = doc["scale"] | settings_.scale;
  if (!valid_server(next.server)) return "{\"error\":\"Server must be host:port, for example 192.168.1.10:8081\"}";
  if (!valid_url(next.url)) return "{\"error\":\"Dashboard address must start with http:// or https://\"}";
  if (!std::isfinite(scale) || scale < 0.5f || scale > 4.0f) return "{\"error\":\"Zoom must be between 0.5 and 4\"}";
  next.scale = std::round(scale * 100.0f) / 100.0f;
  const StreamSettings previous = settings_;
  settings_ = next;
  if (!save_()) return "{\"error\":\"Dashboard settings could not be saved\"}";
  apply_(false, previous);
  return state_json_();
}

}  // namespace esphome::room_companion
