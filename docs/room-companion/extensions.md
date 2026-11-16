# Extensions

Edit the appropriate root YAML (or keep your own root YAML beside it). ESPHome
merges packages, so your additions do not require editing shared application code:

```yaml
substitutions:
  device_name: bedroom-panel
  friendly_name: Bedroom Panel
  room_name: Bedroom
  light_entity: light.bedroom
  scene_entity: scene.bedtime
  temperature_entity: sensor.bedroom_temperature
  media_entity: media_player.bedroom

packages:
  board: !include packages/room_companion/boards/onx2432g028.yaml
  application: !include packages/room_companion/application.yaml
  my_extensions: !include examples/room-companion/extension.yaml
  # wake_word: !include examples/room-companion/wake-word.yaml
```

Your package can define sensors, buttons, scripts, native HA actions, or
`external_components` pointing to your own component directory. Use ESPHome
`!extend` to change an existing ID and `!remove` to remove an item. Avoid redefining
hardware IDs or colliding with I2S, SPI or display ownership.

Reusable IDs include `backlight`, `panel_display`, `panel_touch`, `panel_microphone`,
`panel_speaker`, `assist`, `microphone_muted`, `native_api` and `room_ui`.
Application scripts include `toggle_assist`, `toggle_room_light`,
`activate_room_scene`, `refresh_glance` and `dismiss_notification`.

Native actions exposed to HA:

```yaml
action: esphome.bedroom_panel_show_message
data:
  title: Laundry
  message: Washing finished.<br/>Please empty the machine.
  timeout_seconds: 60
```

Replace the action prefix with the device's actual action name. Use `0` seconds
to keep the message until dismissed. `dismiss_message` clears it remotely. This
initial touch port accepts text notifications; notification pictures remain a
future port from the knob. Supported glyphs currently follow built-in Montserrat
fonts, not the knob's complete emoji/font pipeline.
