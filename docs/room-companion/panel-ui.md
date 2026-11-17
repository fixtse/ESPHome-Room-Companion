# Panel screen behaviour

The rectangular panels run the shared application and add a thin panel
layer on top (`onx_panel_ui.h`, `packages/room_companion/panel.yaml`).

## Gestures

| Where | Gesture | Result |
| --- | --- | --- |
| Carousel | Swipe left / right | Previous / next control |
| Carousel | Tap an icon in the bottom strip | Jump to that control |
| Detail view (weather, light…) | Swipe left / right or up / down | Step the value (pages, brightness, volume) |
| Media player | Swipe left / right | Close the player without changing playback |
| Media player | Swipe up / down | Change volume |
| Detail view | Swipe right **starting at the left edge** | Back |
| Anywhere in the native UI | Swipe down **from the top edge** | Open the notification centre (the only way on detail screens) |
| Dashboard stream | Swipe left / right | Back to the native UI |

## Header, notifications and dashboard

- **No top-corner buttons on details.** Detail screens (media, lights, sensors…)
  start at the top edge.
- **Bell: main view only.** The main carousel keeps a slim row with a plain bell
  icon (no background), dimmed when there is nothing and showing the unread
  count. On every screen, **pull down from the top edge** to open the
  notification centre: list, detail with picture, dismiss and clear all. A
  banner announces new notifications. Home Assistant sends them with the
  device's *show message* action.
- **Dashboard: a bottom-bar item.** Add *Dashboard* (listed under “On this
  panel”) as a control; it appears in the bottom bar and opens the stream once a
  Remote WebView server is configured.

## Detail screens

Opening a control shows a full-screen detail with no header (`onx_panel_detail.h`).
Each type has a direct touch control next to the usual swipe/knob step:

| Type | Screen |
| --- | --- |
| Light | Vertical brightness slider (drag, sent on release), power button, Brightness / White / Colour chips (the swipe mode) and white-temperature or colour swatches |
| Climate | Target arc (drag), − / + steps, HVAC mode chips, preset / fan / swing row (tap for the next option) |
| Fan | Power button, speed bars (tap a bar), oscillate / direction / preset rows when supported |
| Cover | Window with the blind drawn to the position (drag when position is supported), Open / Stop / Close |
| Switch, input boolean, automation | Large toggle; plugs with a power sensor show its reading and graph |
| Scene, script, button | Large run button with a confirmation ring |
| Timer | Progress ring and countdown; HA timers add +1 min, Pause/Resume and Cancel |
| Sensor | Large value, 12 h trend, graph (when enabled) and min / avg / max of the same history |
| Binary sensor | State disc and the last changes seen by the panel since boot (history is not fetched) |
| Number | Large value, horizontal slider (sent on release) and − / + steps |
| Select | Scrollable option list; tap selects immediately |
| Weather | Current conditions, humidity / wind / rain and a five-day range list on one screen |

Media keeps its artwork/transport view. Taps outside a control do nothing on
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

- **Idle screen**: *Clock* (default), *Photo slideshow* or *Dashboard*. The idle
  screen appears after the standby timeout.
- **Slideshow pictures**: picture addresses separated by spaces, commas or
  new lines (JPEG/PNG over HTTP, e.g. `http://homeassistant.local:8123/local/photos/1.jpg`).
  An address that returns a different picture each time (a camera snapshot or a
  random-photo service) also works on its own.
- **Slideshow interval**: seconds per picture (5–600).

*Dashboard* opens the stream when the panel goes idle; it needs a configured
server and is limited by the stream's frame rate.

## Not yet done

- Configurable page layouts (two controls per page, or one control plus three
  sensor values). Base pages are still one control per page.

## Rectangular pages and grouped items

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
