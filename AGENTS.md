# Room Companion

Read `/home/fixt/AGENTS.md`. Builds, installs, tests and services run on devserv
under a task lease and isolated checkout. Do not flash a board unless requested.

Preserve the project's Git history, AGPL license, copyright and the upstream
notice in `THIRD_PARTY_NOTICES.md`. This repository targets only the 3.5"
(ONX3248G035) and 2.8" (ONX2432G028) rectangular panels; all settings,
documentation and UI text describe those devices. Prefer extracting/reusing the
shared
state/configuration logic over reimplementing it. New rectangular firmware lives
in `packages/room_companion/`; both model roots use one shared application.

Pin ESPHome to 2026.11.0. Validate and compile both model roots after shared
changes. If wake-word/extension examples change, also compile the example matrix
using `scripts/validate-room-companion.sh --compile` in the pinned container.
Never commit secrets, keys, `.env`, firmware blobs or private HA state. Keep
hardware-tested status separate from configuration/build validation.

## Current working hardware (verified 2026-11-17)

- The connected, hardware-tested panel is **ONX3248G035**, the **3.5-inch
  320×480 ST7796** model: ESP32-S3, 16 MB flash, 8 MB octal PSRAM.
- Its USB bridge is CH340: `/dev/ttyUSB0`, normally also
  `/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0`. Verify the attachment before
  flashing; device paths and DHCP addresses can change. Last verified chip MAC:
  `80:b5:4e:db:c6:e4`; last Wi-Fi address: `192.168.10.184`.
- The currently working firmware root is **`ONX3248G035.wake-word.yaml`**. It
  includes `ONX3248G035.yaml` and enables the local "Okay Nabu" wake word by
  default. Preserve this variant when updating this board unless told otherwise.
  Enabled/configured does not mean microphone, wake-word and speaker operation
  have all been hardware-tested.
- Latest successful build/flash: ESPHome **2026.10.2**, production config hash
  `0xfc7425ea`, build timestamp `2026-11-17 20:41:49 UTC`, factory image
  SHA-256 prefix `f8c3d70b`, source `61bb877` plus the uncommitted internal-RAM
  budget and Bluetooth start-guard changes. Version text remains
  `2026.10.2.72-panel.1`. Neither that text nor the YAML config hash alone can
  distinguish local builds after C++ header changes; record source hashes and
  the build timestamp too.
- USB flash hash verification, boot without panic and Wi-Fi/web/API checks
  passed, plus a preset save/delete NVS round trip. Internal RAM settled at
  77–90 KB free with a 37–49 KB largest block (previously 5–14 KB; the screen
  froze on failed SPI DMA allocations). Settled `sys` loop peaks were 82–90 ms.
  See `docs/room-companion/memory-budget.md` before adding features that use
  internal RAM (task stacks, DMA, Bluetooth). Not hardware-tested in this build:
  opening the dashboard stream, Assist, wake word and audio, visual appearance
  and every gesture.

## Building and flashing this panel

A request to build is not permission to flash. Once flashing is requested,
complete the build, transfer, flash and flash-hash verification without asking again for
routine steps. Honor an explicit model restriction such as "only compile 3.5";
otherwise keep the model/example validation policy above.

1. Acquire the devserv lease and use an isolated checkout as required by the
   global policy. Synchronize the exact local changes and supply `secrets.yaml`
   from the existing private configuration without printing or committing it.
   The last task's checkout was
   `192.168.31.35:~/agent-work/room-companion-loop-20261117`; it is a reference,
   not a checkout to overwrite or reuse without checking ownership and changes.
2. Build on devserv, from that task's checkout, using the pinned image:

   ```sh
   docker run --rm --name rc-35-TASK -v "$PWD":/config \
     ghcr.io/esphome/esphome:2026.11.0 compile ONX3248G035.wake-word.yaml
   ```

   Use a unique container name. If adding/removing profiling components leaves
   stale component targets, run `esphome clean ONX3248G035.wake-word.yaml` in the
   same pinned container, then compile again. Do not ship temporary verbose
   `runtime_stats` instrumentation as the production build.
3. Copy the resulting **factory image** from devserv to a temporary directory
   outside this repository on the PC physically attached to the board:

   ```text
   .esphome/build/onx3248g035/.pioenvs/onx3248g035/firmware.factory.bin
   ```

   All compilation stays on devserv. The verified physical USB write uses the
   attached PC's existing esptool installation (no local dependency install):

   ```sh
   /home/fixt/.espressif/tools/python/v6.1/venv/bin/python \
     /home/fixt/.espressif/v6.1/esp-idf/components/esptool_py/esptool/esptool.py \
     --chip esp32s3 --port /dev/ttyUSB0 --baud 460800 \
     write-flash 0 /tmp/TASK/firmware.factory.bin
   ```

   Use the actual transferred image path. Write the factory image at **offset
   zero**; do not substitute the OTA application image at that offset. Normal
   esptool DTR/RTS reset entered the bootloader and restarted this board without
   manual button presses. Require `Hash of data verified.` before claiming the
   flash succeeded. Do not run `erase-flash`: the working procedure retains the
   provisioned NVS/Wi-Fi configuration.
4. Ensure no serial monitor or other flasher owns the port. One attempt returned
   a serial readiness/disconnection error; a retry after checking the port
   succeeded. Diagnose ownership/reset/connection issues rather than launching
   overlapping flash attempts. Routine post-flash boot observation is not required;
   capture boot logs only when requested or needed to diagnose a failure. If
   reading UART, use **115200 baud**, **DTR and RTS false**, a bounded capture and
   a read timeout. Opening a monitor with asserted control lines can reset or
   hold the board in reset. Keep visual/audio verification separate from
   serial/network checks. Leave firmware and private logs outside Git.
5. After all remote work finishes, release the lease exactly once. The existing
   automation shuts devserv down after three minutes with no leases; never force
   it off or reset the shared counter.

### OTA status

USB is the verified recovery/update method for this attached panel. Native
ESPHome OTA is configured, but recent attempts failed or stalled with a closed
connection during transfer; do not claim it is a proven replacement for USB.
When OTA is explicitly used, run the pinned container's
`upload ONX3248G035.wake-word.yaml --device ADDRESS` on devserv and make sure that
address is reachable from the container. Earlier attempts used an SSH reverse
forward to the panel's TCP port 3232 and host networking; devserv's loopback is
not the panel without that tunnel. Keep OTA credentials in private secrets.

The HTTP release-manifest updater uses the `esp-ota` release manifest described
below. Publishing a valid manifest is separate from hardware-testing an HTTP OTA
transfer; do not claim the latter based only on a successful build or USB flash.

## OTA repository updates

- Every new firmware version published to OTA must use a new, incremented
  numeric version suffix (for example, `.86` → `.87`); never reuse a published
  version number. Update the firmware's `product_firmware_version` and the OTA
  repository's project version together so the device reports the published
  version correctly.
- The 3.5-inch wake-word device is also released through
  `/home/fixt/git/esp-ota` (`fixtse/esp-ota`). After firmware changes, update
  `configs/onx3248g035.yaml` there to the exact published source commit and
  increment its project version. Preserve `ONX3248G035.wake-word.yaml` as the
  release root. Keep this device's compiler pinned to ESPHome **2026.11.0**.
- Build and validate the OTA integration on devserv under the task lease, then
  publish the OTA application image using that repository's release tooling.
  Verify the release asset checksum and
  `ota/releases/onx3248g035_manifest.yml`. Firmware points to
  `https://raw.githubusercontent.com/fixtse/esp-ota/main/ota/releases/onx3248g035_manifest.yml`.
- Public firmware uses the unencrypted native API requested by the owner.
  Never embed private API keys, OTA passwords or Wi-Fi credentials in releases.
  USB flashes still use the factory image at offset zero; release OTA assets
  must use `firmware.ota.bin`, never the factory image.
