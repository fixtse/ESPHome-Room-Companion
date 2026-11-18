#pragma once
// Touch input for the rectangular panels.
//
// The native UI is driven by encoder detents and one button. This component maps
// touch onto exactly those events, so the shared input behaviour is reused:
//   swipe (horizontal, or vertical on detail pages) -> encoder detents
//   tap -> button press + release        hold -> button held (with swipes: held turn)
//   tap on a strip icon -> jump that many carousel positions
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/touchscreen/touchscreen.h"
#include "esphome/components/onx_button/onx_button.h"
#include <algorithm>
#include <cstdlib>
#include <functional>

namespace esphome::onx_touch_input {

class TouchInput : public Component, public touchscreen::TouchListener {
 public:
  void set_touchscreen(touchscreen::Touchscreen *touch) { touch_ = touch; }
  void set_button(onx_button::BufferedButton *button) { button_ = button; }
  void set_geometry(int width, int strip_top, int strip_spacing, int step_px) {
    width_ = width; strip_top_ = strip_top; spacing_ = strip_spacing; step_px_ = step_px;
  }
  void set_hold_ms(uint32_t ms) { hold_ms_ = ms; }
  // Touches starting inside this rectangle are left to LVGL (e.g. the
  // notification bell). Several regions can be added.
  void add_passthrough(int x0, int y0, int x1, int y1) {
    if (passthrough_count_ < 4) passthrough_[passthrough_count_++] = {x0, y0, x1, y1};
  }
  // While false (dashboard stream, notification centre) touches are ignored.
  void set_enabled_lambda(std::function<bool()> &&f) { enabled_ = std::move(f); }
  // Dynamic regions left to LVGL (on-screen buttons that come and go).
  void set_tap_fn(std::function<bool(int, int)> &&f) { tap_fn_ = std::move(f); }
  void set_hold_fn(std::function<bool(int, int)> &&f) { hold_fn_ = std::move(f); }
  void set_passthrough_fn(std::function<bool(int, int)> &&f) { passthrough_fn_ = std::move(f); }

  void set_page_swipe(std::function<bool()> &&f) { page_swipe_ = std::move(f); }
  void set_back_enabled(std::function<bool()> &&f) { back_enabled_ = std::move(f); }
  void set_native_touch(std::function<bool()> &&f) { native_touch_ = std::move(f); }
  void set_edge_back(std::function<bool()> &&f) { edge_back_ = std::move(f); }
  void set_swipe_left_back(std::function<bool()> &&f) { swipe_left_back_ = std::move(f); }
  void set_pull_down_enabled(std::function<bool()> &&f) { pull_down_enabled_ = std::move(f); }
  Trigger<> *get_pull_down_trigger() { return &pull_down_trigger_; }
  Trigger<> *get_back_trigger() { return &back_trigger_; }
  bool suppress_click() const { return state_ == State::SWIPE || (last_swipe_ms_ && millis() - last_swipe_ms_ < 180); }

  Trigger<int> *get_turn_trigger() { return &turn_trigger_; }
  void set_dock_scroll(std::function<bool(int)> f) { dock_scroll_=std::move(f); }
  Trigger<int> *get_strip_trigger() { return &strip_trigger_; }

  void setup() override {
    if (touch_ != nullptr) touch_->register_listener(this);
  }
  void loop() override {
    // Hold is time-based: promote a still finger to a held button.
    if (!(dock_scroll_ && y0_>=strip_top_ && page_swipe_ && page_swipe_()) && !native_ && !back_ && !pull_only_ && state_ == State::DOWN && millis() - down_ms_ >= hold_ms_) {
      if (hold_fn_ && hold_fn_(x0_, y0_)) { state_ = State::IGNORED; return; }
      state_ = State::HELD;
      press_(true);
    }
  }
  float get_setup_priority() const override { return setup_priority::DATA; }

  void update(const touchscreen::TouchPoints_t &points) override {
    const touchscreen::TouchPoint *point = nullptr;
    for (const auto &p : points)
      if ((p.state & (touchscreen::STATE_PRESSED | touchscreen::STATE_UPDATED)) && !(p.state & touchscreen::STATE_RELEASING)) {
        point = &p;
        break;
      }
    if (point == nullptr) return;
    if (state_ == State::IDLE) begin_(point->x, point->y);
    else if (state_ == State::IGNORED) {
      // LVGL owns the button, but dragging over it must not become a click.
      if (std::abs(point->x - x0_) >= SLOP || std::abs(point->y - y0_) >= SLOP)
        last_swipe_ms_ = millis();
    } else move_(point->x, point->y);
  }
  void release() override { end_(); }

 protected:
  std::function<bool(int, int)> tap_fn_, hold_fn_;
  enum class State { IDLE, IGNORED, DOWN, SWIPE, HELD };
  struct Rect { int x0, y0, x1, y1; };

  bool enabled_now_() { return !enabled_ || enabled_(); }
  void begin_(int x, int y) {
    x0_ = x; y0_ = y;
    for (int i = 0; i < passthrough_count_; ++i) {
      const auto &r = passthrough_[i];
      if (x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1) { state_ = State::IGNORED; return; }
    }
    back_ = back_enabled_ && back_enabled_();
    left_back_ = back_ && swipe_left_back_ && swipe_left_back_();
    // Navigation can start over an LVGL control, but LVGL still owns taps there.
    const bool edge = x < EDGE && back_ && edge_back_ && edge_back_();
    const bool passthrough = !edge && passthrough_fn_ && passthrough_fn_(x, y);
    if (passthrough && !left_back_) { state_ = State::IGNORED; return; }
    // In detail views horizontal swipes page through the view; only a swipe
    // that starts at the left edge goes back.
    if (back_ && x >= EDGE && edge_back_ && edge_back_()) back_ = false;
    native_ = passthrough || (native_touch_ && native_touch_());
    pull_ = y < EDGE && pull_down_enabled_ && pull_down_enabled_();
    const bool enabled = enabled_now_();
    if (!back_ && !pull_ && !enabled) { state_ = State::IGNORED; return; }
    pull_only_ = !back_ && !enabled;  // e.g. a pull-down over the dashboard stream
    state_ = State::DOWN; x0_ = x; y0_ = y; x_ = x; y_ = y; down_ms_ = millis(); consumed_ = 0; axis_ = 0;
  }
  void move_(int x, int y) {
    x_ = x; y_ = y;
    const int dx = x - x0_, dy = y - y0_;
    if (axis_ == 0) {
      if (std::abs(dx) < SLOP && std::abs(dy) < SLOP) return;
      axis_ = std::abs(dx) >= std::abs(dy) ? 1 : 2;
      if (state_ == State::DOWN) state_ = State::SWIPE;  // A held finger keeps the button down.
    }
    if (axis_ == 1 && (back_ || left_back_)) { last_swipe_ms_ = millis(); return; }
    if (axis_ == 2 && pull_) { last_swipe_ms_ = millis(); return; }
    if (pull_only_) return;
    if (native_) { last_swipe_ms_ = millis(); return; }
    if (page_swipe_ && page_swipe_()) {
      if (axis_ == 1 && y0_>=strip_top_ && dock_scroll_) {
        const int step=std::max(24,spacing_/2);
        if(std::abs(dx+consumed_*step)>=step) {
          const int direction=dx+consumed_*step<0?1:-1;
          if(dock_scroll_(direction))consumed_+=direction;
        }
        return;
      }
      if (axis_ == 1 && consumed_ == 0 && std::abs(dx) >= 40) {
        consumed_ = 1; turn_trigger_.trigger(dx < 0 ? 1 : -1);
      }
      return;
    }
    // Swiping left/up moves forward (clockwise), right/down backward.
    const int travel = axis_ == 1 ? -dx : -dy;
    const int steps = travel / step_px_;
    while (consumed_ < steps) { ++consumed_; turn_trigger_.trigger(1); }
    while (consumed_ > steps) { --consumed_; turn_trigger_.trigger(-1); }
  }
  void end_() {
    const State state = state_;
    state_ = State::IDLE;
    const int dx = x_ - x0_;
    if (state == State::SWIPE && axis_ == 1 && ((left_back_ && dx <= -40) || (back_ && std::abs(dx) >= 40))) {
      last_swipe_ms_ = millis(); back_trigger_.trigger(); return;
    }
    if (state == State::SWIPE && axis_ == 2 && pull_ && y_ - y0_ >= 50) {
      last_swipe_ms_ = millis(); pull_down_trigger_.trigger(); return;
    }
    if (native_ || pull_only_) return;
    if (state == State::HELD) { press_(false); return; }
    if (state != State::DOWN) return;  // Swipes and ignored touches end here.
    if (y0_ >= strip_top_ && spacing_ > 0) {
      const int offset = (x0_ - width_ / 2 + (x0_ >= width_ / 2 ? spacing_ / 2 : -spacing_ / 2)) / spacing_;
      if (offset != 0 || dock_scroll_) strip_trigger_.trigger(offset);
      return;
    }
    if (tap_fn_ && tap_fn_(x0_, y0_)) return;
    press_(true);
    press_(false);
  }
  void press_(bool pressed) {
    if (button_ != nullptr) button_->set_virtual_pressed(pressed);
  }

  static constexpr int SLOP = 14;
  static constexpr int EDGE = 32;  // Edge zone for back (left) and pull-down (top).
  touchscreen::Touchscreen *touch_{nullptr};
  onx_button::BufferedButton *button_{nullptr};
  std::function<bool()> enabled_, back_enabled_, native_touch_, page_swipe_, edge_back_, swipe_left_back_, pull_down_enabled_;
  std::function<bool(int, int)> passthrough_fn_;
  Trigger<> back_trigger_, pull_down_trigger_;
  bool back_{false}, left_back_{false}, native_{false}, pull_{false}, pull_only_{false};
  uint32_t last_swipe_ms_{0};
  std::function<bool(int)> dock_scroll_;
  Trigger<int> turn_trigger_, strip_trigger_;
  Rect passthrough_[4]{};
  int passthrough_count_{0};
  int width_{240}, strip_top_{100000}, spacing_{0}, step_px_{24};
  uint32_t hold_ms_{350};
  State state_{State::IDLE};
  int x0_{0}, y0_{0}, x_{0}, y_{0}, consumed_{0}, axis_{0};
  uint32_t down_ms_{0};
};

}  // namespace esphome::onx_touch_input
