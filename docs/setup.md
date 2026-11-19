# Set up your panel

[All guides](../README.md#find-your-guide) · [Choose your controls](web-configuration.md)

You need an **ONX3248G035** (3.5") or **ONX2432G028** (2.8") panel, a Wi-Fi network and Home Assistant.
If your panel already has this firmware, start with Wi-Fi below. For a new or
unflashed device, see [first installation](#first-installation).

## Connect to Wi-Fi

### With Bluetooth

1. Open your compatible Improv Bluetooth setup tool on your phone or computer and choose the panel.
2. When the display says **Press BOOT to allow setup**, press the BOOT button once. Continue in the setup tool.
3. Choose your Wi-Fi network and enter its password. The panel shows that it is connecting.
4. When connected, the setup screen closes. If there are no controls yet, the panel shows its web address and the next steps.

If the screen says **Could not connect**, check the network and password, then
try again. Bluetooth support depends on your browser and setup tool; use the
hotspot below if Bluetooth setup is unavailable.

### With the Wi-Fi hotspot

1. Join the Wi-Fi network named **RoomCompanion-…** from your phone or computer.
2. Open **http://192.168.4.1** and enter your home Wi-Fi details.
3. Rejoin your home Wi-Fi after the panel connects.

A connected panel should show its normal controls or connected onboarding screen,
rather than ask you to join the hotspot again. Saved networks reconnect after
restarting; a temporary Wi-Fi outage does not mean you need to erase your setup.

## Add it to Home Assistant

1. In Home Assistant, open **Settings → Devices & services** and add the discovered **ESPHome** device. If it is not discovered, add ESPHome manually using the panel's IP address.
2. Open the integration's options and enable **Allow the device to perform Home Assistant actions**. Without this, readings may work while controls do not.
3. Open the device's **Visit** link, or type the web address shown on the panel into your browser.
4. On the panel webpage, open **Home Assistant** and sign in to browse your entities. An entity is a Home Assistant item such as a lamp, thermostat, sensor or script.
5. Open **Display & actions**, choose an entity and click **Save slot**. Add up to **20** controls as you need them.

Browser sign-in helps you choose items; adding the ESPHome integration connects
the panel. Both steps matter. No long-lived access token needs to be
copied into the device.

## Keep it updated

Use the panel's **Firmware Update** entity in Home Assistant to install available
wireless updates. Keep power connected during installation. The
[published releases](https://github.com/fixtse/esp-ota/releases) contain the wireless
`.ota.bin` image; it is not a first-install USB image.

Before changing a large setup, [download a configuration backup](web-configuration.md#backups-and-diagnostics).

## If something does not work

- **Readings appear but swiping or tapping does nothing:** check the Home Assistant action permission above.
- **The browser has no entity list:** sign in on the webpage's **Home Assistant** section. This is separate from the panel's connection.
- **A device is unavailable:** check it in Home Assistant first. Use **Retry panel entity states** on the webpage to request fresh readings.
- **Wi-Fi is temporarily down:** allow saved Wi-Fi to reconnect. Use a [configuration reset](controls.md#reset-wi-fi-and-device-configuration) only if you intend to start over.

## First installation

This section is for installing firmware on a new device. The supported hardware
is the **ONX3248G035** (3.5-inch, 320×480 ST7796) and the **ONX2432G028**
(2.8-inch, 240×320 ST7789V) touch panels, both ESP32-S3 with 16 MB flash and
8 MB PSRAM. Use a USB cable that supports data.

Build a factory image with **ESPHome 2026.11.0**:

```bash
docker run --rm -v "$PWD":/config -w /config \
  ghcr.io/esphome/esphome:2026.11.0 compile ONX3248G035.wake-word.yaml
```

Use `ONX3248G035.yaml` (no wake word) or `ONX2432G028.yaml` for the other
variants. Install the generated factory image at offset 0, for example with
[ESPHome Web](https://web.esphome.io/) in Chrome or Edge:

```text
.esphome/build/onx3248g035/.pioenvs/onx3248g035/firmware.factory.bin
```

If download mode does not start automatically, hold **BOOT**, tap **RESET** if
available, and release **BOOT** when uploading starts. Restart after installation,
then follow the Wi-Fi steps above. Compatible Improv tools can also set Wi-Fi over USB.
