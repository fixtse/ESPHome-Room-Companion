#include "room_companion.h"

#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "esphome/components/json/json_util.h"
#include "esphome/components/network/ip_address.h"
#include "esphome/components/wifi/wifi_component.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "src/misc/cache/instance/lv_image_header_cache.h"

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

static bool valid_camera_server(const std::string &value) {
  return value.empty() || ((value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0) && valid_url(value));
}
static bool valid_camera_source(const std::string &value) {
  return value == "off" || value == "webrtc" || value == "go2rtc";
}
// A camera stream address for go2rtc (rtsp://user:pass@host/...).
static bool valid_stream_url(const std::string &value) {
  if (value.size() > 256) return false;
  if (value.rfind("rtsp://", 0) != 0 && value.rfind("rtsps://", 0) != 0 && value.rfind("rtmp://", 0) != 0) return false;
  for (char c : value)
    if (static_cast<unsigned char>(c) <= 0x20 || c == '"' || c == '<' || c == '>' || c == ',') return false;
  return true;
}
static bool is_page(const std::string &target) {
  return target.rfind("http://", 0) == 0 || target.rfind("https://", 0) == 0;
}
// "host" of http://user@host:port/path.
static std::string url_host(const std::string &url) {
  const auto scheme = url.find("://");
  if (scheme == std::string::npos) return "";
  std::string rest = url.substr(scheme + 3);
  rest = rest.substr(0, rest.find_first_of("/?#"));
  const auto at = rest.rfind('@');
  if (at != std::string::npos) rest = rest.substr(at + 1);
  if (!rest.empty() && rest[0] == '[') return rest.substr(0, rest.find(']') + 1);
  return rest.substr(0, rest.find(':'));
}
static bool valid_speaker_entity(const std::string &value) {
  if (value.empty()) return true;
  if (value.rfind("media_player.", 0) != 0 || value.size() <= 13 || value.size() > 128) return false;
  for (char c : value)
    if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '_' || c == '.'))
      return false;
  return true;
}
static bool stream_name_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.' || c == ':' || c == '@';
}
// "camera.porch=porch_hd, camera.door=http://host/page": calls fn(entity, target)
// for each entry; returns false on a malformed entry.
template<typename F> static bool each_camera_stream(const std::string &list, F fn) {
  size_t start = 0;
  while (start <= list.size()) {
    size_t end = list.find_first_of(",\n", start);
    if (end == std::string::npos) end = list.size();
    const std::string entry = trim(list.substr(start, end - start));
    start = end + 1;
    if (entry.empty()) continue;
    const auto eq = entry.find('=');
    if (eq == std::string::npos) return false;
    const std::string entity = trim(entry.substr(0, eq)), target = trim(entry.substr(eq + 1));
    if (entity.rfind("camera.", 0) != 0 || entity.size() <= 7 || entity.size() > 128 || target.empty()) return false;
    for (char c : entity)
      if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '_' || c == '.'))
        return false;
    if (target.rfind("http://", 0) == 0 || target.rfind("https://", 0) == 0) {
      if (!valid_url(target)) return false;
    } else if (target.find("://") != std::string::npos) {
      if (!valid_stream_url(target)) return false;
    } else {
      if (target.size() > 64) return false;
      for (char c : target)
        if (!stream_name_char(c)) return false;
    }
    if (fn(entity, target)) return true;
  }
  return true;
}
static bool valid_camera_streams(const std::string &value) {
  return value.size() <= 1024 && each_camera_stream(value, [](const std::string &, const std::string &) { return false; });
}
static std::string percent_encode(const std::string &value) {
  static const char hex[] = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : value) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
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
  // Unguessable WebRTC Camera link ids (the integration's embed page needs
  // no sign-in, only a registered link).
  char token[17];
  snprintf(token, sizeof(token), "%08x%08x", (unsigned) random_uint32(), (unsigned) random_uint32());
  link_token_ = token;
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
  update_camera_(wifi_ok);
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
  const float scale = doc["scale"] | settings_.scale;
  const std::string side = doc["toolbar_side"] | std::string("right");
  settings_.dock_pin = doc["dock_pin"] | std::string("dashboard");
  if (side == "left" || side == "right") settings_.toolbar_side = side;
  const std::string camera_server = doc["camera_server"] | std::string();
  const std::string camera_streams = doc["camera_streams"] | std::string();
  if (valid_camera_server(camera_server)) settings_.camera_server = camera_server;
  // Settings from 2026.11.1.20 had only the go2rtc address.
  const std::string camera_source = doc["camera_source"] | std::string(settings_.camera_server.empty() ? "off" : "go2rtc");
  if (valid_camera_source(camera_source)) settings_.camera_source = camera_source;
  const std::string rotation = doc["camera_rotation"] | std::string("90");
  if (rotation == "0" || rotation == "90" || rotation == "270") settings_.camera_rotation = rotation;
  const std::string speaker = doc["speaker_entity"] | std::string();
  if (valid_speaker_entity(speaker)) settings_.speaker_entity = speaker;
  if (valid_camera_streams(camera_streams)) settings_.camera_streams = camera_streams;
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
    root["camera_source"] = settings_.camera_source;
    root["camera_server"] = settings_.camera_server;
    root["camera_streams"] = settings_.camera_streams;
    root["speaker_entity"] = settings_.speaker_entity;
    root["camera_rotation"] = settings_.camera_rotation;
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
    // A live camera keeps the stream; the new address loads when it stops.
    if ((boot || settings_.url != previous.url) && !camera_on_) {
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
    message_trigger_.trigger("Set the dashboard server and address on the panel's web page (Visit).", false);
    publish_();
    return;
  }
  dashboard_open_ = true;
  publish_();
  update_owner_(millis());
}

void RoomPanel::close_dashboard() {
  dashboard_open_ = false;
  publish_();
  update_owner_(millis());
  if (stream_) stream_->release_frame_snapshot();
}

void RoomPanel::set_overlay(Overlay overlay, bool on) {
  const uint8_t before = overlays_;
  overlays_ = on ? (overlays_ | overlay) : (overlays_ & ~overlay);
  if (before != overlays_) update_owner_(millis());
}

void RoomPanel::update_owner_(uint32_t now) {
  if (stream_ == nullptr || lvgl_ == nullptr) return;
  if (dashboard_open_ && camera_on_) stop_camera_();
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
  const bool notice_backdrop = dashboard_open_ && !asleep && overlays_ == OVERLAY_NOTICE;
  if (!notice_backdrop) show_backdrop_(false);
  give_display_to_(want);
  if (notice_backdrop) show_backdrop_(true);
}

void RoomPanel::show_backdrop_(bool show) {
  if (show == backdrop_visible_) return;
  if (show) {
    const auto snapshot = stream_->frame_snapshot();
    if (!snapshot.pixels) return;
    if (!backdrop_) {
      backdrop_ = lv_image_create(lv_display_get_layer_top(lvgl_->get_disp()));
      lv_obj_remove_flag(backdrop_, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_pos(backdrop_, 0, 0);
    }
    backdrop_image_.header.magic = LV_IMAGE_HEADER_MAGIC;
    backdrop_image_.header.cf = snapshot.big_endian ? LV_COLOR_FORMAT_RGB565_SWAPPED : LV_COLOR_FORMAT_RGB565;
    backdrop_image_.header.w = snapshot.width;
    backdrop_image_.header.h = snapshot.height;
    backdrop_image_.header.stride = snapshot.width * 2;
    backdrop_image_.data_size = static_cast<uint32_t>(snapshot.width) * snapshot.height * 2;
    backdrop_image_.data = snapshot.pixels;
    lv_image_set_src(backdrop_, &backdrop_image_);
    lv_obj_move_foreground(backdrop_);
    lv_obj_remove_flag(backdrop_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(backdrop_, LV_OBJ_FLAG_HIDDEN);
    lv_image_set_src(backdrop_, static_cast<const void *>(nullptr));
    lv_image_cache_drop(&backdrop_image_);
    lv_image_header_cache_drop(&backdrop_image_);
    backdrop_image_.data = nullptr;
  }
  backdrop_visible_ = show;
  if (backdrop_changed_) backdrop_changed_(show);
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

// ---------------------------------------------------------------- camera view
// The camera's line under "Other stream names", or "".
std::string RoomPanel::camera_target_(const std::string &entity) const {
  std::string found;
  each_camera_stream(settings_.camera_streams, [&](const std::string &e, const std::string &target) {
    if (e != entity) return false;
    found = target;
    return true;
  });
  return found;
}

bool RoomPanel::uses_webrtc_link_(const std::string &entity) const {
  return settings_.camera_source == "webrtc" && entity.rfind("camera.", 0) == 0 && !is_page(camera_target_(entity));
}

std::string RoomPanel::webrtc_link_id_(const std::string &entity) const {
  return "rc" + link_token_ + "_" + entity.substr(7);
}

std::string RoomPanel::camera_source_(const std::string &entity) const {
  if (entity.rfind("camera.", 0) != 0) return "";
  const std::string target = camera_target_(entity);
  if (is_page(target)) return target;
  if (settings_.camera_source == "webrtc") {
    std::string origin = ha_origin_ ? ha_origin_() : std::string();
    while (!origin.empty() && origin.back() == '/') origin.pop_back();
    if (!is_page(origin)) return "";
    // The integration's embed page plays the link registered for this
    // camera: muted, always playing, without its status line or zoom.
    // camera_page_url() adds the card's placement (its style option).
    return origin + "/webrtc/embed?url=" + webrtc_link_id_(entity) +
           "&mode=webrtc,mse&media=video&muted=true&intersection=0&background=true&digital_ptz=false";
  }
  if (settings_.camera_source != "go2rtc" || settings_.camera_server.empty()) return "";
  const std::string name = target.empty() ? entity.substr(7) : target;
  std::string base = settings_.camera_server;
  while (!base.empty() && base.back() == '/') base.pop_back();
  // go2rtc's player page: WebRTC, else MSE; video only (the panel has no
  // audio path from the stream).
  return base + "/stream.html?src=" + percent_encode(name) + "&mode=webrtc,mse&media=video";
}

// ---------------------------------------------------------------- camera sound
std::string RoomPanel::speaker_entity() const {
  return settings_.speaker_entity.empty() ? default_speaker_entity_() : settings_.speaker_entity;
}

std::string RoomPanel::default_speaker_entity_() const {
  // Home Assistant names the entity after the device: "<friendly name> Speaker".
  std::string slug;
  for (char c : App.get_friendly_name()) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (std::isalnum(u)) slug += static_cast<char>(std::tolower(u));
    else if (!slug.empty() && slug.back() != '_') slug += '_';
  }
  while (!slug.empty() && slug.back() == '_') slug.pop_back();
  return slug.empty() ? std::string() : "media_player." + slug + "_speaker";
}

// go2rtc's RTSP output (port 8554) of the camera's stream: Home Assistant's
// ffmpeg reads any audio codec from it, unlike go2rtc's AAC-only HTTP audio.
std::string RoomPanel::camera_audio_url_(const std::string &entity) const {
  if (entity.rfind("camera.", 0) != 0) return "";
  const std::string target = camera_target_(entity);
  if (is_page(target)) return "";
  if (target.find("://") != std::string::npos) return target;
  if (settings_.camera_source == "webrtc") {
    // The integration's go2rtc names the stream after the entity while the
    // video plays (built in on the Home Assistant host).
    const std::string host = url_host(ha_origin_ ? ha_origin_() : std::string());
    return host.empty() ? std::string() : "rtsp://" + host + ":8554/" + (target.empty() ? entity : target);
  }
  if (settings_.camera_source == "go2rtc") {
    const std::string host = url_host(settings_.camera_server);
    return host.empty() ? std::string() : "rtsp://" + host + ":8554/" + (target.empty() ? entity.substr(7) : target);
  }
  return "";
}

int RoomPanel::camera_audio(const std::string &entity) const {
  if (!audio_play_ || entity.empty() || entity != camera_request_.entity || camera_audio_url_(entity).empty() ||
      speaker_entity().empty())
    return -1;
  return audio_entity_ == entity ? 1 : 0;
}

void RoomPanel::toggle_camera_audio(const std::string &entity) {
  if (audio_entity_ == entity) {
    stop_camera_audio_();
    return;
  }
  if (camera_audio(entity) < 0) return;
  stop_camera_audio_();
  const std::string url = camera_audio_url_(entity);
  if (!audio_play_(speaker_entity(), url)) {
    message_trigger_.trigger("Camera sound needs the Home Assistant connection.", true);
    return;
  }
  audio_entity_ = entity;
  ESP_LOGI(TAG, "Camera sound on %s: %s", speaker_entity().c_str(), entity.c_str());
}

void RoomPanel::stop_camera_audio_() {
  if (audio_entity_.empty()) return;
  audio_entity_.clear();
  if (audio_stop_) audio_stop_();
  ESP_LOGI(TAG, "Camera sound off");
}

bool RoomPanel::camera_view(const std::string &entity, int x, int y, int w, int h) {
  if (entity.empty() || stream_ == nullptr || settings_.server.empty() || camera_source_(entity).empty() || w <= 0 || h <= 0) {
    camera_request_ = {};
    return false;
  }
  // An even x keeps the cropped image's rows 4-byte aligned for LVGL; the
  // page places the player at the same position.
  camera_request_ = {entity, x & ~1, y, w, h};
  return true;
}

// ---------------------------------------------------------------- full screen
void RoomPanel::set_camera_fullscreen(bool on) {
  if (on == camera_fullscreen_) return;
  camera_fullscreen_ = on;  // The next loop opens the page for the new size.
  ESP_LOGI(TAG, "Live camera %s", on ? "full screen" : "in its frame");
}

void RoomPanel::reload_camera() {
  if (!camera_on_ || stream_ == nullptr) return;
  // The server skips an address it already shows: leave it, then reopen it
  // (registering a WebRTC Camera link again).
  if (camera_frames_ >= CAMERA_SHOW_AFTER_FRAMES && camera_frame_cb_) camera_frame_cb_(nullptr);
  camera_frames_ = 0;
  stream_->open_url("about:blank");
  camera_url_.clear();
  link_for_url_.clear();
  ESP_LOGI(TAG, "Live camera reloading");
}

RoomPanel::CameraRequest RoomPanel::shown_rect_() const {
  if (!camera_fullscreen_ || stream_ == nullptr) return camera_request_;
  return {camera_request_.entity, 0, 0, stream_->frame_width(), stream_->frame_height()};
}

// Degrees the stream page turns the video: full screen on a portrait screen.
int RoomPanel::camera_turn_() const {
  if (!camera_fullscreen_ || stream_ == nullptr || stream_->frame_height() <= stream_->frame_width()) return 0;
  return std::atoi(settings_.camera_rotation.c_str());
}

std::string RoomPanel::camera_page_url(const std::string &entity) const {
  const CameraRequest r = shown_rect_();
  const int turn = camera_turn_();
  const int fw = stream_ != nullptr ? stream_->frame_width() : 0, fh = stream_ != nullptr ? stream_->frame_height() : 0;
  const std::string source = camera_source_(entity);
  if (source.empty() || fw <= 0 || fh <= 0 || r.x < 0 || r.y < 0 || r.x + r.w > fw || r.y + r.h > fh) return "";
  char rect[96];
  if (uses_webrtc_link_(entity)) {
    // Home Assistant pages refuse to be framed (X-Frame-Options: SAMEORIGIN),
    // so the server opens the embed page itself and the card's style option
    // places the player: inside its shadow root, :host with !important wins
    // over the page's 100% size.
    char style[320];
    if (turn)
      snprintf(style, sizeof(style),
               ":host{position:fixed!important;left:50%%!important;top:50%%!important;width:100vh!important;"
               "height:100vw!important;transform:translate(-50%%,-50%%) rotate(%ddeg)!important}.header{display:none}video::-webkit-media-controls{display:none!important}",
               turn);
    else
      snprintf(style, sizeof(style),
               ":host{position:fixed!important;left:%.3f%%!important;top:%.3f%%!important;width:%.3f%%!important;"
               "height:%.3f%%!important}.header{display:none}video::-webkit-media-controls{display:none!important}",
               100.0 * r.x / fw, 100.0 * r.y / fh, 100.0 * r.w / fw, 100.0 * r.h / fh);
    return source + "&style=" + percent_encode(style);
  }
  if (wifi::global_wifi_component == nullptr) return "";
  char address[network::IP_ADDRESS_BUFFER_SIZE] = {0};
  for (const auto &ip : wifi::global_wifi_component->get_ip_addresses()) {
    if (ip.is_set() && ip.is_ip4()) {
      ip.str_to(address);
      break;
    }
  }
  if (!address[0]) return "";
  // The panel serves the page (/panel/camera): the server's browser lets a
  // page on the local network embed go2rtc, but not a data: URL. The frame
  // is placed in percent of the screen, so the stream zoom does not matter.
  snprintf(rect, sizeof(rect), "&x=%.3f&y=%.3f&w=%.3f&h=%.3f&r=%d", 100.0 * r.x / fw, 100.0 * r.y / fh,
           100.0 * r.w / fw, 100.0 * r.h / fh, turn);
  return std::string("http://") + address + "/panel/camera?u=" + percent_encode(source) + rect;
}

void RoomPanel::update_camera_(bool wifi_ok) {
  if (stream_ == nullptr) return;
  const bool asleep = onx_power::sleeping || onx_power::waking;
  const bool want = !camera_request_.entity.empty() && !dashboard_open_ && !asleep && wifi_ok && stream_->has_server() &&
                    (!camera_visible_ || camera_visible_());
  // Sound follows the camera screen, even before (or without) live video.
  if (!audio_entity_.empty() && (!want || audio_entity_ != camera_request_.entity)) stop_camera_audio_();
  if (!want) {
    if (camera_on_) stop_camera_();
    return;
  }
  // Like the dashboard, the stream's tasks and buffers start on first use.
  if (!stream_->is_started()) {
    stream_->start();
    return;
  }
  if (!stream_->is_connected()) return;  // A reconnect reopens the current page.
  const std::string url = camera_page_url(camera_request_.entity);
  if (url.empty()) {
    if (camera_on_) stop_camera_();
    return;
  }
  if (url != camera_url_) {
    if (uses_webrtc_link_(camera_request_.entity)) {
      // Register the link first (again after a Home Assistant restart, which
      // forgets it), then give the action time to arrive.
      if (link_for_url_ != url) {
        const std::string target = camera_target_(camera_request_.entity);
        if (!webrtc_link_ || !webrtc_link_(webrtc_link_id_(camera_request_.entity), target.empty() ? camera_request_.entity : "", target))
          return;
        link_for_url_ = url;
        link_ms_ = millis();
        return;
      }
      if (millis() - link_ms_ < CAMERA_LINK_DELAY_MS) return;
    }
    if (!stream_->open_url(url)) return;
    ESP_LOGI(TAG, "Live camera: %s", camera_request_.entity.c_str());
    camera_url_ = url;
    camera_opened_ms_ = millis();
    camera_shown_ = shown_rect_();
    if (camera_frames_ >= CAMERA_SHOW_AFTER_FRAMES && camera_frame_cb_) camera_frame_cb_(nullptr);
    camera_frames_ = 0;
  }
  if (!camera_on_) {
    stream_->set_offscreen(true);
    stream_->set_active(true);
    camera_on_ = true;
  }
  const uint8_t *pixels = stream_->take_offscreen_frame();
  if (pixels == nullptr) return;
  // Frames of the previous page do not count. The server reports the page it
  // shows; if that never matches (an address it rewrote), go by time.
  const bool on_page = !camera_url_.empty() && (stream_->get_current_url() == camera_url_ ||
                                                 millis() - camera_opened_ms_ > CAMERA_PAGE_TIMEOUT_MS);
  if (camera_frames_ < CAMERA_SHOW_AFTER_FRAMES && on_page) ++camera_frames_;
  if (camera_frames_ < CAMERA_SHOW_AFTER_FRAMES) return;
  const int fw = stream_->frame_width();
  const auto &r = camera_shown_;
  camera_image_.header.magic = LV_IMAGE_HEADER_MAGIC;
  camera_image_.header.cf = stream_->frame_big_endian() ? LV_COLOR_FORMAT_RGB565_SWAPPED : LV_COLOR_FORMAT_RGB565;
  camera_image_.header.w = r.w;
  camera_image_.header.h = r.h;
  camera_image_.header.stride = fw * 2;
  camera_image_.data_size = static_cast<uint32_t>(fw) * 2 * r.h;
  camera_image_.data = pixels + (static_cast<size_t>(r.y) * fw + r.x) * 2;
  if (camera_frame_cb_) camera_frame_cb_(&camera_image_);
}

void RoomPanel::stop_camera_() {
  stop_camera_audio_();
  // Detach the image before its buffer is freed.
  if (camera_frame_cb_) camera_frame_cb_(nullptr);
  lv_image_cache_drop(&camera_image_);
  lv_image_header_cache_drop(&camera_image_);
  camera_image_.data = nullptr;
  stream_->set_active(false);
  stream_->set_offscreen(false);
  stream_->release_frame_snapshot();
  camera_on_ = false;
  camera_url_.clear();
  link_for_url_.clear();
  camera_frames_ = 0;
  if (camera_fullscreen_) {
    camera_fullscreen_ = false;
    if (fullscreen_exit_) fullscreen_exit_();
  }
  ESP_LOGI(TAG, "Live camera stopped");
  // Ends the camera stream in the server's browser; the dashboard (when set)
  // loads in its place, ready for the next time it opens.
  const std::string next = settings_.url.empty() ? std::string("about:blank") : settings_.url;
  if (!stream_->open_url(next)) stream_->set_url(next);
}

std::string RoomPanel::stream_status() const {
  if (stream_ == nullptr || settings_.server.empty()) return "not configured";
  if (camera_on_) return "showing a camera";
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
    root["camera_source"] = settings_.camera_source;
    root["camera_server"] = settings_.camera_server;
    root["camera_streams"] = settings_.camera_streams;
    root["speaker_entity"] = settings_.speaker_entity;
    root["camera_rotation"] = settings_.camera_rotation;
    root["default_speaker_entity"] = audio_play_ ? default_speaker_entity_() : std::string();
    // Lets the settings page find this panel's speaker in Home Assistant.
    uint8_t mac[6];
    get_mac_address_raw(mac);
    char mac_text[18];
    snprintf(mac_text, sizeof(mac_text), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    root["mac"] = mac_text;
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
  next.camera_source = doc["camera_source"] | settings_.camera_source;
  if (!valid_camera_source(next.camera_source)) return "{\"error\":\"Unknown camera source\"}";
  next.camera_rotation = doc["camera_rotation"] | settings_.camera_rotation;
  if (next.camera_rotation != "0" && next.camera_rotation != "90" && next.camera_rotation != "270")
    return "{\"error\":\"Full-screen rotation must be 0, 90 or 270\"}";
  next.speaker_entity = trim(doc["speaker_entity"] | settings_.speaker_entity);
  if (!valid_speaker_entity(next.speaker_entity))
    return "{\"error\":\"Panel speaker must be a media_player entity, for example media_player.onx3248g035_speaker\"}";
  next.camera_server = trim(doc["camera_server"] | settings_.camera_server);
  next.camera_streams = trim(doc["camera_streams"] | settings_.camera_streams);
  if (!valid_camera_server(next.camera_server))
    return "{\"error\":\"go2rtc address must start with http:// or https://, for example http://192.168.1.10:1984\"}";
  if (!valid_camera_streams(next.camera_streams))
    return "{\"error\":\"Other stream names: one camera.entity=stream name, rtsp:// address or page address per line\"}";
  const float scale = doc["scale"] | settings_.scale;
  if (!valid_server(next.server)) return "{\"error\":\"Server must be host:port, for example 192.168.1.10:8081\"}";
  if (!valid_url(next.url)) return "{\"error\":\"Page address must start with http:// or https://\"}";
  if (!std::isfinite(scale) || scale < 0.5f || scale > 4.0f) return "{\"error\":\"Zoom must be between 0.5 and 4\"}";
  next.scale = std::round(scale * 100.0f) / 100.0f;
  const StreamSettings previous = settings_;
  settings_ = next;
  if (!save_()) return "{\"error\":\"Dashboard settings could not be saved\"}";
  apply_(false, previous);
  return state_json_();
}

}  // namespace esphome::room_companion
