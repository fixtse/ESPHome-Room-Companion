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

  // Immediate visual feedback: (x, y, true) when a finger lands on a target this
  // component handles, (.., false) once it becomes a swipe or lifts.
  void set_press_fn(std::function<void(int, int, bool)> f) { press_fn_ = std::move(f); }

  void set_page_swipe(std::function<bool()> &&f) { page_swipe_ = std::move(f); }
  void set_back_enabled(std::function<bool()> &&f) { back_enabled_ = std::move(f); }
  void set_native_touch(std::function<bool()> &&f) { native_touch_ = std::move(f); }
  void set_edge_back(std::function<bool()> &&f) { edge_back_ = std::move(f); }
  void set_swipe_left_back(std::function<bool()> &&f) { swipe_left_back_ = std::move(f); }
  void set_pull_down_enabled(std::function<bool()> &&f) { pull_down_enabled_ = std::move(f); }
  // Photo idle screens: a horizontal swipe changes the photo (+1 next, -1
  // previous); a tap or any other swipe is a button press (leaves the screen).
  void set_photo_swipe(std::function<bool()> &&f) { photo_swipe_ = std::move(f); }
  Trigger<int> *get_photo_swipe_trigger() { return &photo_swipe_trigger_; }
  Trigger<> *get_pull_down_trigger() { return &pull_down_trigger_; }
  // Live feedback for the pull-down: `drag` gets the travel while the finger
  // moves; `release` gets travel and duration and returns whether to fire
  // on_pull_down (it may also handle the opening itself and return false).
  void set_pull_down_drag(std::function<void(int)> &&drag, std::function<bool(int, uint32_t)> &&release) {
    pull_down_drag_ = std::move(drag); pull_down_release_ = std::move(release);
  }
  // Swipe up (from the lower part of the screen) closes an overlay such as
  // the notification centre; LVGL keeps receiving the touch.
  void set_pull_up_enabled(std::function<bool()> &&f) { pull_up_enabled_ = std::move(f); }
  Trigger<> *get_pull_up_trigger() { return &pull_up_trigger_; }
  // Optional live feedback for that swipe: `drag` gets the vertical travel while
  // the finger moves; `release` gets the travel and duration and decides
  // whether the overlay closes (otherwise any upward travel of 40 px does).
  void set_pull_up_drag(std::function<void(int)> &&drag, std::function<bool(int, uint32_t)> &&release) {
    pull_up_drag_ = std::move(drag); pull_up_release_ = std::move(release);
  }
  // A swipe up that starts where this returns true always closes the overlay,
  // even while its list could still scroll (e.g. the handle at the bottom).
  // Called when a list scroll turns into the overlay drag (LVGL must let go).
  void set_pull_up_handoff(std::function<void()> f) { handoff_cb_ = std::move(f); }
  void set_pull_up_zone(std::function<bool(int, int)> &&f) { pull_up_zone_ = std::move(f); }
  Trigger<> *get_back_trigger() { return &back_trigger_; }
  // Include LVGL passthrough touches and allow its release/click dispatch to finish.
  bool interaction_pending() const { return touching_ || (released_ && millis() - released_ms_ < 180); }
  void acknowledge_click() { released_ = false; }
  bool suppress_click() const { return state_ == State::SWIPE || (last_swipe_ms_ && millis() - last_swipe_ms_ < 180); }

  Trigger<int> *get_turn_trigger() { return &turn_trigger_; }
  void set_dock_tap(std::function<void(int)> f) { dock_tap_=std::move(f); }
  void set_dock_scroll(std::function<void(int)> f) { dock_scroll_=std::move(f); }
  Trigger<int> *get_strip_trigger() { return &strip_trigger_; }

  void setup() override {
    if (touch_ != nullptr) touch_->register_listener(this);
  }
  void loop() override {
    flush_();
    if (press_pending_ && (state_ != State::DOWN || !touching_)) press_pending_ = false;
    if (press_pending_ && millis() - down_ms_ >= PRESS_DELAY_MS) {
      press_pending_ = false; pressed_ = true; press_fn_(x0_, y0_, true);
    }
    // Hold is time-based: promote a still finger to a held button.
    if (!photo_ && !(dock_scroll_ && y0_>=strip_top_ && page_swipe_ && page_swipe_()) && !native_ && !back_ && !pull_only_ && !zone_ && state_ == State::DOWN && millis() - down_ms_ >= hold_ms_) {
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
    if (end_pending_ && !touching_) flush_();
    touching_ = true;
    if (state_ == State::IDLE) {
      begin_(point->x, point->y);
      // Like a phone list: highlight only once the finger has stayed put
      // briefly, so the start of a swipe never flashes the card.
      press_pending_ = state_ == State::DOWN && press_fn_ && !photo_ && !native_ && !pull_only_ && !zone_;
    }
    else if (state_ == State::IGNORED && handoff_(point->x, point->y)) {
    } else if (state_ == State::IGNORED) {
      // LVGL owns the button, but dragging over it must not become a click.
      if (std::abs(point->x - x0_) >= SLOP || std::abs(point->y - y0_) >= SLOP)
        last_swipe_ms_ = millis();
    } else move_(point->x, point->y);
  }
  // The touchscreen calls update()/release() from its own loop. Page changes
  // (swipes, taps) rebuild screens for 40-140 ms, so they run from this
  // component's loop instead, keeping the touch read itself short.
  void release() override {
    press_pending_ = false; unpress_(); touching_ = false; released_ = true; released_ms_ = millis();
    end_pending_ = true;
  }

 protected:
  bool touching_{false}, released_{false};
  uint32_t released_ms_{0};
  std::function<bool(int, int)> tap_fn_, hold_fn_;
  enum class State { IDLE, IGNORED, DOWN, SWIPE, HELD };
  struct Rect { int x0, y0, x1, y1; };

  bool enabled_now_() { return !enabled_ || enabled_(); }
  void begin_(int x, int y) {
    x0_ = x; y0_ = y;
    last_swipe_ms_ = 0;  // A new physical touch is not the previous gesture's delayed click.
    pull_base_ = 0;
    // The overlay's close zone wins over passthrough and disabled input.
    if (pull_up_zone_ && pull_up_zone_(x, y)) {
      back_ = left_back_ = native_ = pull_ = pull_only_ = false;
      pull_up_ = zone_ = true;
      state_ = State::DOWN; x_ = x; y_ = y; down_ms_ = millis(); consumed_ = 0; axis_ = 0;
      return;
    }
    for (int i = 0; i < passthrough_count_; ++i) {
      const auto &r = passthrough_[i];
      if (x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1) {
        state_ = State::IGNORED;
        return;
      }
    }
    zone_ = false;
    photo_ = photo_swipe_ && photo_swipe_();
    if (photo_) {
      back_ = left_back_ = native_ = pull_ = pull_up_ = pull_only_ = false;
      state_ = State::DOWN; x_ = x; y_ = y; down_ms_ = millis(); consumed_ = 0; axis_ = 0;
      return;
    }
    back_ = back_enabled_ && back_enabled_();
    left_back_ = back_ && swipe_left_back_ && swipe_left_back_();
    // Navigation can start over an LVGL control, but LVGL still owns taps there.
    const bool edge = x < EDGE && back_ && edge_back_ && edge_back_();
    const bool passthrough = !edge && passthrough_fn_ && passthrough_fn_(x, y);
    if (passthrough && !left_back_) {
      state_ = State::IGNORED;
      return;
    }
    // In detail views horizontal swipes page through the view; only a swipe
    // that starts at the left edge goes back.
    if (back_ && x >= EDGE && edge_back_ && edge_back_()) back_ = false;
    native_ = passthrough || (native_touch_ && native_touch_());
    pull_ = y < EDGE && pull_down_enabled_ && pull_down_enabled_();
    // With drag feedback the swipe can start anywhere; otherwise only on the
    // lower half (above the notification list's end and the handle).
    pull_up_ = pull_up_enabled_ && (pull_up_drag_ || (strip_top_ < 100000 && y >= strip_top_ / 2)) && pull_up_enabled_();
    const bool enabled = enabled_now_();
    if (!back_ && !pull_ && !pull_up_ && !enabled) { state_ = State::IGNORED; return; }
    pull_only_ = !back_ && !enabled;  // e.g. a pull-down over the dashboard stream
    state_ = State::DOWN; x0_ = x; y0_ = y; x_ = x; y_ = y; down_ms_ = millis(); consumed_ = 0; axis_ = 0;
  }
  void move_(int x, int y) {
    x_ = x; y_ = y;
    const int dx = x - x0_, dy = y - y0_;
    if (press_pending_ && (std::abs(dx) > PRESS_STILL || std::abs(dy) > PRESS_STILL)) press_pending_ = false;
    if (axis_ == 0) {
      if (std::abs(dx) < SLOP && std::abs(dy) < SLOP) return;
      axis_ = std::abs(dx) >= std::abs(dy) ? 1 : 2;
      if (state_ == State::DOWN) state_ = State::SWIPE;  // A held finger keeps the button down.
      press_pending_ = false;
      unpress_();
    }
    if (photo_) { last_swipe_ms_ = millis(); return; }
    if (axis_ == 1 && (back_ || left_back_)) { last_swipe_ms_ = millis(); return; }
    if (zone_ && axis_ != 2) { last_swipe_ms_ = millis(); return; }
    if (axis_ == 2 && pull_ && pull_down_drag_) { last_swipe_ms_ = millis(); pull_down_drag_(dy); return; }
    if (axis_ == 2 && pull_up_ && pull_up_drag_) { last_swipe_ms_ = millis(); pull_up_drag_(dy - pull_base_); return; }
    if (axis_ == 2 && (pull_ || pull_up_)) { last_swipe_ms_ = millis(); return; }
    if (pull_only_) return;
    if (native_) { last_swipe_ms_ = millis(); return; }
    if (page_swipe_ && page_swipe_()) {
      if (axis_ == 1 && y0_>=strip_top_ && dock_scroll_) {
        const int step = std::max(24, spacing_ / 2);
        const int steps = (-dx - consumed_ * step) / step;
        if (steps != 0) {
          consumed_ += steps;
          pending_dock_ += steps;
        }
        return;
      }
      if (axis_ == 1 && consumed_ == 0 && std::abs(dx) >= 40) {
        consumed_ = 1; pending_turns_ += dx < 0 ? 1 : -1;
      }
      return;
    }
    // Swiping left/up moves forward (clockwise), right/down backward.
    const int travel = axis_ == 1 ? -dx : -dy;
    const int steps = travel / step_px_;
    while (consumed_ < steps) { ++consumed_; ++pending_turns_; }
    while (consumed_ > steps) { --consumed_; --pending_turns_; }
  }
  void end_() {
    const State state = state_;
    state_ = State::IDLE;
    const int dx = x_ - x0_;
    if (photo_) {
      photo_ = false;
      if (state == State::SWIPE && axis_ == 1 && std::abs(dx) >= 40) {
        last_swipe_ms_ = millis(); photo_swipe_trigger_.trigger(dx < 0 ? 1 : -1); return;
      }
      if (state == State::IGNORED) return;
      press_(true);
      press_(false);
      return;
    }
    if (state == State::SWIPE && axis_ == 1 && ((left_back_ && dx <= -40) || (back_ && std::abs(dx) >= 40))) {
      last_swipe_ms_ = millis(); back_trigger_.trigger(); return;
    }
    if (state == State::SWIPE && axis_ == 2 && pull_ && pull_down_release_) {
      last_swipe_ms_ = millis();
      if (pull_down_release_(y_ - y0_, millis() - down_ms_)) pull_down_trigger_.trigger();
      return;
    }
    if (state == State::SWIPE && axis_ == 2 && pull_ && y_ - y0_ >= 50) {
      last_swipe_ms_ = millis(); pull_down_trigger_.trigger(); return;
    }
    if (state == State::SWIPE && axis_ == 2 && pull_up_ && pull_up_release_) {
      last_swipe_ms_ = millis();
      if (pull_up_release_(y_ - y0_ - pull_base_, millis() - down_ms_)) pull_up_trigger_.trigger();
      return;
    }
    if (state == State::SWIPE && axis_ == 2 && pull_up_ && y0_ - y_ >= 40) {
      last_swipe_ms_ = millis(); pull_up_trigger_.trigger(); return;
    }
    if (native_ || pull_only_ || zone_) return;  // LVGL handles taps in the close zone.
    if (state == State::HELD) { press_(false); return; }
    if (state != State::DOWN) return;  // Swipes and ignored touches end here.
    if (y0_ >= strip_top_ && spacing_ > 0) {
      if(dock_tap_ && page_swipe_ && page_swipe_()){dock_tap_(x0_);return;}
      const int offset = (x0_ - width_ / 2 + (x0_ >= width_ / 2 ? spacing_ / 2 : -spacing_ / 2)) / spacing_;
      if (offset != 0 || dock_scroll_) strip_trigger_.trigger(offset);
      return;
    }
    if (tap_fn_ && tap_fn_(x0_, y0_)) return;
    press_(true);
    press_(false);
  }
  int pending_turns_{0}, pending_dock_{0};
  bool end_pending_{false};
  void flush_() {
    if (pending_dock_ != 0 && dock_scroll_) { const int n = pending_dock_; pending_dock_ = 0; dock_scroll_(n); }
    pending_dock_ = 0;
    while (pending_turns_ > 0) { --pending_turns_; turn_trigger_.trigger(1); }
    while (pending_turns_ < 0) { ++pending_turns_; turn_trigger_.trigger(-1); }
    if (end_pending_) { end_pending_ = false; end_(); }
  }
  // A swipe up that began by scrolling a list (left to LVGL) hands over to the
  // overlay's drag once the list cannot scroll further, so the same gesture
  // keeps going and closes it, following the finger.
  int pull_base_{0};
  std::function<void()> handoff_cb_;
  bool handoff_(int x, int y) {
    if (!pull_up_drag_ || !pull_up_enabled_ || y0_ - y < SLOP || std::abs(x - x0_) > y0_ - y) return false;
    if (!pull_up_enabled_()) return false;
    if (handoff_cb_) handoff_cb_();
    back_ = left_back_ = native_ = pull_ = pull_only_ = zone_ = false;
    pull_up_ = true;
    state_ = State::SWIPE; axis_ = 2; consumed_ = 0;
    pull_base_ = y - y0_;
    x_ = x; y_ = y; last_swipe_ms_ = millis();
    return true;
  }
  void unpress_() {
    if (pressed_ && press_fn_) press_fn_(x0_, y0_, false);
    pressed_ = false;
  }
  std::function<void(int, int, bool)> press_fn_;
  bool pressed_{false}, press_pending_{false};
  // A card highlights only once the finger has rested this long without
  // moving more than PRESS_STILL px; the start of a swipe never qualifies.
  static constexpr uint32_t PRESS_DELAY_MS = 130;
  static constexpr int PRESS_STILL = 6;
  void press_(bool pressed) {
    if (button_ != nullptr) button_->set_virtual_pressed(pressed);
  }

  static constexpr int SLOP = 14;
  static constexpr int EDGE = 32;  // Edge zone for back (left) and pull-down (top).
  touchscreen::Touchscreen *touch_{nullptr};
  onx_button::BufferedButton *button_{nullptr};
  std::function<bool()> enabled_, back_enabled_, native_touch_, page_swipe_, edge_back_, swipe_left_back_, pull_down_enabled_,
      pull_up_enabled_;
  std::function<bool(int, int)> passthrough_fn_;
  std::function<void(int)> pull_up_drag_, pull_down_drag_;
  std::function<bool(int, uint32_t)> pull_down_release_;
  std::function<bool(int, uint32_t)> pull_up_release_;
  std::function<bool(int, int)> pull_up_zone_;
  Trigger<> back_trigger_, pull_down_trigger_, pull_up_trigger_;
  bool pull_up_{false}, zone_{false}, photo_{false};
  std::function<bool()> photo_swipe_;
  Trigger<int> photo_swipe_trigger_;
  bool back_{false}, left_back_{false}, native_{false}, pull_{false}, pull_only_{false};
  uint32_t last_swipe_ms_{0};
  std::function<void(int)> dock_scroll_;
  std::function<void(int)> dock_tap_;
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
