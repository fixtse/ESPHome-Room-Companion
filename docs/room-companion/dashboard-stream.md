# Streamed page (Home Assistant dashboard) on the panel

In dashboard mode, [RemoteWebViewServer](https://github.com/strange-v/RemoteWebViewServer) renders a web page (usually a
Home Assistant dashboard, but any page the server can reach) in headless Chromium and streams changed tiles to the
panel; touches go back to the server. The panel switches to its native screens
for setup, Assist, notifications and the corner menu, then returns to the
dashboard.

## 1. Run the server

Choose one installation method. The panel connects to the server over your local
network, typically on port `8081`.

### Home Assistant OS add-on

[![Add the Remote WebView Server repository to Home Assistant](https://my.home-assistant.io/badges/supervisor_add_addon_repository.svg)](https://my.home-assistant.io/redirect/supervisor_add_addon_repository/?repository_url=https%3A%2F%2Fgithub.com%2Fstrange-v%2FRemoteWebViewServer)

1. Open the button and confirm the repository in Home Assistant. You can also
   add `https://github.com/strange-v/RemoteWebViewServer` under **Settings →
   Apps → App store → Repositories**. Older Home Assistant versions call this
   section **Add-ons**.
2. Install and start **Remote WebView Server** from the store.
3. Use the Home Assistant host’s LAN address and the add-on’s `ws_port`
   (default `8081`) in the panel’s **Remote WebView server** field.

### Standalone Docker Compose

On a Docker host the panel can reach, open a local copy of this repository and
run the [included Compose example](../../deploy/remote-webview/compose.yaml):

```sh
cd deploy/remote-webview
docker compose up -d
```

`compose.yaml` builds on `strangev/remote-webview-server:1.3.0` and limits each
acknowledging panel to one outstanding frame, with a three-second recovery
timeout. It keeps the browser
profile in `./pw-data`, and turns on reduced motion, which makes SPI panels feel
much smoother. One server can drive several panels at their own sizes.

### Recommended server settings

These panels decode and draw a full 320×480 frame in roughly a tenth of a
second, so the server should send small tiles and no faster than the panel can
draw. The Compose example already uses these values; for the Home Assistant
add-on, set the options of the same name in its **Configuration**:

| Setting | Value | Why |
| --- | --- | --- |
| Tile size (`TILE_SIZE`) | `32` | Small changes (a toggled tile) send little data. |
| Full-frame tile count (`FULL_FRAME_TILE_COUNT`) | `1` | Large changes go as one image instead of many tiles. |
| Full-frame area threshold (`FULL_FRAME_AREA_THRESHOLD`) | `0.5` | Above half the screen changed, send a full frame. |
| Full frame every (`FULL_FRAME_EVERY`) | `50` | Periodic full frame repairs any missed tile. |
| Every Nth frame (`EVERY_NTH_FRAME`) | `1` | Keep every frame; the interval below limits the rate. |
| Minimum frame interval (`MIN_FRAME_INTERVAL_MS`) | `120` | About what the panel can draw; faster only queues frames. |
| JPEG quality (`JPEG_QUALITY`) | `80` | Sharp text at a size the panel decodes quickly. |
| Max bytes per message (`MAX_BYTES_PER_MESSAGE`) | `61440` | Fits the panel's receive buffer. |
| Reduced motion (`PREFERS_REDUCED_MOTION`) | `true` | Fewer animation frames; much smoother on SPI panels. |

## 2. Sign the server's browser in to Home Assistant

For a dashboard that requires sign-in, [point the panel at the server](#3-point-the-panel-at-it)
and open the dashboard on the panel so the server opens the page. Then sign in
once through the server’s Chromium session.

1. Temporarily enable Chrome DevTools. In the add-on’s **Configuration**, set
   `expose_debug_proxy: true`, save, and restart the add-on. For standalone
   Docker, run `docker compose --profile devtools up -d` from
   `deploy/remote-webview`.
2. In desktop Chrome, open `chrome://inspect/#devices`, select **Configure…**,
   and add `<server-host>:9222` (or the add-on’s configured `debug_proxy_port`).
3. Select **inspect** on the dashboard tab, sign in, and enable **Keep me
   logged in**.
4. Turn DevTools off. Set `expose_debug_proxy: false`, save, and restart the
   add-on, or run `docker compose stop devtools` for standalone Docker. Anyone
   who can reach that port while it is open can control the browser session.

A dedicated, non-admin Home Assistant user for panels is recommended.

## 3. Point the panel at it

Open the panel's settings page (**Visit** on the device page in Home Assistant,
or the address on the panel's **More** screen) and its **Streamed page** tab.
The tab repeats these steps in short form.

- **Screen**: *Home Assistant dashboard*.
- **Remote WebView server**: `<server-host>:8081`. Restart when prompted.
- **Page address**: e.g. `http://homeassistant.local:8123/room-panel/0`.
  **Use my Home Assistant** fills in the Home Assistant the panel is connected
  to; add a dashboard path for a view made for the panel. After **Connect to
  Home Assistant** on the page, dashboards appear as suggestions. **Screen mode** in Home Assistant switches between modes.
- **Zoom** enlarges text and controls (restart to apply).

The stream connects the first time the dashboard is opened, not at boot, so a
configured but unused dashboard costs no internal RAM (see
[memory-budget.md](memory-budget.md)); the first open takes a moment longer.
Once started it stays connected until the panel restarts.

The server or zoom cannot change while the stream runs; the page shows a
**Restart needed** card. Presets switch the screen mode and dashboard address,
so e.g. a *Night* preset can show a dimmed dashboard or the native panel.

## Using it

- Swipe **left or right** to return to the native controls
  (Home, Music, Assist, More). **Back to dashboard** or 30 s without touching
  returns to the stream.
- **BOOT** or the **Talk** entity starts Assist from anywhere.
- If the server is unreachable, the native panel stays on screen and the
  **More** page and settings page show the stream status.

## Live cameras

The same server can play cameras at their full frame rate inside the native
camera screen, without a dashboard made for it. The camera screen keeps its
name, buttons and dock; only the picture comes from the stream.

On the **Streamed page** tab, set the **Remote WebView server** (the dashboard
**Page address** is not needed), then choose a **Camera source**:

- **WebRTC Camera integration in Home Assistant**: uses the go2rtc that
  [WebRTC Camera](https://github.com/AlexxIT/WebRTC) runs (built in or
  external). Each camera plays its own stream source; nothing else to set up.
  The panel registers a link with the integration's `webrtc.create_link`
  action each time a camera opens, and the server opens the integration's
  `/webrtc/embed` page for it, which needs no sign-in. Home Assistant pages
  cannot be framed, so this page is opened directly and the card's `style`
  option places the player over the camera frame. Requirements: the
  panel's **Allow the device to perform Home Assistant actions** option, and
  the server's browser must reach Home Assistant at the address set on the
  panel's Home Assistant page.
- **go2rtc server (Frigate or standalone)**: enter its web address, e.g.
  `http://192.168.1.10:1984` (for Frigate, publish its port `1984`). Each
  camera plays the go2rtc stream named like its entity: `camera.front_door`
  plays `front_door`, which is how Frigate names its cameras.

Under **Other stream names**, a line per camera can give a go2rtc stream name
(`camera.porch=porch_hd`), a stream address (`camera.shed=rtsp://…`), or a
full page address (`camera.garage=http://…`) to show in the camera frame
instead.

### Full screen and the camera switched off

Tap the live picture to fill the screen; tap again or swipe back to return.
On a portrait screen the stream page turns the video (clockwise by default;
**Full screen** under Live cameras sets the direction or keeps it upright), so
a landscape camera fills the screen while the panel keeps its orientation. Full
screen sends the whole screen as video, so it updates less often than the
camera frame (the panel decodes and draws a full screen per frame).

A camera that reports `off` shows **Camera is off** instead of a picture and
opens no stream; turning it on opens the stream again. On a live camera the
refresh button reloads the stream rather than the snapshot.

### Camera sound

Panels with a speaker (not the Lite builds) show a speaker button on a live
camera's screen. It asks Home Assistant to play the camera's sound on the
panel: `media_player.play_media` (as an announcement) with go2rtc's RTSP
output of the camera's stream, `rtsp://<go2rtc host>:8554/<stream>`. For the
WebRTC Camera source that is the integration's go2rtc on the Home Assistant
host, where the stream is named after the camera entity while its video plays.
Home Assistant's ffmpeg converts any audio codec for the panel.

- The sound runs a second or two behind the picture (Home Assistant buffers
  it); it is for listening, not lip sync.
- The panel needs its own Speaker entity's ID. Open the **Streamed page** tab
  while this browser is signed in to Home Assistant (Home Assistant tab): the
  page finds the entity of the device with the panel's MAC address and saves
  it. Otherwise enter it under **Panel speaker in Home Assistant**; without
  either, the panel guesses `media_player.<panel name>_speaker`, which misses
  names that include an area or were renamed.
- go2rtc's RTSP port (8554) must be reachable from Home Assistant. Leaving the
  camera screen stops the sound, as does Assist.
- Two-way talk is not supported (the microphone belongs to Assist).

How it works: when a camera screen opens, the panel asks the server to open a
small page the panel serves itself (`/panel/camera`), which places the
go2rtc player (WebRTC, else MSE) exactly over the camera frame. The server's Chromium
plays H.264 natively. The panel decodes the stream into a PSRAM buffer and the
native screen shows it as the camera image, so the stream never draws over the
native UI. Snapshots fill the frame until the video plays; Home Assistant's
MJPEG stream is not opened for a live camera.

- The server's browser must reach go2rtc (or Home Assistant) and the panel
  (port 80) on your network. Chromium only lets a page on the local network embed go2rtc, which
  is why the panel serves the page rather than the server opening go2rtc
  directly.
- The panel draws at most what it can decode (about 5–10 frames per second for
  the camera area, depending on the panel and the server's minimum frame
  interval); the camera itself is not limited.
- Video only: the panel has no audio path from the stream.
- When the camera screen closes, the server goes back to the dashboard page
  (or a blank page without one), which stops the camera stream.
- The dashboard and a live camera share the one stream: opening the dashboard
  ends the live view, and returning to the camera reloads it.
- Like the dashboard, the stream connects on first use and stays connected
  until the panel restarts.

## Designing a dashboard for these panels

These boards use SPI displays. Rough, unmeasured estimates: full-screen changes
reach about 5–10 frames per second on the 3.5-inch panel (320×480) and roughly
twice that on the 2.8-inch panel (240×320); small changes such as a toggled
tile are quick. Therefore:

- Build a dedicated view at the panel's size (portrait 320×480 or 240×320),
  with large tiles and no animated backgrounds, cameras or clocks with seconds.
- Prefer pages that fit without scrolling; scrolling is the slowest case.
- Keep reduced motion enabled on the server.
- Tune `stream_min_frame_interval` in the board profile if frames queue up:
  the client's self-test (`url: self-test` as the dashboard address) measures
  decode time and recommends a value.

Measure on your panel before relying on it: the client can log frame rate,
decode and draw times. Add to your root YAML:

```yaml
remote_webview:
  stats_interval: 10s
```

`remote_webview` is a single-instance component, so this merges with the
package's settings.
