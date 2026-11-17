# Main-loop profiling, 2026-11-17

Profiled the connected ONX3248G035 (ESP32-S3, 320×480) over UART, using
ESPHome **2026.10.2**, the wake-word model root, restored device configuration,
and the normal carousel. Builds ran in the pinned container on devserv in
`~/agent-work/room-companion-loop-20261117`, synchronized from `3edd3e4` plus
this change. No 2.8-inch firmware was compiled.

## Method and results

Temporary `runtime_stats: {log_interval: 15s}` instrumentation collected six
15-second windows for each build. It is excluded from the production firmware:
its verbose reports themselves delay the loop. `debug.loop_time` is a maximum
over its reporting interval, not average execution time; profiler-report delays
must not be mistaken for normal UI latency.

The baseline's one-second UI interval averaged 65–67 ms after startup. LVGL used
about 2,000 ms per 15-second window, with 119–127 ms rendering peaks. Settled
active-loop peaks were 195–206 ms.

After eliminating redundant styles, labels, icon resets and repeated carousel
view construction, the one-second UI interval averaged 15–17 ms (about 76% less).
Most settled windows used 842–926 ms of LVGL time, with 43–45 ms rendering peaks
and 61–67 ms active-loop peaks. One window still contained a 242 ms LVGL spike
(255 ms active-loop peak); this is not a guarantee of worst-case input latency.
These instrumented results precede the final additional guard against unchanged
alignment calls. Startup and initial network/icon downloads are excluded from
these settled ranges.

## Changes

- Skip identical **local** LVGL style values while preserving selectors and
  inherited styles. Keep colour-emoji font wrappers when their base font matches.
- Use cached label updates for the idle clock, date, weather readings, slideshow
  clock and navigation label.
- Construct the selected carousel view once per refresh and apply its final
  geometry directly, rather than resetting to defaults first.
- Bind the final state-aware icon directly, avoiding fallback-image replacement
  and hide/show cycles on every refresh.
- Skip unchanged alignment calls. Touch, media progress and animation intervals
  retain their existing cadence.

## Validation limits

Both model configurations validate; only the 3.5-inch wake-word root is compiled
and flashed for this task. UART timing and connectivity are hardware checks;
cover-art appearance, swiping and all possible screens are not visually verified.

The native control/state, touch, subscription, Wi-Fi lifecycle, HTTP and browser
catalogue checks passed. The aggregate suite also encountered two unrelated
existing harness failures: `test_input_routing` (since removed with the obsolete
encoder input package) searched for an obsolete substring, and the configuration-browser request mock rejects
`/panel/panel.js`. Neither failing source/test area is modified here.

## Reproduction and artifacts

On devserv, under the task lease, in the checkout above:

```sh
docker run --rm -v "$PWD":/config ghcr.io/esphome/esphome:2026.10.2 compile ONX3248G035.wake-word.yaml
docker run --rm -v "$PWD":/config ghcr.io/esphome/esphome:2026.10.2 config ONX2432G028.yaml
mise exec -- bash scripts/test_controls.sh
```

The production build follows an ESPHome clean to discard stale profiler build
metadata. Build and test logs are under `~/agent-work/room-companion-loop-*.log`
on devserv. Temporary UART captures and flash verification logs are in
`/tmp/room-companion-loop-20261117/` on the coordination PC. Firmware binaries
remain outside the repository. The physical board is flashed through
`/dev/ttyUSB0` using esptool at 460800 baud, factory image at offset zero, with
hash verification; provisioned NVS is retained.

## Production hardware check

The clean production build succeeded (`config_hash=0x13065dbc`) and its USB flash
passed esptool hash verification. UART reports a successful boot, approximately
56 KB free internal heap, and settled 15-second maximum-loop samples of
59, 61 and 59 ms. Initial samples were 1557 and 300 ms during startup/downloads;
these are retained rather than included in the settled range. The panel returned
HTTP 200 on repeated Wi-Fi checks. No panic or abort appeared in the observation
window. Source hashes on devserv matched the local edited files exactly.
