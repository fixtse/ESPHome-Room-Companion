# Icons, album art and playback progress

[All guides](../README.md#find-your-guide) · [Playlists](web-configuration.md#playlists-and-stations)

The panel uses the icons you chose in Home Assistant and shows album art when
your media player provides it. Below the artwork, a progress bar in the accent
color follows the track's progress. Live radio or tracks without a known
duration do not show made-up progress.

## Change the look

- Change an entity's icon in Home Assistant, then open the panel webpage's **Home Assistant** section and click **Refresh entity list** to sync saved controls.
- Pick the accent color in **Device settings → Accent color**, then **Save device settings**. Paused playback uses the same color at lower brightness.
- If an icon cannot load, a built-in icon keeps the control recognizable. New downloaded icons need internet access.
- If artwork is missing, check whether that player provides artwork in Home Assistant. **Download diagnostics** can help someone investigate a persistent problem.

[Display settings](web-configuration.md#display-and-idle-screen) · [Media controls](controls.md#music-and-playlists)

## Technical details

The panel displays the MDI icon chosen for each Home Assistant entity. On the
configuration page, refresh the entity list and save the affected slots to copy
registry icon overrides. Native API `icon` attribute changes are also followed
when there is no saved registry override. Icon metadata is included in configuration
exports; older backups without icon fields still import.

With the [Room Companion integration](https://github.com/fixtse/ha-room-companion)
installed, the panel fetches icons, color emoji and media covers from Home
Assistant over the local network instead: Home Assistant downloads each one
once, keeps it in a cache and serves covers already cropped to the panel's
size. This is faster and needs no HTTPS on the panel. When Home Assistant does
not answer, the panel uses the internet sources below for five minutes.

Without the integration, icons download over verified HTTPS from `api.iconify.design` and are rasterized on
the panel in a background task. This needs internet access for an uncached icon.
No Home Assistant token or entity ID is sent to the icon service. Only the MDI name
is requested. Downloads have byte and time limits, with no arbitrary URL support.

A bounded RAM cache avoids repeat downloads while browsing. Cached images remain
available during network outages; after restarting the panel, icons download again.
Failed downloads retry after five minutes. Built-in icons for lights, fans,
climate, media, covers, sensors, helpers, scenes, buttons, scripts, automations,
weather, timers, and switches remain available offline. Unsupported icon namespaces
also use the entity-type fallback. Media album artwork takes priority on the
primary media view. It loads directly from HA's supplied artwork URL, including
its artwork access parameter where provided; no device-stored bearer token is used.

The media detail page has a straight progress bar above the transport buttons. It
uses Home Assistant's `media_position`, `media_position_updated_at`, and
`media_duration`, with smooth local updates every 100ms while playing. Pausing
freezes the bar; seek feedback and track changes reset
the progress anchor. Idle, unavailable, disconnected, and unknown-duration/live
streams hide the bar instead of displaying invented progress. This indicator
shows progress and does not seek when operated.

# Recovering entity state

Missing and unavailable entities are retried automatically, starting after ten
seconds and backing off to five minutes. Recovery reads refresh the existing
Native API callbacks without adding subscriptions or replaying commands.

For an immediate retry, short-tap the screen while an unavailable entity's detail
page is open. You can also open **Home Assistant** on the configuration webpage
and select **Retry panel entity states**. The webpage action renews the Native API
connection so Home Assistant restores all subscriptions. Neither method restarts
the device or changes Wi-Fi credentials. An entity that still does not exist in
Home Assistant remains unavailable until it is created.

Upgrading from older firmware removes its token entry, token-status sensor,
“How to use” sensor, and QR-code menus, and deletes the old saved device token.
Home Assistant may retain old unavailable entity-registry entries after removal
from firmware; those entries can be deleted from the device's entity list.

## Performance and reset diagnostics

Icon bindings reuse unchanged LVGL descriptors and tint styles. Visible icons
enter a deduplicated priority queue ahead of emoji and off-screen background
prefetches (configured entity and page icons after two seconds without input),
and drain back-to-back over one HTTP/1.1 keep-alive TLS connection per host
inside a burst-scoped worker. When the burst ends or media artwork requests the
network, the worker closes its TLS client and frees its stack; background
prefetches cancel cooperatively and re-queue without counting as failures.
SVG downloads, NanoSVG parsing and the bounded PSRAM bitmap cache (up to 120
entries, gated by 1.5 MB free PSRAM headroom) stay in PSRAM. Icons download
again after reboot: no flash partition or NVS space is consumed for bitmap
storage. Failed icons use exponential backoff (2 s → 5 s → 15 s → 60 s → 5 min;
confirmed 404s jump straight to 5 min) and transient failures clear
automatically on Wi-Fi reconnect. Repeated primary-host transport/refusal errors
temporarily prefer the GitHub backup host for 60 s before probing Iconify again.

Artwork remains provider-independent: Home Assistant's supplied URL is fetched,
and the final display buffer is resized to fit the panel's cover size. This does
not shrink the original network file; JPEG requires the complete compressed
download and still has decode cost for the source dimensions. Artwork is
postponed until its URL has settled, while icons are yielding, and when internal
memory is low. Artwork uses strict per-phase timeouts and a 10 s HTTPS / 7 s
HTTP network budget, a bounded 4-entry PSRAM LRU cache, and exponential backoff
(1.5 s → 3.5 s → 8 s → 20 s → 30 s) on failed URLs. Compressed input stays in a
bounded PSRAM buffer. Existing OTA request timeouts are kept.

Download completion refreshes on the next interval, after downloader cleanup.
Unavailable players cancel in-progress artwork and discard stale progress.
Unchanged media/playlist labels and image sources avoid repeated redraws.

The configuration webpage and diagnostic download include readable reset reason,
free/minimum/largest internal heap, free PSRAM, artwork status/stats, and icon
downloader stats (`icon_stats`). An RTC RAM breadcrumb can preserve the last
media action or artwork event across a crash or software reset. It is the last
observed event, not a crash backtrace or proof of cause; power-on and brownout
discard it. No continuous log or flash writes are required. Hardware crash
reproduction remains unverified without a reset log.

Artwork and icon DNS, TCP/TLS, and body reads run in separate worker tasks so
slow servers never block panel input. Both workers prefer an internal stack when
internal RAM has ample headroom and fall back to PSRAM when fragmented; both
require at least 32 KB free internal RAM and an 8 KB largest block for lwIP and
Wi-Fi headroom (reported as `waiting_for_memory` when deferred). mbedTLS
allocations live in PSRAM, covering icon/artwork HTTPS and OTA checks/downloads.
JPEG decoder objects (including MCU/Huffman workspaces) use checked PSRAM
allocation rather than the fragmented internal heap.

HTTP diagnostics also report the most recent Wi-Fi RSSI (sampled every 60 seconds),
Wi-Fi disconnect count since boot, and the last artwork/icon download errors.
A connected Wi-Fi flag alone is not a measurement of signal quality or internet
reachability. Artwork error messages omit URLs and access parameters.
