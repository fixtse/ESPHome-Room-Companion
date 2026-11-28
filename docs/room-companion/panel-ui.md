# Panel screen behaviour

The rectangular panels run the shared application and add a thin panel
layer on top (`onx_panel_ui.h`, `packages/room_companion/panel.yaml`).

The carousel uses one `normal_panel_page` host and the native card renderer.
The retired `device_0`–`device_9` and Settings body widgets are no longer
allocated or refreshed. Shared selection, entity state, subscriptions and
control actions remain independent of card rendering. Opening the notification
centre pauses native card updates until it closes; it does not activate a
second carousel renderer.

## Gestures

| Where | Gesture | Result |
| --- | --- | --- |
| Carousel | Swipe left / right | Previous / next control |
| Carousel | Tap an icon in the bottom strip | Jump to that control |
| Detail view (weather, light…) | Swipe up / down | Step the value (pages, brightness, volume) |
| Media player | Swipe left / right | Close the player without changing playback |
| Media player | Swipe up / down | Change volume |
| Detail view | Swipe left or right anywhere (not starting on a slider or scrolling row) | Back to the main view |
| Anywhere in the native UI | Swipe down **from the top edge** | Open the notification centre (the only way on detail screens) |
| Notification centre | Swipe up starting on the bottom handle area (*Swipe up to close* and the line below it) past a fifth of the screen, or flick up; or tap the handle. Swipes over the list only scroll it | Close it; a sheet follows the finger and springs back from a shorter drag |
| Dashboard stream | Swipe from the left edge | Back to the native UI |

## Header, notifications and dashboard

- **No top-corner buttons on details.** Detail screens (media, lights, sensors…)
  start at the top edge.
- **Bell: main view only.** The main carousel keeps a slim row with a plain bell
  icon (Material Design *bell-outline*, no background), dimmed when there is
  nothing and showing the unread count. On every screen, **pull down from the
  top edge** to open the notification centre. Its header shows
  *Notifications · count* and *Clear all*; each card has an accent bar while
  unread, a thumbnail with its time underneath (*now*, *5 min*, then the time
  of day), a full-width text-column title, up to two lines of the message, and ×
  to dismiss. Tap a card for its detail with the small title thumbnail. Swipe up from the handle (or tap it) to close: the centre moves with the finger, the handle reads
  *Release to close* once the drag is far enough, and it closes only when the
  finger lifts. A shorter drag springs back. A banner announces new notifications. Home Assistant sends
  them with the device's *show message* action.
- **Settings uses the main-page base.** The carousel's Settings item is a card
  on the same background, with the time row, bell and status icons, like the
  other main pages. Tap it to open the settings menu.
- **Dashboard: a bottom-bar item.** Add *Dashboard* (listed under “On this
  panel”) as a control; it appears in the bottom bar and opens the stream once a
  Remote WebView server is configured.

## Detail screens

Opening a control shows a full-screen detail with no header (`onx_panel_detail.h`).
Each type has a direct touch control next to the usual swipe/knob step:

| Type | Screen |
| --- | --- |
| Light | Home Assistant style: name, large brightness and state, one large control for the selected mode (vertical brightness slider, colour-temperature slider, or colour wheel) and a bottom pill bar with power and the mode buttons. Drags preview live and are sent on release |
| Climate | Target arc (drag), − / + steps, HVAC mode chips, preset / fan / swing row (tap for the next option) |
| Fan | Power button, speed bars (tap a bar), oscillate / direction / preset rows when supported |
| Cover | Window with the blind drawn to the position (drag when position is supported), Open / Stop / Close |
| Switch, input boolean, automation | Large toggle; plugs with a power sensor show its reading and graph |
| Scene, script, button | Large run button with a confirmation ring |
| Timer | Progress ring and countdown; Local Assist timers support Pause/Resume and Stop; HA timer helpers add Pause/Resume, +1 min and Stop. With two or more running timers it opens as a list (rows in portrait, a 2-column grid in landscape) with each timer's progress, time left and a Pause/Resume (or Stop) button; finished timers come first. Tap a timer for its full screen, and the list button to return |
| Sensor | Name (two lines when long), large value with its unit, a level bar for percentages, 12 h trend, graph (when enabled) and min / avg / max of the same history |
| Binary sensor | State disc (Home Assistant device-class wording) and the last changes the panel has seen; kept in flash across reboots (saved at most every 10 minutes), not fetched from Home Assistant |
| Event | Latest event type and how long ago it fired, plus recent events (kept across reboots like binary sensors). Icon follows the device class (doorbell, button, motion). Events are read-only |
| Siren | Power plus the advertised options: tone (tap to cycle), volume and duration steps. Choices are stored per siren and sent with the next turn-on |
| Camera | Live MJPEG stream (via Home Assistant's `/api/camera_proxy_stream/`, falling back to snapshots on main cards), or full-frame-rate video from the WebRTC Camera integration, Frigate or go2rtc through the Remote WebView server when configured ([live cameras](dashboard-stream.md#live-cameras)): tap the live picture for full screen (turned for a portrait screen; tap or swipe back to close), a speaker button for the camera's sound, and refresh reloads the stream (reloaded automatically when no video shows or the picture freezes). Otherwise tap to refresh the snapshot. A camera reporting `off` shows **Camera is off**. Icon buttons for refresh, power (`ON_OFF`) and motion detection, plus an optional **ONVIF PTZ** icon bar (pan, tilt, zoom via `onvif.ptz`) configured per camera in the web UI |
| Vacuum | Status badge or battery ring (`battery_level`), icon buttons for play/pause, stop, dock, locate and spot clean based on `supported_features`, a fan speed list (`vacuum.set_fan_speed`), the map (camera or image entity) and a room list that cleans rooms in the tapped order ([vacuum-rooms.md](vacuum-rooms.md)) |
| Lock | Status disc (or on-screen PIN keypad when `code_format` is set) with Lock, Unlock and optional Open (`OPEN` feature) buttons; main cards and compact rows expose direct lock/unlock controls when no code is required |
| Alarm (`alarm_control_panel`) | Status disc or on-screen numeric PIN keypad when `code_format` is set, with Disarm (while armed/triggered) or supported Home / Away / Night arm buttons |
| Number | Large value, horizontal slider (sent on release) and − / + steps |
| Select | Scrollable option list; tap selects immediately |
| Weather | Place name with the condition icon in the top-right corner, large temperature with conditions, feels-like and today's range beside it, humidity / wind / rain tiles and a five-day list (day, conditions, low, range bar, high) |

Local Assist timers offer Pause/Resume in the list and focused view, with Stop in the focused view.
These buttons call `conversation.process` with the built-in Home Assistant
agent (`conversation.home_assistant`) in English, selecting the timer by name (or original duration when
unnamed). Allow this device to perform Home Assistant actions in its ESPHome
integration options. Home Assistant resolves matching timers and sends the
updated state back; duplicate names/durations may require a voice command to
disambiguate. Remote voice/Google timers remain controlled on their source
device. Countdown refresh waits through touch release so list navigation and
buttons keep their pressed widget until LVGL dispatches the click. Countdown
labels and rings update in place; ticks and command feedback do not recreate
the buttons. Cancelled/finished Assist timers are removed from the mirror inside
the event callback, so a finished timer appears only as its ringing alert. Stop
cancels running/paused timers in Home Assistant and waits for the authoritative
cancellation; closing the overlay with Back only dismisses the view. Command
errors or missing confirmation are shown in the timer's status line.

The main-screen timer chip is an LVGL passthrough target, so it cannot also press
the entity underneath it. Timer and media auto-popups are suspended while the
shared timer overlay owns the screen; closing the chip-opened view returns to
the main screen. External timer helper commands keep the visible timer as their
dispatch target even when the underlying carousel has another entity selected.

Media keeps its artwork/transport view, with the elapsed and total time at the
ends of the progress bar; without artwork the placeholder icon is centred in the
artwork area. Taps outside a control do nothing on
these screens; the left-edge back swipe takes priority over controls near that
edge. In landscape the visual (slider, arc, window…) sits on the left and the
name and controls on the right. Commands use the shared control queue, so the
panel shows the requested value until Home Assistant confirms it.

**Hardware status:** configuration/compile validated only; the screens,
gestures and HA actions still need review on a physical panel.

## Accent color

**Device settings → Accent color** (formerly *Ring color*) tints every
highlighted element: progress bars, the media play button, the
selected bottom-bar item and its marker, graphs, timers and the notification
banner. Presets and custom colors are saved with the device settings.

## Orientation

These panels have no accelerometer, so **Device settings → Display orientation**
chooses *Portrait*, *Landscape*, *Portrait · upside down* or *Landscape ·
flipped*. Saving a new orientation restarts the panel; the display controller
is turned in hardware (MADCTL) and the touch coordinates are mapped before the
display, touchscreen and LVGL start, so landscape is a native 480×320 (3.5")
or 320×240 (2.8") screen with no software rotation cost.

In landscape, grouped pages place two controls side by side and the three
sensors in a column to the right of the main control; the media player puts
the artwork on the left with title, volume, progress and transport on the
right. The bottom bar and gestures work the same way. Screens still drawn by
the square base layout (e.g. settings) remain centred. The configuration
preview follows the selected orientation. **Hardware status:** landscape has
been validated by configuration/compile and browser tests only; it has not yet
been verified on a physical panel (display direction, touch mapping).

## Idle screen

Home Assistant entities (device configuration):

- **Idle screen**: *Clock* (default), *Dashboard* or *ImmichFrame*. The idle
  screen appears after the standby timeout.

The *Clock* uses the whole screen. Wi-Fi and Home Assistant status sit at the
top left and the notification count at the top right. Below the clock and date
it shows the weather (condition, temperature and humidity, or the configured
clock sensors), today's high, low and chance of rain, a daily forecast (from
`weather.get_forecasts`, refreshed every 30 minutes) and the media player that
is playing. Landscape puts the clock and media on the left and the weather and
forecast in a right-hand column. Missing parts are hidden. Timers are not
shown: the timer screen covers the clock while one runs.

Choose what the clock shows in the configuration page under **Device settings →
Clock & idle screen → Clock screen content**: a preset (*Time only*, *Time &
weather*, *Weather & forecast*, *Everything*) or any mix of current weather,
today's high/low/rain, daily forecast and playing media. The preview updates as
you choose. The layout uses the room it has: with fewer items the clock switches
to larger digits, the weather values and forecast icons get bigger, and the
spacing grows; when space is short they shrink in steps before anything is
dropped.
- **Seconds per photo** (5–600) and **fade** are set on the web setup's
  Idle screen page with the other ImmichFrame options.

The web setup's **Idle screen** page shows only the options for the selected
screen: clock layout and formats for *Clock*, a note for *Dashboard*, and the
server and photo options for *ImmichFrame*. Weather and the two readings apply
to both the clock and the photo overlay. Firmware before this change also had a
*Photo slideshow*; it has been removed, so re-select the idle screen after
updating if it changed.

### ImmichFrame

Run the ImmichFrame fork with the OpenNextion JPEG endpoint. Open the panel's
web setup, open the **Idle screen** page and choose **ImmichFrame**. It searches the network for the
server (mDNS, like the Home Assistant page; run the container with host
networking) when no address is set, or when you press search. Pick the server
or enter its address (for example `https://frame.example.net`), then save.
Once saved, the page shows the server as a card with its reachability and
settings. Home Assistant keeps only **Idle screen**; the other photo settings
are on that page. The panel requests one photo per interval using its screen
orientation. ImmichFrame uses its configured album and account filters.

Photo options (panel web setup, Idle screen page):

- **Clock on photos** (each choice shows a small picture of the result):
  - *Large clock* (default) shows the time, date, weather and playing media
    over a fade at the bottom.
  - *Small clock* uses a smaller time and date in a thinner band, so more of
    the photo shows.
  - *Photo only* shows just the time, small, in a corner pill.

  **Hardware status:** compiled; appearance not yet checked on a panel.
- **Photo fit**: *Blurred background* (default) shows the whole
  photo over a blurred, darkened copy of itself, as ImmichFrame does when
  `ImageFill` is off. *Fill screen* crops the edges, and *Whole photo* uses black
  bars.
- **Show two photos** (on by default): ImmichFrame's split view. A
  portrait photo on a landscape screen is shown beside a second portrait photo,
  and a landscape photo on a portrait screen is stacked with a second landscape
  photo. A photo is shown alone when its size is unknown or the next photo does
  not match. In that case, the unmatched photo is shown next.
- **Photo details** (date, place, album, people, description): which photo
  details ImmichFrame draws in the top-left corner of each photo (at most three
  lines, in that order). Date and place are on by default; turn all of them off
  for a clean photo. The date and place formats follow ImmichFrame's
  `PhotoDateFormat`, `Language` and `ImageLocationFormat`.
- **Fade in each new photo** (on by default): each new photo fades in from black.

The server composes the photo, so the panel still downloads one screen-sized
JPEG per interval. ImmichFrame's zoom and pan (Ken Burns) effects are not
available because animating a full-screen photo is too slow on the SPI
display.

On both photo idle screens, swipe left for the next photo and right for the
previous one. A tap or a vertical swipe leaves the idle screen. ImmichFrame
remembers its last 20 screens for going back.

Both photo idle screens show the idle clock's information in a band at the
bottom: a large clock, with the date and current weather (from the idle clock's
weather entity) on the line beneath it and the playing media above it. The clock settings that hide weather or
now playing apply here too.

If ImmichFrame uses `AuthenticationSecret`, add the full
`Bearer <AuthenticationSecret>` header to your private `secrets.yaml` and
override the firmware substitution:

```yaml
substitutions:
  immichframe_authorization: !secret immichframe_authorization
```

Use the final HTTPS address for authenticated requests. Keep this credential in
a private build. The photo client does not follow redirects.

*Dashboard* opens the stream when the panel goes idle; it needs a configured
server and is limited by the stream's frame rate.

The clock shows the number of notifications in a pill at the bottom centre
while there are any.

## Assist screen

Laid out like Home Assistant's voice dialog: the microphone in accent rings
(tap to talk again or stop), the status (*Listening…*, *Thinking…*,
*Speaking…*), what was heard as a bubble on the right and the reply on the
left, a hint (*Say your wake word or tap the microphone* while the wake word is
on), then a mute button (highlighted while muted) and *Stop*. A swipe from the
left edge also stops and closes it.

With the wake-word firmware, choose **Okay Nabu**, **Hey Jarvis**, **Hey
Mycroft** or **Kenobi** in the device's **Wake word** setting in Home
Assistant. ESPHome listens for one selected phrase at a time.
Turn on **Wake word enabled** and leave the microphone unmuted for local
detection. Okay Nabu is the default on a fresh setup. **Wake word sensitivity**
adjusts the Okay Nabu/Jarvis/Mycroft thresholds; Kenobi retains its model default.

**Stop** is an internal contextual model: it silences a ringing timer or stops
an announcement after its first second. Activation phrases also stop a ringing
timer or interrupt a reply. **Snooze** is enabled only while a local timer rings
and **Timer snooze enabled** is on. It silences the alert and rings again after
**Timer snooze duration** (1–30 minutes, default 5); it does not restart HA's
completed timer. A newer timer alert supersedes a pending snooze. Microphone
mute and **Wake word enabled** gate all local word detection.

### Sound sensing and dynamic volume

The shared application exposes the SoundSense-compatible **Ambient Sound Level**,
**Sound Detection**, **Dynamic Volume**, **Dyn Minimum Volume**, **Dyn Volume
Level** and **Presence Threshold** entities. **Sound sensing** is on by default;
**Dynamic Volume** is off by default. Turning off microphone capture via
**Microphone muted** also stops this feature's microphone listener. Sound sensing
can operate while the wake-word switch is off. It does not upload or retain PCM.

This panel-native implementation uses the PDM microphone's 16-bit PCM, rather
than the upstream package's 32-bit sample assumption. Audio callbacks only
compute a scalar RMS value; the main loop publishes sensors and applies bounded
volume changes. Measurements pause during Assist and playback to avoid feedback;
stale or paused measurements report unknown, not silence. The relative dB scale
has full scale at 90 dB and is not calibrated sound-pressure level. Tune
**Sound Silence Threshold** (default 30) and **Presence Threshold** (default 50)
to the room. States are quieting, silence after 15 seconds, active, noise after
a sudden rise, and presence after 12 seconds above its threshold. Dynamic volume
adjusts idle volume within 0–100%, with a floor set by **Dyn Minimum Volume**;
it never changes volume during playback or while muted.

## Not yet done

- Configurable page layouts (two controls per page, or one control plus three
  sensor values). Base pages are still one control per page.

## Rectangular pages and grouped items

**Main page cards.** Main pages show rounded cards with the time in the top
row. Cards carry direct controls: lights have an on/off switch, the brightness
percentage and a brightness slider; climate shows the mode and current
temperature, the target and − / +; media players show the track with previous,
play/pause and next; switches, fans and automations have a switch; scenes,
scripts and buttons a *Run* button; weather shows the temperature, conditions,
its icon and up to six readings (humidity, wind, pressure, visibility, UV,
feels like). Sensors in *One control + three sensors* pages are single rows
with icon, name and value. Tap a card outside its controls to open the detail.
**Hardware status:** compiled; appearance and touch behaviour still need
review on the panel.

Normal entity pages now use the full display above the navigation dock, below
a slim row for the notification bell. Generic
entity details use the full height; numeric progress is a straight bar rather
than a circular ring.
Media retains its artwork and transport view, with artwork sized after reserving
space for the text and controls. The configuration preview uses the panel's
actual dimensions, including the outer frame, instead of a 240×240 circle.

In **Display & actions → Display → Page layout**, choose the layout for the
selected control's main page:

- **One control**: the selected entity occupies the page (also used for a sensor
  or other read-only entity on its own).
- **Two controls**: the selected control and another configured control occupy
  two stacked areas.
- **One control + three sensors**: the selected control appears above three
  compact sensor/binary-sensor readings.

Add and save the entities in the normal slots first, then choose the other page
items and select **Save page layout**. Layout saving is separate from saving an
entity slot. Items reuse those slots' existing Home Assistant subscriptions and
control logic; page membership does not create additional subscriptions or
silently change the configured entities. A member may still have its own page.
Tap an area to open that item's normal controls, swipe horizontally to change
pages, and use the left-edge back gesture to return to the grouped page. Sensor
areas also open their existing detail/graph views. Holds and swipe adjustments
continue through the existing input router.

Page layouts are stored in NVS by the primary entity's identity, so rebooting or
reordering slots does not reassign a layout to an unrelated entity. Duplicate
members, unconfigured entities and non-sensors in sensor positions are rejected.
Removing a configured member displays an explicit missing-item state until its
page configuration is updated. Layouts are device settings, independent of the
existing entity preset/backup documents.

The layout model is `onx_panel_layout.h`; the panel renderer, persistent bindings
and page RPC handlers are in `onx_panel_pages.h`. `panel_pages` reads the saved
bindings and `panel_page_save` validates/persists one page. Layout state is kept
in PSRAM. The browser's panel extension implements matching portrait and landscape geometry.
Run `mise exec -- bash scripts/test_panel_pages.sh` on devserv for geometry,
validation, tap-versus-swipe routing and browser editor/preview tests.

### Validation on 2026-11-17

Work ran under the task lease in
`192.168.31.35:~/agent-work/room-companion-pages-20261117`, based on `8199e15`
plus the synchronized working changes. Source hashes matched locally and on
that checkout, including the generated firmware header copy. Commands run there:

```sh
mise exec -- bash scripts/test_panel_pages.sh
mise exec -- bash scripts/test_config_web.sh
docker run --rm -v "$PWD":/config ghcr.io/esphome/esphome:2026.10.2 config ONX2432G028.yaml
# Use a unique container/cache name and clean stale copied framework metadata.
docker run --rm -v "$PWD":/config -v rc-pages-pio-20261117:/root/.platformio \
  ghcr.io/esphome/esphome:2026.10.2 compile ONX3248G035.wake-word.yaml
```

The native geometry/validation/touch tests and both browser suites passed.
The 2.8-inch model validated; only the 3.5-inch wake-word firmware was compiled,
following the requested model restriction. Final build timestamp:
`2026-11-17 13:25:32 UTC`; YAML config hash: `0x02b96553`.

The connected 3.5-inch board passed USB flash hash verification, boot, repeated
Wi-Fi HTTP 200 checks and a real device API persistence test: a temporary
two-control page was saved, survived a hardware reset, and its original page
configuration was restored afterward. No HA entity actions were invoked by that
test. UART observation showed no panic/abort, approximately 45 KB internal heap
free and settled maximum-loop samples of 84, 86 and 92 ms. Startup, icon downloads
and configuration writes produced larger peaks (including 2474 ms during the
configuration test); these are not normal interaction latency measurements.
Unchanged native cards are cached, and the covered base-layout body is hidden while the
rectangular page is displayed to avoid redundant rendering and touch handling.

Hardware verification covers boot, networking and saved layout persistence.
The browser geometry was visually inspected; the actual screen appearance,
physical gestures, audio and all entity combinations still require device review.
Build/test logs remain in `~/agent-work/room-companion-pages-*.log` on devserv;
private UART/flash captures and firmware are outside Git in
`/tmp/room-companion-pages-20261117/` on the attached PC.

### Entity view follow-up (2026-11-17)

Sensor history has 1 h, 12 h, 24 h and 7 d ranges, requested through the shared
HA recorder action. Recorder statistics must exist for the selected entity;
an empty response is not fabricated into a graph. Changing range resets the
bounded 73-point buffer. Automation views offer run and enable/disable; running
scripts offer stop. Configured quick actions are visible in a horizontally
scrollable row, and the media detail has a list button for its configured actions.

Album art stays square with rounded corners. The visible main-page media card
uses the shared, memory-guarded artwork downloader, with the entity icon while
loading or when no image is supplied. The first media card owns that single
artwork buffer if a page contains multiple media players. Persistent image
widgets detach before a new download or buffer release.

Build validation for this follow-up used ESPHome 2026.10.2 on devserv in
`~/agent-work/rc-assist-web-fix-20261117b/src`, the isolated checkout retained
from this task. Its Git base was `c3c23a1`; the complete local source at
`49856a9` plus the current changes was synchronized before compilation.
The final `ONX3248G035.wake-word.yaml` compile passed (config `0xd2679430`,
build time `2026-11-17 17:31:49 UTC`, RAM 167472 B, flash 6962575 B).
The tested `onx_panel_detail.h` SHA-256 begins `df2183c4e2477615`; the factory
image SHA-256 is
`2973d9c61723b0cc18fd1a21989a74800c0aac01eecdbcc5497ac785a9d859bd`.
The remote sensor-range test and `scripts/test_panel_pages.sh` passed.
Only the 3.5-inch model was compiled, following the user's model restriction.
These checks do not certify physical appearance, gestures or audio operation.

The factory image was flashed over USB at offset zero to MAC
`80:b5:4e:db:c6:e4`, with `Hash of data verified.` and no flash erase.
A 100-second UART capture at 115200 baud (DTR/RTS false) showed one initial
power-on boot and no panic, abort, watchdog or repeated reset. Nine SPI DMA
private-TX-buffer allocation errors occurred only at 6.68–6.90 seconds after
boot. After startup/download peaks, the final loop samples were 23–24 ms
with 43.2–43.4 KB internal heap free. HTTP returned 200 repeatedly at the
current Wi-Fi address. These are boot/network checks, not visual or audio
certification. The task's retained devserv lease was released once after all
remote builds and tests completed. Private logs and firmware remain outside Git.

### Main-page action dispatch fix (2026-11-17)

Main-page buttons queued shared control commands, but the 100 ms dispatcher
selected an entity only while a detail page was active. With the main page
visible it passed an empty entity to `onx_controls::tick`, which canceled the
command before transmission. The rectangular panel now supplies a visibility
hook so the dispatcher can send a command for any configured card on the
visible main page, including secondary controls. Detail dispatch stays tied to
the selected detail entity. Inactive screens, hidden cards, overlays and a lost
HA connection still prevent dispatch.

On devserv, in the same isolated task checkout described above,
`python3 -m unittest discover -s tests -p test_entity_controls.py` passed with
regressions for primary/secondary main controls and cancellation after leaving
the page, sleeping or disconnecting. The existing `test_touch_input.cpp` harness
also passed. ESPHome 2026.10.2 compiled `ONX3248G035.wake-word.yaml` successfully
(config `0x18071f0a`, build `2026-11-17 17:40:06 UTC`; RAM 167496 B, flash
6962927 B). Local source was `49856a9` plus the working changes; the tested
`onx_entity_controls.h` SHA-256 begins `15f57815357db4e1`. Factory image SHA-256:
`eadf70d7568f9c5e3ac465ba4b72e5b1ff8c0b7ac1952da84a6d813c5bb40f75`.
Only the requested 3.5-inch model was compiled.

USB flashing at offset zero verified successfully on the expected MAC, and
HTTP returned 200 repeatedly. The first 100-second UART capture contained one
initial power-on boot and no panic, abort or watchdog. Nine DMA allocation
errors occurred during startup. Artwork decoding caused a 2.29 s loop peak;
the last captured loop sample was 219 ms, so this run does not establish a
settled loop-time bound. A separate 25-second capture began with another
power-on boot after reopening the serial port, despite DTR/RTS being set false;
no further serial reopen was attempted. Startup also logged empty-image decoder
and wake-word ring-buffer warnings. Physical button taps and audio were not
verified. The devserv lease was released once after remote work completed.

### Web preview refresh (2026-11-17)

The configuration webpage now renders domain-specific rectangular previews
instead of spreading the old rotary text lines over a tall card. Weather has
readings and forecast rows; main cards show light/climate controls and media
artwork with rounded corners; sensor details show history ranges and summaries.
The preview API supplies structured readings, forecasts and configured action
names from the same shared entity/configuration state used by the panel. Missing
readings remain unavailable, rather than being replaced by sample values.

On devserv in `~/agent-work/rc-assist-web-fix-20261117b/src` (Git base `c3c23a1`,
synchronized local `49856a9` plus working changes), `scripts/test_panel_pages.sh`
passed, including the startup check and weather temperature/forecast checks at
both model sizes in portrait and landscape. SVG screenshots were inspected.
ESPHome 2026.10.2 compiled `ONX3248G035.wake-word.yaml` successfully (config
`0x85621fcf`, build `2026-11-17 17:53:45 UTC`; RAM 167496 B, flash 6972919 B).
The tested `panel_web.h` SHA-256 begins `0abc13d82f225e80`; factory image SHA-256:
`36d12f5a6fb438fc42c202794aeec2d5df9636dd7af2ffcbd33bf17af776c2a1`.
Only the requested 3.5-inch firmware was compiled; browser geometry checks
covered both sizes.

### Sidebar, Home Assistant discovery and remembered sign-in

The webpage sidebar collapses to an icon rail on desktop and a menu button on
mobile. Its state persists in browser local storage; navigation labels remain
available to assistive technology.

The panel discovers `_home-assistant._tcp` instances using ESP-IDF's asynchronous
mDNS query API. A bounded 2.5-second search collects up to four instances without
waiting for network responses on the LVGL task. The browser searches on first
load when signed out and offers a Find Home Assistant button. It fills a blank
or default address for a single result, offers choices for multiple results,
and preserves manually entered addresses. Multicast must reach the panel; manual
address entry remains available. Discovery does not authenticate or change the
configured host until the user chooses to sign in.

OAuth credentials now persist in browser local storage, while authorization
redirect state and PKCE remain in session storage. Existing session-only logins
migrate automatically. Expired access tokens renew through the stored refresh
token; Disconnect browser removes both the persistent login and any legacy
session copy. Persistence is scoped to the same browser profile and panel
origin: using a different IP/hostname/port, private browsing, clearing site data
or revoking the HA refresh token can require sign-in again. Credentials are not
copied to the firmware or repository.

Validation used the same isolated devserv checkout and source base noted above.
`node tests/test_browser_catalogue.cjs`, the native `test_config_http.cpp` harness,
and `scripts/test_panel_pages.sh` passed. Regressions cover session-to-local
migration, returning with empty session storage, access-token renewal, explicit
logout, sidebar persistence, discovery autofill and preserving a manual address.
The final ESPHome 2026.10.2 build of `ONX3248G035.wake-word.yaml` passed (config
`0x85621fcf`, build `2026-11-17 18:03:22 UTC`; RAM 167728 B, flash 6979611 B).
The tested `ha_browser.h` SHA-256 begins `361f4eb960741f4a`; `ha_discovery.h` begins
`5da7ccf7a0f18d42`. Factory image SHA-256:
`98b009312c6626eec8f7bf69583f39504395d65bc031c3d55a0b83d52e5d7cc9`.
USB flashing verified on the expected MAC. Live requests confirmed the updated
HTML and all three scripts, structured weather preview data, and successful
mDNS discovery of one Home Assistant instance on the panel's network. The
devserv lease was released once after all remote work completed.

The final 100-second UART capture showed one initial boot and no panic, abort,
watchdog or repeated reset. Eight DMA allocation errors occurred during startup.
The final loop samples were 310–342 ms while the webpage/runtime was active,
with about 42 KB internal heap free; this is not a settled performance
certification. The HTTP endpoint remained reachable with status 200. Firmware,
private runtime logs and discovery results remain outside Git.

### Settings and idle transition flashes (2026-11-17)

The rectangular card layer could disappear while the base carousel was still
animating toward Settings, exposing stale entity widgets. Rectangular carousel
page loads now finish directly before the card layer is hidden for Settings or
other non-entity entries; the underlying carousel animation is no longer used.

Idle entry previously hid the card layer on its 100 ms tick, while the clock
page was loaded by a separate once-per-second refresh. That gap exposed the old
weather/entity page. The clock refresh is now a shared script, called at idle
entry before hiding the card layer and reused for periodic updates. Its weather
readings, font, time and date are populated before loading the clock page.

On devserv in `~/agent-work/rc-assist-web-fix-20261117b/src` (Git base `c3c23a1`,
synchronized local `49856a9` plus working changes), the standby-scope unit test
and native touch/layout harnesses passed. ESPHome 2026.10.2 compiled
`ONX3248G035.wake-word.yaml` successfully (config `0xd4999ba7`, build
`2026-11-17 18:16:47 UTC`; RAM 167760 B, flash 6980847 B). Only the requested
3.5-inch model was compiled. Tested `normal_mode_scripts.yaml` SHA-256 begins
`aa32fd9c25903fd8`; `panel.yaml` begins `ff6e68991cd532a3`. Factory image SHA-256:
`9081854678fe29347245af74df5e293cf49af2e374818e30ae819827bc503d31`.
These checks do not certify physical transition appearance.

The factory image was flashed at offset zero to MAC `80:b5:4e:db:c6:e4` with
`Hash of data verified.` and no erase. A single continuous 100-second UART
capture (115200 baud, DTR/RTS false) showed one initial boot and no panic, abort,
watchdog or repeated reset. Nine DMA allocation errors were limited to startup;
the final loop samples were 35, 147 and 69 ms, with about 43 KB internal heap
free. HTTP returned 200 repeatedly. The devserv lease was released once after
remote work completed. Physical transition appearance still needs user
confirmation; private logs and firmware remain outside Git.

### Shared background colour (2026-11-17)

Settings previously used the legacy Dark mode palette while rectangular cards
kept their own colours. One shared saved background now replaces that split.
The webpage offers a Panel background colour picker in Device settings; the
on-panel Settings menu offers Background presets. Both use a separate versioned
NVS preference, preserving the existing accent, clock, orientation and slot
records. The former Dark mode switch/menu item has been replaced.

Main and entity pages, Settings, clock and legacy overlays use the same
background, derived card/control surfaces, and light/dark foreground selection.
Screen-load hooks theme hidden pages before their first frame; per-screen
palette records also support a page left hidden across many colour changes.
Artwork pixels and accent colours are preserved. The webpage preview uses the
same background/surface/foreground calculation and updates before saving.

Validation ran in the existing isolated devserv task checkout
`~/agent-work/rc-assist-web-fix-20261117b/src` (Git base `c3c23a1`, synchronized
local `49856a9` plus working changes). The display-settings harness, production
palette mapper tests, HTTP harness, browser catalogue tests, and
`scripts/test_panel_pages.sh` passed. Tests cover light/dark backgrounds,
idempotent mapping, hidden-page colours after more than eight changes, accent
preservation, and editing the background picker before saving.

ESPHome 2026.10.2 compiled `ONX3248G035.wake-word.yaml` successfully (config
`0x2eb32bb6`, build `2026-11-17 18:45:06 UTC`; RAM 167928 B, flash 6986039 B).
Only the requested 3.5-inch firmware was compiled. Tested `onx_render.h` SHA-256
begins `49f8c881be81c6e5`; `onx_display_settings.h` begins `b82c71aeb9c22c0d`.
Factory image SHA-256:
`b9c452d61e02d207c262c75ba689a4565d10a738b72c2b8170d8c5666b55c290`.

Live API checks accepted and read back both light (`#F4F4F4`) and dark
(`#14283D`) backgrounds, rejected an invalid hexadecimal colour without changing
the saved value, and served the new picker. A final preview-only adjustment
keeps button symbols legible on light backgrounds; browser regressions passed
again. Final production build: ESPHome 2026.10.2, config `0x2eb32bb6`,
`2026-11-17 18:53:03 UTC`, RAM 167928 B, flash 6986215 B; final factory SHA-256:
`379a7f217e2f9b8c21d00acbfbc43a93cfedaf9708e30980b280f4c190f40142`.

### Panel feedback fixes (2026-11-17, evening)

- **Sensor graph jumping.** The base layout's periodic detail refresh still
  drew the shared graph canvas at its default size about once a second, so the
  graph alternated between two positions and sizes. While the rectangular detail
  owns the canvas (`onx_sensor_graph::panel_owned`), that refresh no longer
  hides, re-parents or resizes it.
- **Light brightness drag not applied.** The custom sliders chained the drag to
  their parents; LVGL's top layer is scrollable, so the drag became a scroll of
  the layer and the value was not reliably committed. Draggable controls now
  disable scroll chaining, as LVGL's own slider and arc do, and a lost press
  commits the last previewed value. The colour wheel sends `hs_color` as a
  template so Home Assistant receives a list.
- Settings, media times and centred placeholder, weather icon, bell icon, idle
  notification count, notification centre and Assist screen as described above.

**Hardware status:** configuration/compile validation only until checked on the
panel; appearance and touch behaviour of these screens are not yet
hardware-tested.

When a dashboard address is configured, its shortcut appears automatically at
the edge of the bottom toolbar. Choose Left or Right in the web page’s Home
Assistant dashboard settings; the position is saved on the panel. Clearing the
address removes the shortcut. The remaining carousel controls keep their order.


### Extending timers

The focused view offers **+1 min** for running and paused local Assist timers,
voice-satellite timers and Home Assistant timer helpers. Assist timers use the
built-in HA conversation agent's add-time intent; their paused state is kept
by HA. Helpers use `timer.change` while running. HA cannot change a paused
helper directly, so the panel starts it with its saved remaining time plus
60 seconds, waits for confirmation, then re-pauses it. This produces a brief
start/pause transition and may trigger HA automations listening for those events.
The view waits for HA updates and shows a confirmation error if the update or
pause restoration is not confirmed. Finished alarms do not offer +1 min.

### Cover position presets

Covers supporting position control show 15%, 25%, 50% and 75% by default. In the web configuration, select the cover and edit Position presets (up to four unique whole percentages from 0 to 100). Disable Show position presets to hide the buttons while retaining their values. An empty list also hides them. Values persist per cover entity and are included in configuration backups and presets. The web preview reflects draft edits.

The dashboard defaults to 0.75 zoom on the 2.8-inch panel and 1.0 on the 3.5-inch panel. A saved zoom takes precedence over these first-boot defaults.

The NS4168 amplifier starts disabled and is enabled only while the I²S speaker reports running, covering media, Assist and timer sounds. Cold-power startup noise must be checked on hardware separately from compilation.
