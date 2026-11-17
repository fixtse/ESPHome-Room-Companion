# Extensions

Edit the appropriate root YAML (or keep your own root YAML beside it). ESPHome
merges packages, so your additions do not require editing shared application code:

```yaml
substitutions:
  device_name: bedroom-panel
  friendly_name: Bedroom Panel
  room_name: Bedroom              # first-boot default; editable on the settings page

packages:
  board: !include packages/room_companion/boards/onx2432g028.yaml
  application: !include packages/room_companion/application.yaml
  my_extensions: !include examples/room-companion/extension.yaml
  # wake_word: !include examples/room-companion/wake-word.yaml
```

Room entities, screen mode, dashboard, sleep and volume are runtime settings
(the **Visit** page), not substitutions. `default_dashboard_server` and
`default_dashboard_url` set first-boot values for fleets.

Your package can define sensors, buttons, scripts, native HA actions, or
`external_components` pointing to your own component directory. Use ESPHome
`!extend` to change an existing ID and `!remove` to remove an item. Avoid redefining
hardware IDs or colliding with I2S, SPI or display ownership: nothing else may
draw on `panel_display` while the dashboard stream owns it.

Reusable IDs include `backlight`, `panel_display`, `panel_touch`, `panel_microphone`,
`panel_speaker`, `assist`, `microphone_muted`, `native_api`, `room_ui`,
`companion` and `dashboard_stream`. Application scripts include `toggle_assist`,
`toggle_room_light`, `activate_room_scene`, `show_notification` and
`dismiss_notification`.

From lambdas, `id(companion)` exposes `settings()`, `dashboard_mode()`,
`request_sleep(bool)`, `apply_preset(name)`, `set_mode("native"|"dashboard")`,
`set_dashboard_url(url)` and `set_overlay(esphome::room_companion::OVERLAY_MENU, on)`.
Its `on_event: (key, value)`, `on_settings`, `on_sleep: (sleeping)` and
`on_owner: (dashboard)` triggers can be extended in your package.
