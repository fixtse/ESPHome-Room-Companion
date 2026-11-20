# Touch latency, 2026-11-19

Measured on the connected ONX3248G035 (3.5", wake-word root, ESPHome 2026.11.0)
with the temporary `lat` trace (`onx_latency.h`): touch-down, release, action,
page rebuild and LVGL frame events. The trace is diagnostic and should be
removed or disabled before a release build.

## Measurements (second run, 23:50–23:51)

| Interaction | Result |
|---|---|
| LVGL button (media, light toggle) | Pressed look on screen 22–31 ms after touch-down |
| Same control: action → page rebuild | 230–570 ms (waits for the HA state to change the page signature) |
| Tile open | Release → finished frame 77–150 ms; up to 1.9 s when the sensor graph loads |
| Carousel swipe | Rebuild 40–140 ms **inside the touchscreen callback**; frame 150–440 ms after it; finger lag 140–360 ms at 2.5–6 fps |
| Notification centre drag | 1–2 frames per drag, each 80–214 ms; finger lag 400–990 ms |
| Main loop | `loop=` peaks 315–507 ms; touchscreen component warnings 98–229 ms |
| SPI | LVGL never waits on the flush (`waiting on SPI` ≈ 0); queued DMA hides transfer time |

## Findings

1. **The main loop blocks, not the display bus.** Touch-down to the first frame
   is 80–600 ms because LVGL's 16 ms timer cannot run while other work holds
   the loop: page rebuilds run synchronously in the touchscreen callback
   (swipe → `on_turn` → refresh scripts), and other components hold it for
   300–500 ms.
2. **Full-page rebuilds cost 40–140 ms** (up to 338 ms with a graph) and mark
   the whole screen dirty (1–4.8 M px per gesture on a 154 k px screen).
3. **Control results wait for Home Assistant.** The local request does not
   change the rendered page; only HA's state reply does.
4. **The notification centre redraws its full panel each move** (80–214 ms
   per frame), so it can only reach about 5 fps.

## Plan

1. Defer swipe/turn work out of the touchscreen callback to the next loop
   pass (render request), and attribute the 300–500 ms loop peaks with
   per-component timing.
2. Show control results immediately: render the requested value and only
   rebuild again if HA's answer differs (no second rebuild on confirmation).
3. Notification-centre drag: draw a snapshot of the panel once at drag start
   and move that image, instead of re-rendering its widgets every frame;
   coalesce moves to one per frame.
4. Update changed values in place instead of rebuilding pages (largest work;
   do after 1–3).
5. Move the LVGL draw buffer to internal RAM (two partial buffers) only if
   frame draw time is still the bottleneck after 1–4.
