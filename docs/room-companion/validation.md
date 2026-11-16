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
