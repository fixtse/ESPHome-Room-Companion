# Home Assistant dashboard on the panel

In **Home Assistant dashboard** mode, a server renders any web page (usually a
Lovelace dashboard) in headless Chromium and streams changed tiles to the
panel; touches go back to the server. The panel switches to its native screens
for setup, Assist, notifications and the corner menu, then returns to the
dashboard.

## 1. Run the server

On any always-on Docker host on the same network (not on the panel):

```sh
cd deploy/remote-webview
docker compose up -d
```

`compose.yaml` pins `strangev/remote-webview-server:1.3.0`, keeps the browser
profile in `./pw-data`, and turns on reduced motion, which makes SPI panels feel
much smoother. One server can drive several panels at their own sizes.

## 2. Sign the server's browser in to Home Assistant

The server's Chromium needs its own Home Assistant session. Once:

1. `docker compose --profile devtools up -d` (exposes Chrome DevTools on 9222).
2. In desktop Chrome open `chrome://inspect/#devices`, **Configure…**, add
   `<server-host>:9222`.
3. Set the panel's dashboard address (below) so the server opens it, then click
   **inspect** on that tab and sign in. Enable **Keep me logged in**.
4. `docker compose stop devtools`. Anyone who can reach port 9222 controls that
   browser session, so do not leave it running on an untrusted network.

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

The server or zoom cannot change while the stream runs; the page shows a
**Restart needed** card. Presets switch the screen mode and dashboard address,
so e.g. a *Night* preset can show a dimmed dashboard or the native panel.

## Using it

- Hold the **top-left corner** for about a second to open the native controls
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
