# OpenNextion Room Companion by fixtSE

**The Home Assistant controls you reach for every day, right where you use them.**

Turn a rectangular OpenNextion touchscreen into a dedicated control panel for a
room. Dim the lights from your bedside, start a playlist in the kitchen, or see
who’s at the door without reaching for your phone. Choose the controls you want
on screen and arrange them around your routine.

Made by [fixtSE](https://fixtse.com/).

[Get started](#get-started) · [User guide](docs/web-configuration.md) · [Firmware releases](https://github.com/fixtse/esp-ota/releases) · [All guides](#find-your-guide)

## What can it do?

| Make it part of your day | What you can do |
| --- | --- |
| Set the mood | Dim the lights, pick a color, lower the blinds, or run a favorite scene. |
| Get comfortable | Adjust the thermostat and fan, check the forecast, or see how the room temperature has changed. |
| Put on something good | Play, pause, skip tracks, adjust the volume, or start a saved playlist or radio station. |
| Keep timers in sight | See active Home Assistant and supported Assist timers, with countdowns and controls for pausing or stopping them. |
| Catch the things that matter | Put a laundry reminder, calendar alert, or doorbell photo on screen with a Home Assistant automation. Swipe down to open the notification center. |
| Ask Home Assistant | With a compatible microphone, speaker, and Assist pipeline, tap **Talk** to make a request, see the transcript, and hear the reply. The wake-word variant adds hands-free activation with **Okay Nabu**. |

## Make it yours

- **Pick up to 20 controls.** Give a favorite control its own page or group
  related controls together. Swipe between pages or tap an icon along the
  bottom to jump straight there.
- **Keep a setup for each routine.** Save up to eight named presets and switch
  from the panel’s web page or Home Assistant’s **Preset** select without
  restarting.
- **Set up from a phone or computer.** Open **Visit** from the Home Assistant
  device page to browse entities, arrange controls, preview changes, and back
  up your configuration. The browser handles Home Assistant sign-in.
- **Choose what shows when you’re away.** Show a clock, a dashboard, or a photo slideshow,
  or schedule the display to switch off while the panel stays connected to
  Home Assistant. Choose portrait or landscape, brightness, and
  accent colors to suit the room.

## Choose your panel

Both models run the same Room Companion app. Pick the firmware that matches
the model number on your panel.

| OpenNextion model | Touchscreen | ESPHome firmware |
| --- | --- | --- |
| **ONX2432G028** | 2.8 inches · 240 × 320 | [ONX2432G028.yaml](ONX2432G028.yaml) · [Wake-word variant](ONX2432G028.wake-word.yaml) |
| **ONX3248G035** | 3.5 inches · 320 × 480 | [ONX3248G035.yaml](ONX3248G035.yaml) · [Wake-word variant](ONX3248G035.wake-word.yaml) |

## Get started

1. [Install the firmware and connect to Wi-Fi](docs/setup.md). For a first
   install, use the factory image for your model, built with **ESPHome 2026.11.0**.
   Wi-Fi setup is available over USB or Bluetooth with Improv, or through the
   panel’s setup hotspot.
2. Add the panel through Home Assistant’s **ESPHome** integration and enable
   **Allow the device to perform Home Assistant actions**.
3. Open **Visit** on the device page, or use the address shown on the panel.
   Sign in to browse your Home Assistant entities, choose your first control,
   and select **Save slot**.

Already running Room Companion? Check the panel’s **Firmware Update** entity
in Home Assistant for available updates. [Setup and update guide](docs/setup.md#keep-it-updated)

## Add a dashboard or photo slideshow

**Bring your dashboard to the room.** The optional
[RemoteWebViewServer](https://github.com/strange-v/RemoteWebViewServer) streams an
interactive Home Assistant dashboard to the panel. Install it as a Home Assistant
add-on or run it on a separate Docker host. [Dashboard streaming setup](docs/room-companion/dashboard-stream.md)

[![Add the Remote WebView Server repository to Home Assistant](https://my.home-assistant.io/badges/supervisor_add_addon_repository.svg)](https://my.home-assistant.io/redirect/supervisor_add_addon_repository/?repository_url=https%3A%2F%2Fgithub.com%2Fstrange-v%2FRemoteWebViewServer)

**Turn the idle screen into a photo frame.** The
[fixtSE ImmichFrame fork](https://github.com/fixtse/ImmichFrame) serves photos from
your Immich library at the panel’s resolution. Open the panel’s **Idle screen**
settings, choose **ImmichFrame**, and select your server or enter its URL. The
panel fetches photos directly from ImmichFrame. [Slideshow setup](docs/room-companion/panel-ui.md#immichframe)

## Find your guide

| I want to… | Guide |
| --- | --- |
| Install the firmware, connect Wi-Fi, or update my panel | [Setup and connection help](docs/setup.md) |
| Add controls, set up presets, or change the display | [Web configuration](docs/web-configuration.md) |
| Learn the touch controls or set up shortcuts | [Using the panel](docs/controls.md) · [Custom gestures](docs/gesture-controls.md) |
| Show reminders, photos, or calendar events | [Display notifications](docs/display-notifications.md) |
| Connect the microphone and speaker or set up wake word | [Hardware and Assist](docs/room-companion/hardware-and-assist.md#commissioning) |
| Show a Home Assistant dashboard | [Dashboard streaming](docs/room-companion/dashboard-stream.md) |
| Extend the firmware or see what has been verified on hardware | [Architecture](docs/room-companion/architecture.md) · [Extensions](docs/room-companion/extensions.md) · [Validation record](docs/room-companion/validation.md) |

## Development

See [AGENTS.md](AGENTS.md) for the devserv workflow. In the pinned ESPHome
development container, run `bash scripts/validate-room-companion.sh` to
validate both models, their wake-word variants, and the extension example. Add
`--compile` to build the same matrix.

## License

Room Companion is licensed under **AGPL-3.0-only** and preserves the Git history
of the earlier fixtSE OpenNextion ESPHome project. See [LICENSE](LICENSE) and
[COPYRIGHT](COPYRIGHT).

OpenNextion’s original [MIT notice](LICENSES/OpenNextion-MIT.txt) is preserved.
Third-party code, fonts, and assets retain their own terms. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
