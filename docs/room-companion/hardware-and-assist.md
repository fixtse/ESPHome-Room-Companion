# Hardware and Assist

| Signal | ONX2432G028 | ONX3248G035 |
|---|---|---|
| Display | ST7789V, 240×320 | ST7796, 320×480 |
| SPI CLK / MOSI / CS / DC | 5 / 1 / 2 / 3 | Same |
| Backlight | GPIO6 | Same |
| I2C SDA / SCL | GPIO8 / GPIO7 | Same |
| PCF8574 address | 0x38 | Same |
| LCD reset | PCF8574 P5 | Same |
| Speaker amplifier enable | PCF8574 P0, active high | Same |
| Touch | CST826, 0x15, polled | Same |
| PDM microphone CLK / DATA | GPIO19 / GPIO20 | Same |
| Speaker BCLK / DATA / LRCLK | GPIO14 / GPIO15 / GPIO16 | Same |
| Flash / PSRAM | 16 MB / 8 MB octal | Same |

Vendor expander enums are ONE-based: LCD_RST=6 and I2S_CTRL=1. ESPHome pin
numbers are ZERO-based: P5 and P0. Confusing these would break display/audio.
The vendor speaker example uses NS4168 with MSB-justified I2S, so this project
uses `stand_msb`. PDM RX is declared first to allocate I2S0; speaker TX uses I2S1.

Sources:
- [2.8-inch board](https://open-nextion-app-store.nextion.tech/devices/onx2432g028/)
- [3.5-inch board](https://open-nextion-app-store.nextion.tech/devices/onx3248g035/)
- [2.8-inch official code](https://github.com/OpenNextion/OpenNextion-SKU-ONX2432G028)
- [3.5-inch official code](https://github.com/OpenNextion/OpenNextion-SKU-ONX3248G035)
- [ESPHome Assist](https://esphome.io/components/voice_assistant/)
- [ESPHome PDM input](https://esphome.io/components/microphone/i2s_audio/)

## Commissioning

Attach the compatible PDM microphone board to MIC_PDM and a suitable speaker to
SPK, with power off. Use the correct FPC orientation and vendor accessory pinout.
After firmware upload, verify I2C detection, display colors/orientation, corner
touch coordinates and PSRAM initialization before testing audio.

### Dual microphone board

The Nextion Dual MIC board carries two PDM microphones 4 cm apart on the one
data line (GPIO20). The firmware captures both (`panel_microphone_pair`,
stereo: channel 0 is the board's right microphone, channel 1 its left one)
and `onx_mic_select` turns them into the mono `panel_microphone` that Assist,
the wake word and SoundSense use:

- **Auto** (default) tracks each microphone's noise floor and, while there is
  sound above it, a smoothed signal-to-noise score. The other microphone takes
  over when it scores more than 1.5 dB better, or within about 1 s when the
  current one goes silent while the other still hears sound (a failed or
  disconnected microphone). Switches crossfade over one 16 ms chunk.
- **Left** / **Right** force one microphone (*Assist microphone* select).
- *Assist microphone in use* reports the current one. Before selection,
  Assist used the left microphone (mono `channel: right`), so Auto starts there.

Measured with a stereo test build (2026.11.0.136/.137) over about 2 hours of
room sound, read back from Home Assistant: the right microphone is about
0.7 dB more sensitive, by the same amount in quiet and loud periods, so both
have the same signal-to-noise ratio. Ambient sound correlates at about 0.9
between the two (0.93 when loud), and inter-mic arrival offsets stay within the
±117 µs that 4 cm allows. Expect Auto to switch rarely: at 4 cm a talker a
metre away reaches both microphones almost equally, and general background
noise reaches both alike, so summing or beamforming them would gain only
1–2 dB. Stereo capture costs about 2 KB more internal DMA memory than mono.

## Wi-Fi provisioning

Both Improv transports are on by default; there is no setup access point and no
compiled SSID.

- **Improv Serial**: connect USB (CH340, GPIO43/44, also the log port), open
  [web.esphome.io](https://web.esphome.io) in Chrome or Edge, **Connect**, then
  **Configure Wi-Fi**. Afterwards it offers **Visit device**, the settings page.
- **Improv BLE**: Home Assistant's Bluetooth discovery or any Improv app. Press
  **BOOT** to authorize for 60 seconds. BLE is turned off two seconds after
  Wi-Fi connects (freeing memory for audio and the stream) and back on if Wi-Fi
  drops.

## Assist

In Home Assistant, configure an Assist pipeline including STT and TTS. Add the
ESPHome device and select that pipeline for its Assist satellite. Start a
request with the panel's **Talk** button (Assist page), the **BOOT** button, or
the **Talk** button entity; tap again to stop. In dashboard mode Assist takes
the screen and gives it back six seconds after the reply.

While Assist runs, a full-screen overlay (above the bottom bar) shows the status
(*Listening…*, *Thinking…*, *Speaking…* or an error), what was heard in quotes,
the reply in a card, and a large microphone button in the accent colour. Tap the
microphone to talk again or to stop; swipe horizontally to close the overlay.

No HA long-lived access token is needed by the panel. The microphone-mute switch
prevents new captures and stops the running pipeline; it is a software control,
not physical electrical isolation. Reply volume (default 60%) is on the
settings page and part of presets.

The OTA builds use `ONX2432G028.wake-word.yaml` and
`ONX3248G035.wake-word.yaml`, which enable local “Okay Nabu” by default.
To add wake word to a custom build, include `examples/room-companion/wake-word.yaml`
through `packages:` and enable its switch in Home Assistant. Detection pauses
when the microphone is muted or the Assist connection is unavailable.
Local wake-word detection keeps one CPU core busy and competes with dashboard
JPEG decoding; prefer tap-to-talk in dashboard mode. It is not an
echo-cancellation implementation, and successful compilation does not verify
acoustic performance.

No Bluetooth proxy, camera or SD support is enabled, leaving resources for
LVGL, audio and the stream. Extensions must avoid occupied pins and both I2S
peripherals.
