# OpenNextion Room Companion

An ESPHome touch panel and Home Assistant Assist satellite for **ONX2432G028
(2.8-inch, 240 × 320)** and **ONX3248G035 (3.5-inch, 320 × 480)**.

It builds on **OpenNextion Knob by fixtSE**, preserving its complete Git history
(base `6a710e2ce0258e3ced31da3177e00ed5f0b260e3`), license and original
round-knob entry point. See [the original README](docs/room-companion/KNOB-README.md).

## What it does

- **Two screen modes, switchable at runtime.** A *native panel* (LVGL: Home,
  Music, Assist, More) that needs no extra server, or an interactive *Home
  Assistant dashboard* rendered by a Remote WebView server and streamed to the
  panel, as in [ay129's Waveshare display](https://github.com/ay129-35MR/Waveshare-ESP32-S3-Touch-LCD-4-Home-Assistant-Display).
  Native screens take over for setup, Assist and notifications, and the
  dashboard comes back afterwards.
- **Settings page with Home Assistant's Visit link.** Room name, screen mode,
  dashboard server and address, room light/scene/temperature/media player,
  brightness, sleep, Assist volume and presets. Optional sign-in from the
  browser lists your entities and dashboards; no token is stored on the panel.
- **Presets.** Up to eight named snapshots of the settings, applied without a
  restart from the page or Home Assistant's **Preset** select (shows *Custom*
  after manual changes).
- **Wi-Fi setup with Improv** over USB (Improv Serial) and Bluetooth (Improv
  BLE, authorized by pressing BOOT). No setup hotspot.
- **Assist.** Tap Talk, press BOOT, or use the **Talk** entity; transcript and
  reply on screen, spoken reply on the speaker, software mute.
- **Notifications** compatible with the knob's `show_message`, including an
  optional picture; HTML line breaks become text.
- **Low-power sleep.** Screen off, Wi-Fi modem sleep, Home Assistant stays
  connected. After inactivity, on a schedule, from the **Low power sleep**
  switch or the page. Touch wakes it; notifications can.

## Set up

Use ESPHome **2026.10.2**, matching the development container.

1. Copy `secrets.example.yaml` to ignored `secrets.yaml`. Fill in a unique API
   encryption key (`openssl rand -base64 32`) and OTA password.
2. Choose **ONX2432G028.yaml** or **ONX3248G035.yaml** and build/upload it. The
   images are not interchangeable between models.
3. Connect Wi-Fi with Improv: USB at [web.esphome.io](https://web.esphome.io),
   or Bluetooth from Home Assistant or an Improv app (press **BOOT** on the
   panel to allow it).
4. Add the panel in Home Assistant's ESPHome integration and enable **Allow the
   device to perform Home Assistant actions**.
5. Open **Visit** on the device page (or the address on the panel's **More**
   screen) and choose the room's controls.
6. For Assist, configure a pipeline with speech-to-text, a conversation agent
   and text-to-speech, and select it for the panel's Assist satellite. Attach
   the PDM microphone and speaker accessories.
7. Optional: [run the dashboard server](docs/room-companion/dashboard-stream.md)
   and switch the screen mode to *Home Assistant dashboard*.

## Notifications

```yaml
action: esphome.room_companion_35_show_message
data:
  title: Doorbell
  message: Someone is at the front door.
  image_url: "http://homeassistant.local:8123/local/doorbell.jpg"
  timeout_seconds: 60
```

Replace the prefix with your device's. Include all four fields; leave
`image_url` empty for text only, and use `timeout_seconds: 0` to keep the
message until it is dismissed. `dismiss_message` clears it remotely.

## Performance

These panels use SPI displays, so the dashboard stream is the expensive part.
Expect roughly 5–10 frames per second for full-screen changes on the 3.5-inch
panel and faster on the 2.8-inch one; these are estimates, not yet measured on
hardware. Design compact, non-animated dashboards for the panel size. The stream allocates
nothing until dashboard mode has a server, pauses during Assist and
notifications, and decodes nothing while paused. Prefer tap-to-talk over the
optional on-device wake word in dashboard mode.
[Details](docs/room-companion/dashboard-stream.md#designing-a-dashboard-for-these-panels)

[Architecture](docs/room-companion/architecture.md) ·
[Dashboard stream](docs/room-companion/dashboard-stream.md) ·
[Hardware, Wi-Fi and Assist](docs/room-companion/hardware-and-assist.md) ·
[Extensions](docs/room-companion/extensions.md) ·
[Validation record](docs/room-companion/validation.md)

## Development

Follow `/home/fixt/AGENTS.md`: installations, compilation, tests and services run
on **devserv**, with a task lease and an isolated checkout. The pinned
`.devcontainer` provides the ESPHome environment. Inside it on devserv:

```sh
bash scripts/validate-room-companion.sh           # both models + example packages
bash scripts/validate-room-companion.sh --compile # additionally compile all three
```

The script creates temporary, non-production secrets only if `secrets.yaml` is
absent, and removes only the file it created. It never uploads firmware.

## License

AGPL-3.0-only, inherited from the knob project. Original copyright, upstream MIT
notices, fonts and other third-party notices remain in `COPYRIGHT`, `LICENSES/`
and `THIRD_PARTY_NOTICES.md`. `my_components/remote_webview` is vendored from
[strange-v/RemoteWebViewClient](https://github.com/strange-v/RemoteWebViewClient)
under the MIT License; see its `NOTICE.md`.
