# ONX3248G035 USB deployment — 2026-11-17

The connected 3.5-inch ONX3248G035 was flashed with Room Companion 0.1.0,
replacing OpenHAPanel 1.4.0. Build and flash commands ran on devserv using
`/home/fixt/agent-work/room-companion-20261117`; the local CH340 USB UART was
forwarded over a loopback-only SSH tunnel.

## Provisioning and console

Wi-Fi setup now uses the base project's provisioning pattern: native Improv BLE,
a physical BOOT-button authorizer (GPIO0), a 60-second authorization window,
BLE disabled two seconds after Wi-Fi connects and re-enabled on disconnect.
The default application has no compiled network credentials or setup AP.
Device credentials were generated into ignored, owner-readable `secrets.yaml`.
No keys or network credentials are committed.

The logger explicitly uses UART0 (GPIO43/44) for the CH340 connection. The S3's
default USB-Serial-JTAG logging selection would conflict with the PDM microphone
pins GPIO19/20 and would not expose application logs over this USB adapter.

## Validation

- Both hardware profiles compiled with Improv BLE and the final UART0 selection.
- The optional wake-word/extension build also passed with Improv BLE, before the
  UART-only logger correction; that example was not flashed.
- Final 3.5-inch application: 1,740,515 bytes reported flash use, 148,651 bytes
  reported static RAM. The written padded application image was 1,740,624 bytes.
- The standard bootloader, partition table, OTA-data and application offsets in
  the build's `flash_args` were written; all four hashes verified successfully.
- A local BLE scan detected the panel advertising as `room-companion-35`.
- The final UART log confirmed `setup() finished successfully!` and Wi-Fi
  `Connected`. Wi-Fi credentials provisioned over BLE survived the final reflash.

The compiler/ESPHome environment was `ghcr.io/esphome/esphome:2026.10.2`.
Final builds ran `esphome compile ONX3248G035.yaml` followed by
`esphome compile ONX2432G028.yaml` in that container. Flashing used
`python -m esptool --chip esp32s3 --port socket://127.0.0.1:43317 --baud 115200
--before no_reset --after no_reset write_flash @flash_args` from the 3.5-inch
native build directory. The device was then reset and monitored over UART0.

Physical display colors/touch calibration, microphone capture, speaker output,
wake-word behavior and an end-to-end Home Assistant Assist conversation still
require user testing. Successful component setup does not establish acoustic or
visual correctness. The 2.8-inch firmware was compiled but not flashed.
