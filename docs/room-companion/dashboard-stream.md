# Home Assistant dashboard on the panel

In **Home Assistant dashboard** mode, [RemoteWebViewServer](https://github.com/strange-v/RemoteWebViewServer) renders a web page (usually a
Home Assistant dashboard) in headless Chromium and streams changed tiles to the
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
or the address on the panel's **More** screen):

- **Screen**: *Home Assistant dashboard*.
- **Remote WebView server**: `<server-host>:8081`. Restart when prompted.
- **Dashboard address**: e.g. `http://homeassistant.local:8123/room-panel/0`.
  After **Connect to Home Assistant** on the page, dashboards appear as
  suggestions. Home Assistant can also change it through the **Dashboard
  address** text entity, and **Screen mode** switches between modes.
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
