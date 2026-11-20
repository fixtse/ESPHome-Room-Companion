# Room Companion emulator

Renders the 2.8" (ONX2432G028) and 3.5" (ONX3248G035) panels on a PC with
[esphome-lvgl-emulator](https://github.com/fixtse/esphome-lvgl-emulator). The
emulator compiles the firmware's own panel headers (`onx_panel_pages.h`,
`onx_panel_detail.h`, `onx_navigation.h` and others) against LVGL 9.6.0 with the firmware's
`lv_conf.h`. Screen sizes, scales and dock geometry are read from the same board
YAML files, and fonts and icons come from `fonts/` and `assets/`.

```sh
emulator/render.sh                                   # every entity type, both panels (devserv)
emulator/render.sh --model 28 --only climate         # one panel, matching screens
emulator/render.sh --scenario emulator/scenarios/demo.json
LVGL_EMU_DIR=../esphome-lvgl-emulator emulator/render.sh   # local emulator checkout
```

Output in `emulator/out/`:
- Device renders (`28-*.png`, `35-*.png`).
- The web configuration preview of the same screens (`web/`), drawn by `panel_web.h` from the same preview data (`onx_preview.h`, shared with the firmware's preview API).
- Side-by-side sheets (`compare/`, with `28-all.png` and `35-all.png`).

See "Validating screen changes" in AGENTS.md.

A scenario (`scenarios/all-entities.json`, `scenarios/demo.json`) lists entities with their states and
attributes, and the pages that lay them out (`single`, `two`, `sensors`,
`grid`). Each page is rendered with the dock, then each entity's detail screen.

Not emulated:
- Live Home Assistant data (states come from the scenario).
- Downloaded MDI icons (the bundled fallback icons are used).
- Status icons drawn by YAML widgets.
- Touch input in PNG mode.

Fonts use FreeType rather than ESPHome's pre-rendered bitmaps, so glyph edges
differ slightly; positions and sizes match. A render is a layout check, not
hardware verification.
