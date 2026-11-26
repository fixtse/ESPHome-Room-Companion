# Architecture and software ancestry

The software base is an earlier fixtSE OpenNextion ESPHome project (credited in
[THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md)), from local commit
`6a710e2ce0258e3ced31da3177e00ed5f0b260e3`. Its Git history, headers, components
and shared application packages are preserved. This repository builds only the
3.5" (ONX3248G035) and 2.8" (ONX2432G028) panels. Rectangular firmware lives in `packages/room_companion/` and
`my_components/room_companion/`; both model roots merge one `application.yaml`.

## Layers

| Layer | Where | Notes |
|---|---|---|
| Board profiles | `packages/room_companion/boards/` | Pins, display driver, sizes, stream frame interval. |
| Platform | `platform.yaml` | ESP-IDF, PSRAM, UART0 logger, API, OTA, `web_server` (Visit link). |
| Provisioning | `features/provisioning.yaml` | Improv Serial (CH340 UART) + Improv BLE with BOOT authorization. No AP. |
| Core | `my_components/room_companion` + `features/companion.yaml` | Settings, presets, Visit page, display owner, sleep. |
| Native UI | `features/interface.yaml` | LVGL Home, Music, Assist, More, setup and notification pages. |
| Dashboard stream | `features/dashboard.yaml` + `my_components/remote_webview` | Optional, server-rendered HA dashboard. |
| Assist | `features/assist.yaml` | Voice satellite: PDM mic, I2S speaker, tap/BOOT/HA button. |
| Notifications | `features/notifications.yaml` | `show_message` / `dismiss_message` actions, optional picture. |
| Sleep | `features/sleep.yaml` + core | Connected low-power sleep, timeout and schedule. |

## Reused from the base project

- `onx_notification.h` — message model, HTML-to-text, bounded timeouts, suspend.
- `onx_subscriptions.h` — reference-counted Home Assistant subscriptions. Room
  entities change at runtime from the settings page without leaking API rows.
- `onx_control_state.h`, `onx_media_state.h` — value parsing and formatting.
- Patterns re-implemented for the rectangular settings model (the base project's code is
  bound to its 20-slot registry and round renderer): the nonce- and
  Origin-checked configuration page executed on the main loop, NVS-backed
  named presets with a Home Assistant **Preset** select that shows "Custom"
  after manual edits, Improv BLE with a physical authorizer, and connected
  low-power sleep (backlight off, LVGL paused, Wi-Fi modem sleep).

## Configuration page ("Visit")

`web_server` advertises port 80, so Home Assistant shows **Visit** on the device
page; Improv Serial also reports the address after provisioning. The
`room_companion` handler registers before `web_server` and owns every route once
Wi-Fi is connected, so ESPHome's generic entity REST endpoints are not exposed.
Changes are POSTed to `/rc/api` with a per-boot token embedded in the page and
an `Origin` that must match `Host`; requests are queued to the ESPHome main loop.
There is no login: anyone on the LAN who can open the page can change settings,
as with ESPHome's own web server. Optional Home Assistant sign-in happens in the
browser (OAuth with PKCE) only to list entities and dashboards; no token is
stored on the panel.

Settings are one JSON document in NVS namespace `rc`; presets are up to eight
documents in `rc_presets`. A preset holds everything except the stream server
and zoom.

## Display ownership

One renderer draws at a time. `RoomCompanion::update_owner_()` gives the panel
to the stream only when the mode is *dashboard*, the stream is connected, the
panel is awake and there is no overlay:

| Overlay | Set by |
|---|---|
| Setup | Wi-Fi not connected |
| Assist | Pipeline listening; cleared 6 s after it ends |
| Notice | `show_message`; cleared on dismiss/timeout |
| Menu | Swipe from the left edge to return from the dashboard to native controls |

Handing over pauses LVGL (`resume_on_input: false`) before the stream is
activated; taking the panel back deactivates the stream, waits for any in-flight
tile draw, then resumes LVGL with a full redraw. A lost stream keeps its last
frame for 3 s, then the native UI returns.

## Dashboard stream

The ay129 Waveshare project streams a Lovelace dashboard with
[RemoteWebViewClient](https://github.com/strange-v/RemoteWebViewClient) and
[RemoteWebViewServer](https://github.com/strange-v/RemoteWebViewServer). The
client must own its display, so it is vendored (MIT, `NOTICE.md`) with a small
handoff patch: lazy task start, `set_active()`, frames discarded without
decoding and no full-frame repair requests while inactive, and a full-frame
request on activation. The Waveshare YAML targets an RGB ST7701S panel, GT911
touch and PCA9554; these boards use SPI ST7789V/ST7796, CST826 and PCF8574, so
only the software approach is reused.

Expected cost on these SPI panels (estimates, not measured on hardware yet):
a full 320×480 frame is 300 KB of RGB565, about 60 ms on the 40 MHz bus plus
software JPEG decode, so full-screen changes run at roughly 5–10 fps; partial
updates are much cheaper. The stream's tasks (32 KB decode stack, 8 KB socket
task, 30 KB socket buffer) are created only when dashboard mode has a server.
Wi-Fi power saving is off for latency; sleep switches to modem sleep.

### Live camera view

The camera detail reuses the same client and server without handing over the
display. `RoomPanel::camera_view()` receives the snapshot frame's screen
rectangle from `build_camera()` and asks the server to open `/panel/camera`,
a page the panel serves that places a player iframe at that rectangle (in
percent of the screen, so the zoom does not matter): go2rtc's `stream.html`,
or the WebRTC Camera integration's `/webrtc/embed` with a link registered by
`webrtc.create_link`. Chromium's Private Network Access rules block a `data:`
page from embedding LAN players, hence the panel-served page.

The client runs in **offscreen** mode (`set_offscreen()`): tiles decode only into
the PSRAM frame copy (the same double buffer as the notification backdrop),
never to the display. On each finished frame the main loop takes the front
buffer (`take_offscreen_frame()`) and LVGL shows the rectangle as an RGB565
image with the full screen stride, over the snapshot. The decode task does not
write the next frame until the main loop has taken the last one (bounded wait),
so LVGL never reads a buffer being written, and the SPI bus has a single owner.
The image appears after three frames of the camera page (a loading page draws
one or two). Closing the camera navigates the server back to the dashboard
address, or `about:blank`.

Camera sound does not use the stream: the panel calls `media_player.play_media`
(announce) on its own speaker entity with go2rtc's RTSP output; Home
Assistant's ESPHome ffmpeg proxy converts it to the speaker's FLAC format.

## Not ported

The base project's step/press gestures, timers, artwork modes, sensor graphs and 20-slot
entity editor are not part of the rectangular firmware. Its full emoji/font
pipeline is not used; text uses built-in Montserrat glyphs.
