# Validation record

Date: 2026-11-17. Execution host: **devserv (192.168.31.35)**.
Isolated checkout: `/home/fixt/agent-work/room-companion-20261117`.
Knob software base: `6a710e2ce0258e3ced31da3177e00ed5f0b260e3`.
The committed changes in this repository were synchronized into that checkout.

Environment: `ghcr.io/esphome/esphome:2026.10.2`, matching `.devcontainer`.
The container ran the validation/build matrix with the checkout mounted at
`/config` and its own persistent build-tool cache. No dependency installation,
compilation or tests ran on the coordination server.

Commands:

```sh
bash scripts/validate-room-companion.sh
bash scripts/validate-room-companion.sh --compile
```

The script validates ONX2432G028 and ONX3248G035 and a composed example including
local extensions and the optional `okay_nabu` wake word. The compile variant also
builds all three configurations using ESP-IDF. Validation uses generated temporary
API credentials and placeholder network settings; these are not deployment
firmware credentials.

Native C++ regression checks were run on devserv for `test_control_state.cpp`,
`test_media_state.cpp` and `test_config_rules.cpp`. The inherited media test's
old 500–1500 ms playlist assertions were updated to the existing implementation's
800–2000 ms timing; the production knob timing was not changed.

The renderer image was built from `renderer/Dockerfile`, then
`python -m unittest -v test_server` ran inside it. Tests check both model-specific
JPEG dimensions, required bearer authorization, unknown models, a successful
rendered response and HA-fetch failure behavior. No live HA credentials were used.

The development container JSON was checked, and its pinned image was exercised
by the firmware matrix. The devcontainer CLI itself was not used.

## Hardware verification still required

This new ESPHome project has not been flashed during project creation. The
previously flashed firmware was OpenHAPanel, a different application. Display
initialization/colors, all touch corners, microphone channel/gain, amplifier
output, Assist pipeline behavior, local wake-word accuracy and concurrent audio/
UI load need physical testing on **both** actual board models. The optional image
service has not been deployed or connected to the user's HA instance.

The original round-knob entry point and full historical test suite have not been
rebuilt as part of the rectangular port. The native rectangular firmware does not
yet include the knob's complete configuration UI, preset manager or all entity
controls; those are explicit follow-up ports.

## Results

All three firmware configurations validated and compiled successfully:

| Build | Static RAM | Application flash |
|---|---:|---:|
| ONX2432G028 | 118,155 bytes | 1,465,251 bytes |
| ONX3248G035 | 118,155 bytes | 1,472,111 bytes |
| ONX2432G028 + wake word + extension | 119,891 bytes | 1,724,863 bytes |

These build summaries do not measure peak PSRAM use, audio buffer headroom or
runtime responsiveness. All three selected native C++ test programs and both
renderer test cases passed. The renderer container built successfully.

## 0.2.0 rework — 2026-11-17

Execution host: **devserv**. Isolated checkout:
`/home/fixt/agent-work/room-companion-rework-20261117`, synchronized from the
uncommitted working tree with `rsync` (no `.git`, `.esphome` or `secrets.yaml`;
temporary non-production secrets were generated there). Container:
`ghcr.io/esphome/esphome:2026.10.2`, ESP-IDF 5.5.6.

```sh
docker run --rm -v $PWD:/config -w /config -e ESPHOME_ESP_IDF_PREFIX=/config/.esphome/idf \
  --entrypoint bash ghcr.io/esphome/esphome:2026.10.2 scripts/validate-room-companion.sh --compile
```

| Configuration | Result | Static internal RAM | App flash |
|---|---|---|---|
| ONX2432G028 | config valid, compiled | 49.7% (170,015 B) | 23.5% |
| ONX3248G035 | config valid, compiled | 49.7% (170,015 B) | 23.6% |
| ONX2432G028 + wake word + extension example | config valid, compiled | 50.3% | 26.7% |

For comparison, the 0.1.0 build used 43.5% static internal RAM. The settings
page JavaScript was syntax-checked with `node --check` (node:22-alpine).

**Not verified:** nothing was flashed. Runtime behaviour on hardware is
untested: display handoff between LVGL and the stream, stream frame rate and
heap headroom with Assist, Improv Serial/BLE provisioning with this build, the
settings page, presets, sleep/wake and notification pictures. The dashboard
performance figures in the documentation are estimates.

### USB deployment, 3.5-inch with wake word — 2026-11-17

`ONX3248G035.wake-word.yaml` (0.2.0 + `examples/room-companion/wake-word.yaml`
with `wake_word_restore_mode: RESTORE_DEFAULT_ON`) was compiled on devserv with
the panel's existing secrets (removed afterwards): 50.2% static internal RAM,
26.7% app flash. The chip was put in download mode by toggling CH340 DTR/RTS,
and the port was bridged to devserv over a loopback-only SSH tunnel. esptool
`--no-stub` wrote the app, partition table and OTA data with verified hashes
(the stub loader did not respond through the tunnel). NVS was not erased.

Observed after reset: `setup() finished successfully`, Wi-Fi connected
(192.168.10.184), settings page served on port 80, `/rc/api` state returned
`home_assistant: true`, a foreign `Origin` got 403, 130 KB internal heap free.
Over the API: *Wake word enabled* on, *Microphone muted* off, voice assistant
flags 7. Not yet checked by a person: the display, touch, saying "Okay Nabu",
speaker output, the dashboard stream, presets and sleep.

### Speaker media player — 2026-11-17

Assist replies now use a speaker media player named **Speaker**, exposing volume
and mute to Home Assistant. The initial volume is 60%; the component restores
subsequent volume/mute settings. The announcement pipeline requests mono 16 kHz
FLAC for the panel's fixed-rate I2S speaker. Talk/Close stop announcements, and
the Assist overlay stays visible while the player is announcing.

At the user's request, this change targets only `ONX3248G035.wake-word.yaml`;
the 2.8-inch model and example matrix are not rebuilt in this iteration.

Hardware follow-up found that a mapping-shaped shared `esphome.on_boot` was
replaced by the panel's list-shaped hooks during package merging. The generated
3.5-inch C++ lacked the shared initialization and backlight-on sequence. The
shared hook now uses a list so all three startup handlers are retained.
The 3.5-inch root also overrides the knob's scale defaults explicitly; scaled
fonts initially used 4-bit antialiasing while investigating the OTA limit (the
full-atlas 8-bit scaled build was rejected at 9,562,711 bytes, before upload). The PDM microphone now
matches the manufacturer's right-channel, 32-bit input with DC correction.
The scaled 4-bit build was still 144,387 bytes too large. Panel builds now keep
1,409 individual color emoji while excluding composite sequences from the atlas;
the round-knob build retains the full atlas. The generator emits the same
conditional subset and proper C++ UTF-8 byte escapes.

The compact-atlas 8-bit build passed at 8,114,303 bytes (99.9%). The user
then chose 4-bit fonts for headroom; the final 3.5-inch root explicitly uses
`font_bpp: 4`, with 6,822,447 bytes (84.0%) flash and 163,168 bytes static RAM.

Final touch update: 44 px icon-only navigation (tap to jump, one page per
horizontal swipe), horizontal swipe to close details/overlays/dashboard, native
scrollable five-row Settings with direct row taps and a persisted Dark mode
switch. LVGL runs at 16 ms with a 25% draw buffer to shorten individual SPI
transfers; touch movements bypass the encoder's multi-detent accumulation.
Only the 3.5-inch wake-word root was compiled and uploaded, per user instruction.
The user confirmed the backlight fix restored the display. The final gestures,
perceived latency, dark palette, microphone/wake word and speaker acoustics
still need physical testing.

Execution: devserv checkout `/home/fixt/agent-work/room-companion-volume-20261117`,
base revision `0e9bbe0f14e1cf1e459eb8ff5c88ec80b67eb573` plus this working tree.
Pinned container `ghcr.io/esphome/esphome:2026.10.2`; commands:
`esphome compile ONX3248G035.wake-word.yaml` and
`esphome upload ONX3248G035.wake-word.yaml --device 127.0.0.1`
(loopback SSH forwarding to the panel's OTA port).
Native C++ touch regression tests passed (one page per swipe, back in both
directions, native scroll click suppression and navigation tap behavior), as did
both the full and compact atlas encoding/decoding tests.
The final binary is kept separately under
`.esphome/artifacts/rc35-4bit-nav-dark-20261117.bin` on devserv.

### Stable media layout and exit gestures — 2026-11-17

The panel owns media geometry during shared detail refreshes: centred artwork,
one title/artist pair, volume and room labels, a straight progress bar and
transport buttons. The inherited circular ring is transparent on media pages.
A visible close button and horizontal swipes exit; vertical swipes adjust volume.
Dragging from a transport button suppresses its click. Idle playback automatically
replaces the panel clock using the inherited popup lifecycle.

Build/configuration validation only; these changes have not been flashed or
hardware-tested. On devserv, isolated checkout
`/home/fixt/agent-work/room-companion-media-20261117`, base revision
`e2927cb0ce2cc721403d4068e24b9b9fad044cca` plus this working diff:

- ESPHome container `ghcr.io/esphome/esphome:2026.10.2`: configuration and
  compilation passed for `ONX2432G028.yaml` and `ONX3248G035.yaml`, using the
  validation script restricted to these two model roots.
- `c++ -std=c++17 -Itests/touch_stubs tests/test_touch_input.cpp` and
  `c++ -std=c++17 tests/test_media_state.cpp`, followed by both executables:
  passed, including the new drag-over-transport click suppression regression.
- The full `scripts/validate-room-companion.sh --compile` matrix stops at the
  inherited knob root's existing configuration error: its shared settings lambda
  references panel-only `touch_input` and `panel_dark_mode` IDs inside a C++
  preprocessor guard, which ESPHome still checks during configuration. The knob
  and example matrix therefore was not compiled in this run.
