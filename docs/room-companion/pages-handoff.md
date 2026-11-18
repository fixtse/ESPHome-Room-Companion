# Pages redesign: handoff (2026-11-18, work in progress)

The project is intentionally **not buildable or consistent** until every phase
below is done. Read `/home/fixt/AGENTS.md` and `AGENTS.md` first: builds and tests run
on devserv under a lease, and nothing gets flashed without a request.

## Owner decisions

- The carousel shows **pages**, not slots. Each page has its own **name and
  carousel icon** (`mdi:*`). The icon no longer comes from an entity.
- There is **no main entity**. A page holds 1, 2 or 4 entities by placement:
  `single` (full screen), `two_controls` (top/bottom) or `control_sensors`
  (one large + three compact; the compact positions accept only
  `sensor.`/`binary_sensor.`).
- Any Home Assistant entity can be placed. Every placed entity is its own
  configuration entry with its **own shortcut, swipe gestures and quick
  actions** (the existing per-slot settings). An entity appears on at most one page.
- Entry pool: **20 → 40** (`CONFIGURABLE_DEVICE_SLOT_COUNT`).
- The web editor supports **drag and drop** for building layouts.

## Done: phase 3 web editor (uncommitted, not yet run in a browser)

- `my_components/onx_config_web/panel_web.h`: new pages editor (replaces the
  old dropdown editor). It has a page list (drag to reorder), name, icon, layout
  and a grid of positions. Drop entities from the picker or search results onto
  a position, drag between positions to swap, and drag out (or Remove) to
  remove. Selecting a placed entity opens its entry's normal tabs. The editor
  saves the whole page list with `panel_pages_save`. `pageFor()` still feeds the
  preview: it treats `entities[0]` as the drawn page. Update the preview
  (`ONX_PANEL_PREVIEW`) to draw the *selected page* and its icon.
- `config_app.h`: `MAX_SLOTS=40`. It adds a `page` picker mode, draggable
  search results (`application/x-onx-entity`) and the `globalThis.ONX_APP`
  bridge (`pick`, `item`, `slots`, `select`, `add`, `clear`, `saveAll`).
  Placing a new entity creates a draft entry in the first free index. Removing
  the last placement clears it. **Save pages** saves dirty entries first, then
  the pages.
- `config_page.h`: grid/list CSS.

### Remaining web work

- Hide or replace the old slot list (`#slots`, move up/down, duplicate,
  add-slot) with the page list. Entries are internal now.
- Remove the "Choose entity" display binding for page-owned entries, or make
  it call `place()`.
- Generalise `#timer-options` ("Hide from carousel"): page membership replaces
  it.
- Import/export and presets: accept 10/20/40 slots (pad to 40). Include
  `pages` in exports and presets. Migrate 20-slot exports into one `single`
  page per entity.
- Rewrite `tests/test_panel_pages.cjs` (it still uses `selectOption` on
  `[data-member]`). Stub `globalThis.ONX_APP` and test drag/drop through
  `dispatchEvent` with a `DataTransfer`. Also update `tests/test_panel_startup.cjs`.
- Docs: `docs/web-configuration.md` and `docs/room-companion/panel-ui.md`.

## Phase 1: firmware, 40 entries (written, NOT compiled)

- `onx_memory::ENTRY_COUNT=40`. `CONFIGURABLE_DEVICE_SLOT_COUNT` uses it, and
  the hard-coded `20`s in headers/YAML now use one of the two constants
  (`git diff` shows them all).
- `normal_device_online_mask` is now `uint64_t`/`1ull`. Check any other `1u <<`
  or `1 <<` on slot or UI indexes.
- NVS migrations: `onx_storage` config v2 → v3 and state v3 → v4 (20 → 40,
  with a copy at boot). Press bindings v2 → v3 (20 → 40). Presets are JSON
  (`SLOT_COUNT=ENTRY_COUNT`; shorter lists pad). Per-entry prefs for entries
  20–39 use the existing generated keys.
- The web request `data` cap was raised from 6144 to 16384 for the page list.
- Still to do: `onx_config_web.cpp` slot bound (raised to 40). Tests that
  assume 20 (`tests/test_sensor_view.cpp`, `test_selected_metadata.py`,
  `test_entity_recovery.py`, JS tests). Web import/export (10/20/40).
- RAM: unmeasured. The `onx_storage` arrays (`normal_ui_devices_` strings,
  `normal_ui_device_states_` 40×32 B) are object members; check whether they are
  internal RAM. `save_normal_ui_states_` keeps 2×1.3 KB on the stack.
  `StoredPages` (about 4.8 KB) is saved through ESPHome preferences, which
  makes an internal-RAM copy during sync. Switch it to a direct `nvs_set_blob`
  like `save_normal_ui_config_` if RAM is tight.

## Phase 2: firmware pages (written, NOT compiled)

- `onx_panel_pages.h`: a `StoredPages` list (`rc.pages.v2`) of up to 40 pages
  `{name, icon, kind, entries[4]}`, holding *configuration* indexes (stable).
  It migrates once from `rc.page.v1.<entity>`. `reconcile()` drops cleared
  entries (it splits broken pages into singles) and gives unplaced entries a
  single page. It runs when `dirty` is set (in `mode_router` slot save) and on
  RPC.
- RPC `panel_pages` / `panel_pages_save` (whole list) match the web editor.
- Carousel: `onx_entity_config::page_order` hook. The carousel functions now
  walk page order (the first entity's UI slot per page), so the other entries
  are reachable only from their page.
- Navigator: the page icon (`entity_icon_downloader` binds `mdi:*`) and page
  name override the entity's (`normal_mode_scripts.yaml`).
- Not yet:
  - `timer_hidden` is now unused by the carousel when pages are installed.
    Remove its UI or fold it in.
  - Preset apply or import paths that bypass the `mode_router` save may need
    `onx_panel_pages::dirty=true`.
  - Pages are not part of presets/exports yet.
  - The page name is not shown on the page body (`render` still titles cards
    by entity, which is fine).
  - `test_panel_layout.cpp` still uses the old `valid()` rules (it compiles,
    but the rules are no longer used for saving).

## Next agent: verify

1. devserv lease plus an isolated checkout. Then compile `ONX3248G035.wake-word.yaml`
   and `ONX2432G028.yaml` (pinned 2026.10.2), run `--compile` for the examples,
   and the C++ and Playwright tests.
2. Fix compile errors. Likely spots: `INT32_MAX`/includes in
   `onx_entity_config.h`, signed/unsigned `(int)` casts in YAML loops,
   `make_ram_allocated` usage in `onx_panel_pages.h`.
3. Hardware: boot with existing 20-entry NVS, then confirm migration (entries,
   light state, press actions, layouts become pages). Then measure internal RAM
   against `memory-budget.md`.
