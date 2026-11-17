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
