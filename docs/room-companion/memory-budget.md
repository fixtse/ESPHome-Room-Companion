# Internal RAM budget, 2026-11-17

The ESP32-S3 has about 512 KB of internal SRAM shared by code placed in IRAM,
static data, task stacks, DMA buffers and the heap. The panel's 8 MB PSRAM is
plentiful but cannot hold DMA buffers for the SPI display, I2S microphone or
Wi-Fi, or most task stacks. When internal RAM ran out the panel showed
`setup_dma_priv_buffer: Failed to allocate priv TX buffer` (frozen screen),
failed to start the dashboard WebSocket task, crashed while starting Bluetooth,
and sometimes stopped answering on the network.

## Measurements (ONX3248G035, wake-word root, restored configuration)

Static (linker map of the production build):

| Region | Bytes | Main contributors |
| --- | ---: | --- |
| IRAM code | 97.7 KB | Bluetooth controller 16.5 KB, flash driver 10.7 KB, Wi-Fi `pp`/`net80211`/PHY 16 KB |
| `.data` | 51 KB | application tables 26 KB |
| `.bss` | 121 KB | application 99 KB: `JPEGDEC` inside the dashboard stream 18 KB, loop stack 16 KB, entity/config tables about 26 KB; Bluetooth host 10 KB |

Runtime (temporary heap task tracking, `diag-ram.yaml`, not shipped):

| Boot phase | Free internal | Step |
| --- | ---: | ---: |
| Before component setup | 160 KB | |
| Wi-Fi started (priority 250) | 101 KB | −53 KB |
| After Wi-Fi / connection components | 87 KB | −14 KB |
| Panel application and dashboard stream (LATE) | 28 KB | −59 KB |
| Connected to Home Assistant, wake word running | 5–14 KB | −14 to −23 KB |

Internal heap owners once settled: `loopTask` 117 KB in 1,880 blocks (about
1,800 of them are 16–128 byte strings/vectors that `SPIRAM_MALLOC_ALWAYSINTERNAL`
forced into internal RAM, plus the stacks of tasks it created), Wi-Fi 29 KB,
`ipc0` 15 KB. The dashboard stream connected at boot whenever a server was
configured, even if the dashboard was never opened; its WebSocket task peaked
at 66 KB and retried every 5 s when it could not get a task stack. Each LVGL
flush passed a PSRAM buffer of up to 38 KB to the SPI driver, which then needed
one contiguous internal DMA block of the same size.

## Plan

1. **Small allocations to PSRAM.** `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL: 0`.
   ESP-IDF drivers that need internal or DMA memory request it with explicit
   capabilities; FreeRTOS objects and task stacks stay internal. Expected
   50–60 KB.
2. **Display transfers without large DMA requests.** The ILI9xxx driver copies
   PSRAM pixels through one permanent 4 KB internal DMA buffer instead of
   letting the SPI driver allocate a frame-sized bounce buffer per flush.
3. **Dashboard stream only when used.** Start the stream on the first
   dashboard open instead of at boot; allocate the `JPEGDEC` state and task
   stacks in PSRAM. Expected 18 KB static plus up to 66 KB runtime peak.
   A live camera view starts the same stream (same internal cost) and adds
   two screen-sized RGB565 buffers in PSRAM only (2 × 300 KB on the 3.5-inch,
   2 × 150 KB on the 2.8-inch), freed when the camera closes. Camera sound
   uses the speaker's existing media pipeline.
4. **Application tables to PSRAM.** Large entity, configuration, area and
   light-feedback tables are allocated in PSRAM at start-up. Expected ~25 KB.
5. **Wi-Fi buffers.** Fewer static RX buffers and smaller AMPDU windows, sized
   for a panel rather than a throughput router. Expected ~10 KB.
6. **Kept as is:** the Bluetooth stack (Improv provisioning; it is not started
   once Wi-Fi works, but its code and tables cost about 27 KB). Removing it
   would change provisioning and is left as an explicit future decision.

Target: at least 60 KB internal free and a 32 KB largest block once settled.

## Results (steps 1–5, hardware-measured on ONX3248G035)

Production root `ONX3248G035.wake-word.yaml`, ESPHome 2026.10.2, config hash
`0xfc7425ea`, build `2026-11-17 20:41:49 UTC`, factory image SHA-256 prefix
`f8c3d70b`, flashed over USB with the provisioned NVS kept.

| | Before | After |
| --- | ---: | ---: |
| Static `.data` + `.bss` | 172 KB | 124 KB |
| Free internal, settled | 5–14 KB | 77–90 KB |
| Largest internal block, settled | 4–8 KB | 37–49 KB |
| Minimum since boot | 5 KB | 40 KB |
| `Failed to allocate priv TX buffer` per 90 s | 14–65 | 0 |
| Settled `sys` loop peaks | 26–229 ms | 82–90 ms (startup 2.5 s, early peaks 147–178 ms) |

Checked on hardware: boot without panic or reset, Wi-Fi and Home Assistant
connected, configuration web page and API, and a preset save/delete NVS round
trip (configuration unchanged afterwards). Not checked on hardware: opening the
dashboard stream (decoder state and task stacks now in PSRAM), Assist, wake-word
detection and audio, every screen and gesture.

PSRAM use rose by roughly 0.7 MB as small allocations moved there; about
7.1 MB remains free.
