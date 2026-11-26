#pragma once
#include <cstdint>
#include "onx_playlist.h"
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include <cmath>
namespace onx_media {
struct IdlePopup {bool visible{false};int previous_index{-1};};
inline IdlePopup idle_popup;
enum class IdleMediaAction : uint8_t { NONE, OPEN_POPUP, SWITCH_PLAYER, CLOSE_DETAIL };
struct IdleMediaInput {
  bool mode_ok{false};
  bool allowed{false};
  bool standby_enabled{false};
  bool standby_active{false};
  bool user_idle{false};
  bool overlay_blocking{false};
  bool detail_active{false};
  bool media_detail{false};
  bool cur_detail_playing{false};
  bool newly_playing_found{false};
  bool detail_stopped_playing{false};
  int cur_detail{-1};
  int selected_index{0};
  int candidate{-1};
};
struct IdleMediaDecision {
  IdleMediaAction action{IdleMediaAction::NONE};
  int target_index{-1};
};
inline IdleMediaDecision decide_idle_media(IdlePopup &popup, const IdleMediaInput &in) {
  // Repair orphaned popup state if detail exited or moved off a media player.
  if (popup.visible && !in.media_detail) {
    popup.visible = false;
    popup.previous_index = -1;
  }
  if (popup.visible) {
    if (!in.user_idle) {
      // User interaction takes ownership as a regular detail view while keeping
      // previous_index so exiting returns to the page that was active before.
      popup.visible = false;
    } else {
      if (!in.allowed || in.candidate < 0) {
        popup.visible = false;
        return {IdleMediaAction::CLOSE_DETAIL, -1};
      }
      if (in.candidate != in.cur_detail) {
        return {IdleMediaAction::SWITCH_PLAYER, in.candidate};
      }
      return {};
    }
  }
  if (in.media_detail) {
    // If the open media player paused/stopped and another player is now playing
    // (either newly started or already playing when this one paused), hand off to it.
    if (in.allowed && !in.cur_detail_playing && in.candidate >= 0 && in.candidate != in.cur_detail &&
        (in.newly_playing_found || in.detail_stopped_playing)) {
      return {IdleMediaAction::SWITCH_PLAYER, in.candidate};
    }
    // After the standby timeout with no user activity, a playing media detail
    // resumes automatic popup ownership, another playing player takes over, and
    // any non-playing player (paused, idle, off, standby, unavailable, etc.)
    // closes so the panel returns to the idle view instead of getting stuck.
    if (in.mode_ok && in.user_idle && in.standby_enabled && !in.overlay_blocking) {
      if (in.allowed && in.cur_detail_playing) {
        popup.visible = true;
        return {};
      }
      if (in.allowed && in.candidate >= 0) {
        popup.visible = true;
        return {IdleMediaAction::SWITCH_PLAYER, in.candidate};
      }
      return {IdleMediaAction::CLOSE_DETAIL, -1};
    }
    return {};
  }
  if (!in.allowed || in.candidate < 0 || in.detail_active || !in.standby_active || !in.standby_enabled) return {};
  popup.previous_index = in.selected_index;
  popup.visible = true;
  return {IdleMediaAction::OPEN_POPUP, in.candidate};
}
// Registered playing media players muted by Assist (unmuted when Assist ends).
inline std::vector<std::string> assist_muted;
// Artwork owner on the visible rectangular main page.
inline std::string main_entity;
inline std::function<void()> detach_main_artwork;
inline std::string volume_percent(float level) {
  if (!std::isfinite(level)) return "--";
  return std::to_string(static_cast<int>(std::lround(std::clamp(level, 0.0f, 1.0f) * 100.0f))) + "%";
}
// Preserve sequences until the renderer performs emoji shaping.
inline std::string display_text(std::string value) { return value; }

struct ProgressView {
  bool visible{false};
  float seconds{0}, duration{0};
  int permille{0};
};
// HA timestamps anchor late feedback; monotonic time provides smooth sub-second animation.
struct Progress {
  float duration{NAN}, position{NAN}, last_drawn{NAN};
  double updated_epoch{NAN};
  uint32_t received_ms{0}, drawn_ms{0};
  std::string state;
  bool waiting_track{false};
  void new_track() {
    position = last_drawn = NAN; updated_epoch = NAN; waiting_track = true;
  }
  void set_duration(float value) { duration = value; }
  void set_position(float value, uint32_t now) {
    position = value; received_ms = now; updated_epoch = NAN;
    last_drawn = NAN; waiting_track = false;
  }
  // `local_epoch` is this panel's clock when the report arrived. A fresh report
  // (within 3 s) is anchored to its arrival instead: Home Assistant's time sync
  // is only whole seconds, so panels side by side drifted a second apart, while
  // all of them receive the same report at the same moment.
  void set_updated_at(double value, uint32_t now_ms = 0, double local_epoch = NAN) {
    if (std::isfinite(value) && std::isfinite(local_epoch) && std::fabs(local_epoch - value) < 3) {
      if (waiting_track) { position = 0; waiting_track = false; }
      received_ms = now_ms; updated_epoch = NAN; last_drawn = NAN;
      return;
    }
    updated_epoch = value;
    // A changed track may restart at the same HA position (usually zero), so its
    // unchanged position attribute may not be resent. The new timestamp anchors zero.
    if (waiting_track && std::isfinite(value)) { position = 0; waiting_track = false; }
  }
  void set_state(const std::string &value, uint32_t now) {
    if (value == state) return;
    if (state == "playing" && value == "paused" && std::isfinite(position)) {
      position = std::isfinite(last_drawn) ? last_drawn + (now - drawn_ms) / 1000.0f
                                         : position + (now - received_ms) / 1000.0f;
      received_ms = now; updated_epoch = NAN;
    } else if (state == "paused" && value == "playing") {
      received_ms = now; updated_epoch = NAN;
    } else if (value != "playing" && value != "paused") {
      position = last_drawn = NAN; updated_epoch = NAN;
    }
    state = value;
  }
  ProgressView view(uint32_t now, double epoch, bool connected) {
    ProgressView out;
    if (!connected || waiting_track || (state != "playing" && state != "paused") ||
        !std::isfinite(duration) || duration <= 0 || !std::isfinite(position) || position < 0) return out;
    double elapsed = 0;
    if (state == "playing") {
      elapsed = (now - received_ms) / 1000.0;
      if (std::isfinite(epoch) && std::isfinite(updated_epoch)) {
        const double age = epoch - updated_epoch;
        if (age < -2) return out;
        elapsed = std::max(0.0, age);
      }
    }
    out.visible = true; out.duration = duration;
    out.seconds = std::clamp(static_cast<float>(position + elapsed), 0.0f, duration);
    out.permille = std::clamp(static_cast<int>(out.seconds / duration * 1000 + 0.5f), 0, 1000);
    last_drawn = out.seconds; drawn_ms = now;
    return out;
  }
};
inline bool has_playback(const std::string &state) {
  return state == "playing" || state == "paused" || state == "buffering";
}
inline bool can_skip(const std::string &state, uint32_t features, bool previous) {
  return has_playback(state) && (features & (previous ? 16U : 32U));
}
enum class Overlay { NONE, PAUSE, NEXT, PREVIOUS };
inline const char *overlay_name(Overlay overlay) {
  return overlay == Overlay::PAUSE ? "pause" : overlay == Overlay::NEXT ? "next" : overlay == Overlay::PREVIOUS ? "previous" : "";
}
inline const char *overlay_path(Overlay overlay) {
  return overlay == Overlay::PAUSE ? "M8 6h6v20H8zM18 6h6v20h-6z" :
         overlay == Overlay::NEXT ? "M7 6v20l14-10zM23 6h3v20h-3z" :
         overlay == Overlay::PREVIOUS ? "M25 6v20L11 16zM6 6h3v20H6z" : "";
}
struct TransportFeedback {
  std::string entity;
  uint32_t sent_ms{0};
  Overlay action{Overlay::NONE};
  void clear() { entity.clear(); action = Overlay::NONE; }
  void observe(const std::string &selected, bool active) {
    if (!active || entity != selected) clear();
  }
  void show(const std::string &selected, bool previous, uint32_t now) {
    entity = selected; sent_ms = now; action = previous ? Overlay::PREVIOUS : Overlay::NEXT;
  }
  uint32_t remaining_ms(const std::string &selected, uint32_t now) const {
    const uint32_t elapsed = now - sent_ms;
    return entity == selected && action != Overlay::NONE && elapsed < 1000 ? 1000 - elapsed : 0;
  }
  Overlay view(const std::string &selected, const std::string &state, uint32_t now) const {
    // Next/previous show for one second even if the player reports idle or
    // buffering while it changes track.
    if (remaining_ms(selected, now)) return action;
    return state == "paused" ? Overlay::PAUSE : Overlay::NONE;
  }
};
inline TransportFeedback transport;
inline std::string requested_url, ready_url;
inline uint32_t requested_ms{0};
// Non-zero: refresh the cover at this millis() once a retry guard expires.
inline uint32_t retry_at_ms{0};
// Diagnostics: cumulative cover fetch outcomes since boot.
inline uint32_t stat_attempts{0}, stat_ok{0}, stat_failed{0}, stat_timeouts{0}, stat_last_ms{0}, stat_slowest_ms{0};
inline std::string stat_last_failure;
// Copied from OnlineImage::last_stats() when a fetch ends.
struct LastFetch { uint32_t attempts{0}, open_ms{0}, read_ms{0}, bytes{0}, heap_free_start{0}, heap_largest_start{0}, heap_free_after_open{0}; bool https{false}, stack_psram{false}, cached{false}; std::string host; };
inline LastFetch last_fetch;
// Fast, wrap-safe exponential backoff for failed artwork URLs:
// 1.5 s -> 3.5 s -> 8 s -> 20 s -> 30 s, resetting on URL change or success.
struct ArtworkRetry {
  std::string url;
  uint32_t failures{0};
  uint32_t next_try_ms{0};
  bool waiting{false};
  static uint32_t delay_for(uint32_t streak) {
    static constexpr uint32_t DELAYS[] = {1500, 3500, 8000, 20000, 30000};
    if (streak == 0) return 0;
    return DELAYS[std::min<uint32_t>(streak - 1, 4)];
  }
  void clear() { url.clear(); failures = 0; next_try_ms = 0; waiting = false; }
  void on_success(const std::string &ok_url) {
    if (url.empty() || url == ok_url) clear();
  }
  uint32_t on_failure(const std::string &failed_url, uint32_t now) {
    if (failed_url != url) { url = failed_url; failures = 0; }
    ++failures;
    const uint32_t wait = delay_for(failures);
    next_try_ms = now + wait;
    waiting = true;
    return wait;
  }
  bool ready(const std::string &desired, uint32_t now) const {
    if (!waiting || desired != url) return true;
    return static_cast<int32_t>(now - next_try_ms) >= 0;
  }
  uint32_t wakeup_ms(const std::string &desired, uint32_t now) const {
    if (ready(desired, now)) return 0;
    return next_try_ms == 0 ? 1 : next_try_ms;
  }
};
inline ArtworkRetry artwork_retry;
inline void stat_end(bool ok, uint32_t now) {
  const uint32_t took = now - requested_ms;
  stat_last_ms = took; if (took > stat_slowest_ms) stat_slowest_ms = took;
  if (ok) ++stat_ok; else ++stat_failed;
}
inline bool downloading{false}, artwork_refresh_pending{false};
inline std::string candidate_url, artwork_wait_reason;
inline uint32_t candidate_ms{0}, last_artwork_end_ms{0};
inline uint32_t camera_refresh_seq{0};
inline std::string artwork_url(const std::string &origin, const std::string &picture) {
  if (picture.empty() || picture == "None" || picture == "unknown" || picture == "unavailable") return "";
  if (picture.rfind("http://", 0) == 0 || picture.rfind("https://", 0) == 0) return picture;
  if (picture[0] == '/' && picture.rfind("//", 0) != 0 && !origin.empty()) return origin + picture;
  return "";
}
inline std::string camera_snapshot_url(const std::string &origin, const std::string &picture, uint32_t seq) {
  std::string url = artwork_url(origin, picture);
  if (url.empty() || seq == 0) return url;
  url += (url.find('?') == std::string::npos ? "?_onx=" : "&_onx=") + std::to_string(seq);
  return url;
}
inline std::string camera_stream_url(const std::string &origin, const std::string &picture) {
  std::string url = artwork_url(origin, picture);
  const size_t pos = url.find("/api/camera_proxy/");
  if (pos == std::string::npos) return "";
  url.replace(pos, 18, "/api/camera_proxy_stream/");
  return url;
}

}
