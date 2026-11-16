# Architecture and software ancestry

The main software base is [fixtSE's OpenNextion Knob](https://github.com/fixtse/OpenNextion-Example-ESPHome_KNOB_Control),
from local commit `6a710e2ce0258e3ced31da3177e00ed5f0b260e3`. Its Git history,
headers, components and original entry point are preserved. New work lives in
`packages/room_companion/`, avoiding a rewrite of the original knob firmware.

Both rectangular entry points merge the same `application.yaml`. A board profile
supplies dimensions, driver, title font and rendered-card dimensions. Shared
hardware defines I2C, the expander, SPI, touch, backlight, microphone and speaker.
The new application calls the original `onx_notification::Message`,
`onx_notification::plain_text`, `onx_control::number` and
`onx_media::volume_percent` implementations. Standard ESPHome YAML remains the
extension interface. Existing knob features can be ported into shared packages
without duplicating either screen's application.

## Rendering choices

Native LVGL owns the display and touch navigation. Normal controls and Assist do
not depend on a rendering service. The optional Today page downloads an
authenticated JPEG when explicitly requested. This first renderer draws bounded
HA state cards with Pillow, not Chromium; it demonstrates the transport and
resolution split rather than claiming to reproduce a Lovelace dashboard.

[crow-ha](https://github.com/fintros/crow-ha/blob/master/crow_ws_panel/DOCS.md)
provided the idea of rendering rich content on a server and returning input.
Its documented client targets an ESP32-P4 panel; it is not the firmware base here.

The user-supplied [Waveshare example](https://github.com/ay129-35MR/Waveshare-ESP32-S3-Touch-LCD-4-Home-Assistant-Display)
uses [RemoteWebViewClient](https://github.com/strange-v/RemoteWebViewClient), an
ESPHome external component, and [RemoteWebViewServer](https://github.com/strange-v/RemoteWebViewServer).
That is a more direct candidate for a future full-dashboard mode on ESP32-S3.
The server supports per-client resolution and interactive touch forwarding.

The Waveshare YAML is a hardware example, not a drop-in configuration: its actual
YAML uses an ST7701S RGB display, GT911 touch and PCA9554 expander, while these
OpenNextion boards use SPI ST7789/ST7796 displays, CST826 touch and PCF8574.
Its README's ST7796 description differs from its YAML, so use verified board
schematics and code when porting.

A later RemoteWebView package must explicitly arbitrate display ownership and
touch routing with LVGL, stop or throttle JPEG work during Assist, handle renderer
loss, and pin tested external-component revisions. Adding it alongside LVGL
without that arbitration would let both renderers write to the same display.
It is deliberately not advertised as implemented or tested in this first port.

## Next ports from the knob

1. Separate the 20-slot configuration/preset store from round-renderer callbacks.
2. Map press/turn/held-turn actions to touch widgets, sliders and long presses.
3. Port registry browsing, feature-aware entity controls and media artwork.
4. Add standby, timers and notification-image scheduling around Assist priority.
5. Benchmark an optional interactive RemoteWebView mode on both SPI displays.

The initial UI uses substituted HA entity IDs. It does not yet expose the knob's
web editor on these rectangular firmware entry points.
