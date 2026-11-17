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

In Home Assistant, configure an Assist pipeline including STT and TTS. Add the
ESPHome device and select that pipeline for its Assist satellite. Tap Talk, say a
short command, check the transcript, then check reply text and speaker output.
No HA long-lived access token is needed by the panel. The microphone-mute switch
prevents new captures and stops the running pipeline; it is a software control,
not physical electrical isolation. Default playback volume is 35%.

The optional wake-word package is `examples/room-companion/wake-word.yaml`.
Add it through `packages:` after verifying tap-to-talk. Enable its switch in HA;
it is intentionally off at boot and is disabled when muted or disconnected.
Local wake-word detection uses additional CPU/RAM. It is not an echo-cancellation
implementation, and successful compilation does not verify acoustic performance.

Improv BLE is active only while Wi-Fi is disconnected; press BOOT to authorize
provisioning for 60 seconds. BLE is disabled after connecting to free resources
for audio. No Bluetooth proxy, camera or SD support is enabled by default, leaving resources
for LVGL and audio. Extensions must avoid occupied pins and both I2S peripherals.
