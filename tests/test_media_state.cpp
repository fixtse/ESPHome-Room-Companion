#include "../onx_media_state.h"
#include <cassert>
int main() {
  assert(!onx_playlist::medium_press(799));
  assert(onx_playlist::medium_press(800));
  assert(onx_playlist::medium_press(1999));
  assert(!onx_playlist::medium_press(2000));
  assert(onx_playlist::action_valid("script.jazz"));
  assert(onx_playlist::action_valid("automation.radio"));
  assert(!onx_playlist::action_valid("light.radio"));
  assert(!onx_playlist::action_valid("script."));
  onx_playlist::Browser browser;
  browser.toggle("media_player.room");browser.rotate(-1,3);
  assert(browser.open && browser.selected==2);
  browser.rotate(1,3);assert(browser.selected==0);
  browser.toggle("media_player.room");assert(!browser.open);

  assert(onx_media::volume_percent(0.60000004f) == "60%");
  assert(onx_media::volume_percent(0.59999996f) == "60%");
  assert(onx_media::volume_percent(0.555f) == "56%");
  assert(onx_media::volume_percent(-1) == "0%" && onx_media::volume_percent(2) == "100%");
  assert(onx_media::volume_percent(NAN) == "--");
  using namespace onx_media;
  for (const auto &state : {"playing","paused","buffering"}) assert(has_playback(state));
  for (const auto &state : {"idle","off","on","unknown","unavailable"}) assert(!has_playback(state));
  assert(can_skip("playing",32,false)&&!can_skip("playing",32,true));
  assert(can_skip("paused",16,true)&&!can_skip("idle",48,false));

  TransportFeedback feedback;
  assert(feedback.view("media_player.room","paused",100)==Overlay::PAUSE);
  feedback.show("media_player.room",false,100);
  assert(feedback.view("media_player.room","paused",1099)==Overlay::NEXT);
  assert(feedback.view("media_player.room","paused",1100)==Overlay::PAUSE);
  feedback.show("media_player.room",true,2000);
  assert(feedback.view("media_player.room","playing",2000)==Overlay::PREVIOUS);
  assert(feedback.view("media_player.room","playing",3000)==Overlay::NONE);
  // A skip stays visible while the player briefly reports buffering or idle.
  feedback.show("media_player.room",false,3500);assert(feedback.view("media_player.room","idle",3600)==Overlay::NEXT);
  assert(feedback.view("media_player.room","idle",4600)==Overlay::NONE);
  feedback.show("media_player.room",false,4000);feedback.observe("media_player.other",true);
  assert(feedback.view("media_player.room","playing",4001)==Overlay::NONE);
  feedback.show("media_player.room",false,0xffffff00u);
  assert(feedback.remaining_ms("media_player.room",0x100u)==488);
  feedback.observe("media_player.room",false);assert(feedback.view("media_player.room","playing",0x101u)==Overlay::NONE);

  const std::string origin="http://ha.local:8123";
  assert(artwork_url(origin,"/api/media_player_proxy/player?token=example") == origin+"/api/media_player_proxy/player?token=example");
  assert(artwork_url(origin,"https://art.example/album.png")=="https://art.example/album.png");
  for (const auto &missing : {"", "None", "unknown", "unavailable", "//other.example/cover", "file:///etc/passwd"})
    assert(artwork_url(origin,missing).empty());
  assert(camera_snapshot_url(origin, "/api/camera_proxy/camera.front?token=abc", 0) == origin + "/api/camera_proxy/camera.front?token=abc");
  assert(camera_snapshot_url(origin, "/api/camera_proxy/camera.front?token=abc", 3) == origin + "/api/camera_proxy/camera.front?token=abc&_onx=3");
  assert(camera_snapshot_url(origin, "/api/camera_proxy/camera.front", 2) == origin + "/api/camera_proxy/camera.front?_onx=2");
  assert(camera_snapshot_url(origin, "", 2).empty());
  Progress p;
  p.set_state("playing", 1000); p.set_duration(200); p.set_position(20, 1000); p.set_updated_at(1000);
  auto view = p.view(2000, 1005.5, true);
  assert(view.visible && std::fabs(view.seconds - 25.5f) < .001f && view.permille == 128);
  p.set_state("paused", 2100); view = p.view(9000, 1012.5, true);
  assert(view.visible && std::fabs(view.seconds - 25.6f) < .001f);
  p.set_state("playing", 9000); view = p.view(10000, 1013.5, true);
  assert(std::fabs(view.seconds - 26.6f) < .001f);
  p.set_position(80, 10000); p.set_updated_at(1013.5); // Seek feedback replaces interpolation.
  view = p.view(10500, 1014, true); assert(std::fabs(view.seconds - 80.5f) < .001f);
  assert(!p.view(11000, 1015, false).visible);
  p.new_track(); assert(!p.view(11000, 1015, true).visible);
  p.set_position(0, 11000); p.set_updated_at(1015);
  view = p.view(11100, 1015.1, true); assert(view.visible && view.seconds < .11f);
  p.new_track(); p.set_updated_at(1016); // Same zero position need not be resent for a new track.
  assert(p.view(12000, 1016.1, true).visible);
  p.set_duration(0); assert(!p.view(12000, 1016.2, true).visible); // Live/unknown-duration streams.
  p.set_duration(NAN); assert(!p.view(12000, 1016.2, true).visible);
  p.set_duration(10); p.set_position(999, 13000);
  assert(p.view(13000, NAN, true).permille == 1000);
  p.set_position(0, UINT32_MAX - 500); p.updated_epoch = NAN;
  assert(std::fabs(p.view(499, NAN, true).seconds - 1) < .01f);
  p.set_state("idle", 500); assert(!p.view(500, NAN, true).visible);
  // A fresh report anchors to its arrival, not to the (whole-second) clock.
  Progress q;
  q.set_state("playing", 0); q.set_duration(200); q.set_position(30, 5000); q.set_updated_at(2000.0, 5000, 2000.9);
  assert(std::fabs(q.view(6000, 2010.0, true).seconds - 31) < .001f);   // Panel clock ignored.
  Progress r;  // A stale report (on subscribe) still uses the timestamp.
  r.set_state("playing", 0); r.set_duration(200); r.set_position(30, 5000); r.set_updated_at(2000.0, 5000, 2060.0);
  assert(std::fabs(r.view(5000, 2060.0, true).seconds - 90) < .001f);

  // ArtworkRetry fast exponential backoff, URL reset, success reset, and millis() wraparound.
  ArtworkRetry ar;
  assert(ar.ready("https://art/a.jpg", 100));
  assert(ar.on_failure("https://art/a.jpg", 1000) == 1500);
  assert(!ar.ready("https://art/a.jpg", 2499) && ar.ready("https://art/a.jpg", 2500));
  assert(ar.ready("https://art/b.jpg", 1100));  // Different URL is not blocked.
  assert(ar.on_failure("https://art/a.jpg", 2500) == 3500);
  assert(ar.on_failure("https://art/a.jpg", 6000) == 8000);
  assert(ar.on_failure("https://art/a.jpg", 14000) == 20000);
  assert(ar.on_failure("https://art/a.jpg", 34000) == 30000);
  assert(ar.on_failure("https://art/a.jpg", 64000) == 30000);  // Capped at 30 s.
  ar.on_success("https://art/a.jpg");
  assert(ar.failures == 0 && ar.ready("https://art/a.jpg", 64001));
  assert(ar.on_failure("https://art/wrap.jpg", UINT32_MAX - 500) == 1500);
  assert(!ar.ready("https://art/wrap.jpg", 998) && ar.ready("https://art/wrap.jpg", 999));

  // Idle media popup / stuck non-playing media player return-to-idle state machine.
  {
    IdlePopup popup;
    // 1. Standby + playing player opens popup and saves previous carousel index.
    IdleMediaInput in;
    in.mode_ok = in.allowed = in.standby_enabled = in.standby_active = in.user_idle = true;
    in.selected_index = 2; in.candidate = 5;
    auto d = decide_idle_media(popup, in);
    assert(d.action == IdleMediaAction::OPEN_POPUP && d.target_index == 5);
    assert(popup.visible && popup.previous_index == 2);

    // 2. While still playing and user stays idle, popup stays open.
    in.standby_active = false; in.detail_active = in.media_detail = in.cur_detail_playing = true;
    in.cur_detail = 5; in.selected_index = 5;
    d = decide_idle_media(popup, in);
    assert(d.action == IdleMediaAction::NONE && popup.visible);

    // 3. Playback stops/pauses while popup is active and user is idle -> closes immediately.
    in.cur_detail_playing = false; in.candidate = -1;
    d = decide_idle_media(popup, in);
    assert(d.action == IdleMediaAction::CLOSE_DETAIL && !popup.visible && popup.previous_index == 2);
  }
  {
    // 4. User interacts with popup (e.g. taps Pause): takes ownership, does NOT close
    // immediately, then closes once standby timeout elapses while still paused/not playing.
    IdlePopup popup{true, 3};
    IdleMediaInput in;
    in.mode_ok = in.allowed = in.standby_enabled = true;
    in.user_idle = false; in.detail_active = in.media_detail = true;
    in.cur_detail = 4; in.cur_detail_playing = false; in.candidate = -1;
    auto d = decide_idle_media(popup, in);
    assert(d.action == IdleMediaAction::NONE && !popup.visible && popup.previous_index == 3);
    // Still paused after standby timeout -> closes so panel returns to idle view.
    in.user_idle = true;
    d = decide_idle_media(popup, in);
    assert(d.action == IdleMediaAction::CLOSE_DETAIL && popup.previous_index == 3);
  }
  {
    // 5. User touches volume on a playing player, then walks away: seamlessly promotes
    // back to popup.visible = true after timeout, then closes immediately when it stops.
    IdlePopup popup{true, 1};
    IdleMediaInput in;
    in.mode_ok = in.allowed = in.standby_enabled = true;
    in.user_idle = false; in.detail_active = in.media_detail = in.cur_detail_playing = true;
    in.cur_detail = 4; in.candidate = 4;
    assert(decide_idle_media(popup, in).action == IdleMediaAction::NONE && !popup.visible);
    in.user_idle = true;
    assert(decide_idle_media(popup, in).action == IdleMediaAction::NONE && popup.visible && popup.previous_index == 1);
    in.cur_detail_playing = false; in.candidate = -1;
    assert(decide_idle_media(popup, in).action == IdleMediaAction::CLOSE_DETAIL && !popup.visible);
  }
  {
    // 6. Manually opened non-playing player (idle/paused/off/unavailable/HA disconnected)
    // closes after standby timeout when standby is enabled, but stays open while user is active,
    // while an overlay (notification centre / Assist) is open, or when standby is disabled.
    IdlePopup popup;
    IdleMediaInput in;
    in.mode_ok = true; in.allowed = false; in.standby_enabled = true;
    in.user_idle = true; in.overlay_blocking = true;
    in.detail_active = in.media_detail = true; in.cur_detail = 2;
    assert(decide_idle_media(popup, in).action == IdleMediaAction::NONE);
    in.overlay_blocking = false; in.standby_enabled = false;
    assert(decide_idle_media(popup, in).action == IdleMediaAction::NONE);
    in.standby_enabled = true; in.mode_ok = false;
    assert(decide_idle_media(popup, in).action == IdleMediaAction::NONE);
    in.mode_ok = true;
    assert(decide_idle_media(popup, in).action == IdleMediaAction::CLOSE_DETAIL);
  }
  {
    // 7. Manual non-playing player hands off to another playing player, and after idle
    // timeout switches to an already-playing other player in popup mode.
    IdlePopup popup;
    IdleMediaInput in;
    in.mode_ok = in.allowed = in.standby_enabled = true;
    in.detail_active = in.media_detail = true; in.cur_detail = 1; in.candidate = 2;
    in.detail_stopped_playing = true;
    auto d = decide_idle_media(popup, in);
    assert(d.action == IdleMediaAction::SWITCH_PLAYER && d.target_index == 2 && !popup.visible);
    in.detail_stopped_playing = false; in.user_idle = true;
    d = decide_idle_media(popup, in);
    assert(d.action == IdleMediaAction::SWITCH_PLAYER && d.target_index == 2 && popup.visible);
  }
  {
    // 8. Orphaned popup.visible when not on a media detail is repaired; non-media details untouched.
    IdlePopup popup{true, 4};
    IdleMediaInput in;
    in.mode_ok = in.allowed = in.standby_enabled = in.user_idle = true;
    in.detail_active = true; in.media_detail = false; in.candidate = 1;
    assert(decide_idle_media(popup, in).action == IdleMediaAction::NONE);
    assert(!popup.visible && popup.previous_index == -1);
  }
}
