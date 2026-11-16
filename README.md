# OpenNextion Room Companion

An ESPHome touchscreen and Home Assistant Assist satellite for **ONX2432G028
(2.8-inch, 240 × 320)** and **ONX3248G035 (3.5-inch, 320 × 480)**.

This project starts from **OpenNextion Knob by fixtSE**, preserving its complete
Git history at `6a710e2ce0258e3ced31da3177e00ed5f0b260e3`. The new rectangular
application directly reuses the knob's notification model, control-value parsing,
media formatting and supporting state modules. Its original source and round-knob
entry point remain available for incremental porting. See
[the original README](docs/room-companion/KNOB-README.md).

## Current implementation

- One shared application, two small hardware profiles; separate firmware images.
- Native LVGL Room, Music, Assist, Today and notification pages. Pages scroll on
  the smaller screen; text and touch targets are not scaled down.
- Home Assistant light/scene actions, temperature, media title/artist, playback,
  next track and volume; entity IDs are configurable through substitutions.
- Tap-to-talk Assist with PDM microphone input, I2S spoken responses, transcript,
  reply text, cancel and a persistent software microphone-mute switch.
- Optional `okay_nabu` local wake-word package, disabled on boot.
- Knob-derived text notifications, HTML-break normalization and bounded timeouts.
- Optional authenticated HTTP JPEG cards, plus a small companion renderer.
- Native encrypted ESPHome API, OTA, fallback Wi-Fi setup and extension packages.

This is an initial port. The knob's 20-slot web editor, presets, artwork modes,
encoder gestures, timer overlays and complete entity catalogue are retained in
source but are **not wired into the new rectangular entry points yet**. This
firmware does not currently stream an interactive Lovelace dashboard. See
[architecture and reference projects](docs/room-companion/architecture.md).

## Set up

Use ESPHome **2026.10.2**, matching the development container.

1. Copy `secrets.example.yaml` to ignored `secrets.yaml`. Fill in Wi-Fi, a unique
   API encryption key (`openssl rand -base64 32`), OTA and fallback AP passwords.
   The example key is intentionally a placeholder, not a usable credential.
2. Choose **ONX2432G028.yaml** or **ONX3248G035.yaml**. Set `room_name`,
   `light_entity`, `scene_entity`, `temperature_entity` and `media_entity` under its
   `substitutions:` block. No Home Assistant access token goes on the panel.
3. Build and upload using that entry point in ESPHome. Hardware profiles must
   match the actual board; the images are not interchangeable.
4. Add the device in Home Assistant's ESPHome integration. Enable **Allow the
   device to perform Home Assistant actions** for light/music/scene controls.
5. Configure an Assist pipeline with speech-to-text, a conversation agent and
   text-to-speech. Select it for the discovered satellite, attach the compatible
   PDM microphone accessory and speaker, then open **Assist → Talk**.

The microphone and speaker sockets require attached accessories; board support
alone does not mean a microphone/speaker is fitted. Do not use USB-Serial-JTAG on
GPIO19/20 while they serve the microphone. The board's CH340 USB-to-UART uses
GPIO43/44 and remains available.

[Hardware and Assist setup](docs/room-companion/hardware-and-assist.md) ·
[Adding extensions](docs/room-companion/extensions.md) ·
[Optional renderer](docs/room-companion/renderer.md) ·
[Validation record](docs/room-companion/validation.md)

## Development

Follow `/home/fixt/AGENTS.md`: installations, compilation, tests and services run
on **devserv**, with a task lease and an isolated checkout. Local source editing
is supported. The pinned `.devcontainer` provides the ESPHome environment.

Inside the container on devserv:

```sh
bash scripts/validate-room-companion.sh           # both models + optional packages
bash scripts/validate-room-companion.sh --compile # additionally compile all three
```

The script creates temporary, non-production secrets only if `secrets.yaml` is
absent, and removes only the file it created. It never uploads firmware. Build
outputs live under `.esphome/build/` and are ignored.

## License

AGPL-3.0-only, inherited from the knob project. Original copyright, upstream MIT
notices, fonts and other third-party notices remain in `COPYRIGHT`, `LICENSES/`
and `THIRD_PARTY_NOTICES.md`. Reference projects are linked in the architecture
document; no source from crow-ha or RemoteWebView has been incorporated.
