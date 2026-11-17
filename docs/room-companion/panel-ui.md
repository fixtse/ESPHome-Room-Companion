# Panel screen behaviour

The rectangular panels run the knob application unchanged and add a thin panel
layer on top (`onx_panel_ui.h`, `packages/room_companion/panel.yaml`).

## Gestures

| Where | Gesture | Result |
| --- | --- | --- |
| Carousel | Swipe left / right | Previous / next control |
| Carousel | Tap an icon in the bottom strip | Jump to that control |
| Detail view (weather, light…) | Swipe left / right or up / down | Same as turning the knob (pages, brightness, volume) |
| Media player | Swipe left / right | Close the player without changing playback |
| Media player | Swipe up / down | Change volume |
| Detail view | Swipe right **starting at the left edge** | Back |
| Anywhere in the native UI | Swipe down **from the top edge** | Open the notification centre |
| Dashboard stream | Swipe left / right | Back to the native UI |

## Top corners

- **Top right: bell.** Always visible (dimmed when there is nothing). Shows the
  unread count; opens the notification centre: list, detail with picture,
  dismiss and clear all. A banner announces new notifications. Home Assistant
  sends them with the device's *show message* action.
- **Top left: dashboard.** Shown once a Remote WebView server is configured;
  opens the dashboard stream.

## Media detail

Centered cover art stays above a single title and artist, volume, room name and
a straight progress bar. The circular playback ring is hidden. Use the previous,
play/pause and next buttons for transport. Tap the top-centre close button or
swipe horizontally to leave the player; vertical swipes change volume.

While the panel is idle (including the clock), a configured player that starts
playing opens automatically. Playback stopping restores the previous view unless
you have interacted with the player. Sleep and notification priorities still apply.

## Idle screen

Home Assistant entities (device configuration):

- **Idle screen**: *Clock* (default), *Photo slideshow* or *Dashboard*. The idle
  screen appears after the standby timeout.
- **Slideshow pictures**: picture addresses separated by spaces, commas or
  new lines (JPEG/PNG over HTTP, e.g. `http://homeassistant.local:8123/local/photos/1.jpg`).
  An address that returns a different picture each time (a camera snapshot or a
  random-photo service) also works on its own.
- **Slideshow interval**: seconds per picture (5–600).

*Dashboard* opens the stream when the panel goes idle; it needs a configured
server and is limited by the stream's frame rate.

## Not yet done

- Configurable page layouts (two controls per page, or one control plus three
  sensor values). The knob pages are still one control per page.
