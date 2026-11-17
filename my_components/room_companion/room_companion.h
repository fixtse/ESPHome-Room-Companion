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
#include <string>

namespace esphome::room_companion {

// Reasons the native UI must own the display even while the dashboard is open.
enum Overlay : uint8_t {
  OVERLAY_ASSIST = 1 << 0,  // Voice assistant running (and reply on screen).
  OVERLAY_NOTICE = 1 << 1,  // Notification shown or notification centre open.
};

struct StreamSettings {
  std::string server;  // RemoteWebView server, host:port.
  std::string url;     // Page the server renders.
  float scale{1.0f};
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
  void set_defaults(const std::string &server, const std::string &url) {
    settings_.server = server;
    settings_.url = url;
  }
  Trigger<bool> *get_owner_trigger() { return &owner_trigger_; }
  Trigger<std::string> *get_message_trigger() { return &message_trigger_; }

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

  // Overlays and input.
  void set_overlay(Overlay overlay, bool on);
  // Touch drives the native UI only when it is visible and no overlay owns input.
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
  void publish_();

  lvgl::LvglComponent *lvgl_{nullptr};
  remote_webview::RemoteWebView *stream_{nullptr};
  touchscreen::Touchscreen *touch_{nullptr};
  text::Text *url_text_{nullptr};
  switch_::Switch *dashboard_switch_{nullptr};
  CornerListener *listener_{nullptr};
  Trigger<bool> owner_trigger_;
  Trigger<std::string> message_trigger_;

  StreamSettings settings_;
  std::string boot_server_;
  float boot_scale_{1.0f};
  bool restart_required_{false};

  bool dashboard_open_{false};
  uint8_t overlays_{0};
  bool stream_owner_{false};
  uint32_t stream_lost_ms_{0};
  std::atomic<uint32_t> corner_since_ms_{0};
  std::atomic<bool> corner_requested_{false};
};

// Set in setup(); lets shared lambdas reach the panel without id().
inline RoomPanel *global_panel = nullptr;

}  // namespace esphome::room_companion
