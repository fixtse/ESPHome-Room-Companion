#pragma once
// Room Companion panel extras on top of the shared application:
//  - dashboard stream settings (server, address, zoom), persisted in NVS and
//    edited from the panel's configuration page (ops panel_state / panel_save)
//  - display ownership between the native LVGL UI and the stream
//  - leaving the dashboard with a horizontal swipe
// Everything else (slots, presets, sleep, notifications, web page) is shared.
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/components/text/text.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/touchscreen/touchscreen.h"
#include "esphome/components/remote_webview/remote_webview.h"

#include <atomic>
#include <functional>
#include <string>

namespace esphome::room_companion {

// Reasons the native UI must own the display even while the dashboard is open.
enum Overlay : uint8_t {
  OVERLAY_ASSIST = 1 << 0,  // Voice assistant running (and reply on screen).
  OVERLAY_NOTICE = 1 << 1,  // Notification shown or notification centre open.
  OVERLAY_TIMER = 1 << 2,   // The panel's own Assist timer screen (started or ringing).
};

struct StreamSettings {
  std::string server;  // RemoteWebView server, host:port.
  std::string url;     // Page the server renders.
  float scale{1.0f};
  std::string dock_pin{"dashboard"};
  std::string toolbar_side{"right"};
  // Live camera view. Source "webrtc": the WebRTC Camera integration in Home
  // Assistant (its go2rtc plays the camera's own stream); "go2rtc": a go2rtc
  // web address (Frigate's too); "off". Optional per-camera stream names,
  // stream URLs or page addresses ("camera.porch=porch_hd, camera.door=http://...").
  std::string camera_source{"off"};
  // Camera sound: the panel's speaker entity in Home Assistant ("" = the
  // default media_player.<panel name>_speaker).
  std::string speaker_entity;
  std::string camera_server;
  std::string camera_streams;
};

class RoomPanel;

class CornerListener : public touchscreen::TouchListener {
 public:
  explicit CornerListener(RoomPanel *parent) : parent_(parent) {}
  void update(const touchscreen::TouchPoints_t &points) override;
  void release() override;

 protected:
  RoomPanel *parent_;
};

class RoomPanel : public Component {
 public:
  void set_lvgl(lvgl::LvglComponent *lvgl) { lvgl_ = lvgl; }
  void set_stream(remote_webview::RemoteWebView *stream) { stream_ = stream; }
  void set_touchscreen(touchscreen::Touchscreen *touch) { touch_ = touch; }
  void set_url_text(text::Text *t) { url_text_ = t; }
  void set_dashboard_switch(switch_::Switch *s) { dashboard_switch_ = s; }
  void set_defaults(const std::string &server, const std::string &url, float scale = 1.0f) {
    settings_.scale = scale;
    settings_.server = server;
    settings_.url = url;
  }
  Trigger<bool> *get_owner_trigger() { return &owner_trigger_; }
  Trigger<std::string, bool> *get_message_trigger() { return &message_trigger_; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  // Dashboard.
  void open_dashboard();
  void close_dashboard();
  bool dashboard_open() const { return dashboard_open_; }
  bool stream_owns_display() const { return stream_owner_; }
  std::string stream_status() const;
  void set_dashboard_url(const std::string &url);
  const StreamSettings &stream_settings() const { return settings_; }

  // Live camera view. The camera detail calls camera_view() each time it is
  // built, with the snapshot frame's screen rectangle (an empty entity when
  // it goes away); it returns false when the camera has no live stream. While
  // the visibility check passes, the stream renders the camera (go2rtc in the
  // server's browser) into an offscreen buffer and on_camera_frame hands each
  // new frame to the native UI (nullptr when the view stops).
  bool camera_view(const std::string &entity, int x, int y, int w, int h);
  bool camera_live() const { return camera_on_ && camera_frames_ >= CAMERA_SHOW_AFTER_FRAMES; }
  const lv_image_dsc_t *camera_frame() const { return camera_live() ? &camera_image_ : nullptr; }
  void set_camera_visible(std::function<bool()> check) { camera_visible_ = std::move(check); }
  void set_camera_frame_callback(std::function<void(const lv_image_dsc_t *)> callback) { camera_frame_cb_ = std::move(callback); }
  // WebRTC Camera source: Home Assistant's address (http://host:8123), and a
  // webrtc.create_link action (link id, camera entity or stream URL); the
  // action returns false while it cannot be sent.
  void set_ha_origin(std::function<std::string()> origin) { ha_origin_ = std::move(origin); }
  // Camera sound (builds with a speaker): Home Assistant plays the camera's
  // audio (go2rtc RTSP) on the panel's speaker through its ffmpeg proxy.
  // play(speaker entity, url) returns false while it cannot be sent.
  void set_camera_audio_actions(std::function<bool(const std::string &, const std::string &)> play, std::function<void()> stop) {
    audio_play_ = std::move(play);
    audio_stop_ = std::move(stop);
  }
  // -1: no sound for this camera, 0: off, 1: on.
  int camera_audio(const std::string &entity) const;
  void toggle_camera_audio(const std::string &entity);
  std::string speaker_entity() const;
  void set_webrtc_link_action(std::function<bool(const std::string &, const std::string &, const std::string &)> action) {
    webrtc_link_ = std::move(action);
  }
  std::string camera_page_url(const std::string &entity) const;

  // Overlays and input.
  void set_overlay(Overlay overlay, bool on);
  void set_backdrop_changed(std::function<void(bool)> callback) { backdrop_changed_ = std::move(callback); }
  // Touch drives the native UI only when it is visible and no overlay owns input.
  bool native_ui_can_render() const { return !stream_owner_ && !backdrop_visible_ && (overlays_ & ~OVERLAY_NOTICE) == 0; }
  bool native_ui_has_touch() const { return !stream_owner_ && overlays_ == 0; }

  // Configuration page hook: returns "" when the op is not ours.
  std::string handle_web(const std::string &op, const std::string &data);

  // Corner hold (touch listener).
  void on_points_(const touchscreen::TouchPoints_t &points);
  void on_release_();

 protected:
  void load_();
  bool save_();
  std::string settings_json_() const;
  std::string state_json_() const;
  void apply_(bool boot, const StreamSettings &previous);
  void update_owner_(uint32_t now);
  void give_display_to_(bool stream);
  void show_backdrop_(bool show);
  void publish_();
  void update_camera_(bool wifi_ok);
  void stop_camera_();
  std::string camera_source_(const std::string &entity) const;
  std::string camera_target_(const std::string &entity) const;
  bool uses_webrtc_link_(const std::string &entity) const;
  std::string webrtc_link_id_(const std::string &entity) const;
  std::string camera_audio_url_(const std::string &entity) const;
  std::string default_speaker_entity_() const;
  void stop_camera_audio_();

  lvgl::LvglComponent *lvgl_{nullptr};
  remote_webview::RemoteWebView *stream_{nullptr};
  touchscreen::Touchscreen *touch_{nullptr};
  text::Text *url_text_{nullptr};
  switch_::Switch *dashboard_switch_{nullptr};
  CornerListener *listener_{nullptr};
  Trigger<bool> owner_trigger_;
  Trigger<std::string, bool> message_trigger_;

  StreamSettings settings_;
  std::string boot_server_;
  float boot_scale_{1.0f};
  bool restart_required_{false};

  bool dashboard_open_{false};
  uint8_t overlays_{0};
  bool stream_owner_{false};
  bool backdrop_visible_{false};
  lv_obj_t *backdrop_{nullptr};
  lv_image_dsc_t backdrop_image_{};
  std::function<void(bool)> backdrop_changed_;
  uint32_t stream_lost_ms_{0};
  // Camera view. A page that is still loading draws a frame or two; the
  // image appears once frames keep coming (the video plays).
  static constexpr uint32_t CAMERA_SHOW_AFTER_FRAMES = 3;
  static constexpr uint32_t CAMERA_PAGE_TIMEOUT_MS = 4000;
  uint32_t camera_opened_ms_{0};
  // WebRTC Camera links: registered before each open (Home Assistant keeps
  // them in memory only), then the page opens once the action had time.
  static constexpr uint32_t CAMERA_LINK_DELAY_MS = 1500;
  std::function<std::string()> ha_origin_;
  std::function<bool(const std::string &, const std::string &, const std::string &)> webrtc_link_;
  std::string link_token_;
  std::function<bool(const std::string &, const std::string &)> audio_play_;
  std::function<void()> audio_stop_;
  std::string audio_entity_;
  std::string link_for_url_;
  uint32_t link_ms_{0};
  struct CameraRequest {
    std::string entity;
    int x{0}, y{0}, w{0}, h{0};
  } camera_request_;
  std::function<bool()> camera_visible_;
  std::function<void(const lv_image_dsc_t *)> camera_frame_cb_;
  bool camera_on_{false};
  std::string camera_url_;
  CameraRequest camera_shown_;
  uint32_t camera_frames_{0};
  lv_image_dsc_t camera_image_{};
  std::atomic<uint32_t> corner_since_ms_{0};
  std::atomic<bool> corner_requested_{false};
};

// Set in setup(); lets shared lambdas reach the panel without id().
inline RoomPanel *global_panel = nullptr;

}  // namespace esphome::room_companion
