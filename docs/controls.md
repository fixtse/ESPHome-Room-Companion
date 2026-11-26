# Using the panel

[All guides](../README.md#find-your-guide) · [Change your settings](web-configuration.md) · [Screen behaviour](room-companion/panel-ui.md)

Swipe left or right on the main view to browse your controls, or tap an icon in
the bottom bar to jump to one. Tap a control to open it; swipe up or down to
adjust it. Swipe **left or right** to go back. Pull down from the **top
edge** on any screen to open notifications.

| Gesture | What it does |
| --- | --- |
| Tap | Press: toggle, confirm, play/pause, run |
| Touch and hold (about 0.35 s) | Hold: choose a secondary setting, open playlists |
| Swipe up | Step up: increase, next |
| Swipe down | Step down: decrease, previous |
| Swipe left or right on an opened control | Back to the main view |
| Swipe down from the top edge | Notification centre |

A longer swipe applies several steps. **Swipe sensitivity** (Device settings)
sets how many steps one swipe position moves.

## Everyday controls

| Control | Swipe | Tap | Hold, then swipe |
| --- | --- | --- | --- |
| Lights | Adjust the selected light setting | Switch on/off | Choose another supported light setting |
| Climate | Adjust the selected temperature or setting | Change heating/cooling mode, confirm an option, or run your custom tap action | Choose temperature, range endpoints, preset, fan, swing or humidity where supported |
| Fans | Adjust speed or preview an option | Switch on/off or confirm an option | Choose speed, preset, oscillation or direction where supported |
| Numbers | Adjust the value | No extra action needed | — |
| Choices | Browse the options | Confirm your choice | — |
| Scenes and buttons | — | Activate | — |
| Media players | Up / down: volume | Play/pause (or the transport buttons) | Next / previous track |
| Cameras | — | Tap snapshot to refresh; use icon buttons for refresh, power, motion detection, and optional ONVIF PTZ | — |
| Vacuums | Preview fan speed (when supported) | Start / pause cleaning, or use icon buttons for stop, dock, locate, spot clean, and fan speed | — |
| Locks | — | Lock / unlock (or enter PIN on the detail screen when a code is required) | — |
| Alarms | — | Enter PIN (when required) and tap Disarm or an arm mode (Home / Away / Night) | — |
| Weather | Browse readings and daily forecasts | Retry unavailable forecasts | — |

Some devices offer fewer options. Home Assistant must allow the panel to perform
actions: see [connection setup](setup.md#add-it-to-home-assistant).

A brief checkmark means the request was sent, rather than a guarantee that the
other device finished it. If **No confirmation** appears, check the device and
permissions in Home Assistant. Missing or unavailable readings show clearly;
reconnecting does not replay old adjustments.

## Music and playlists

The media player shows its artwork, title, artist, volume, a progress bar in the
accent color and previous / play-pause / next buttons. Swipe up or down for
volume; swipe left or right to leave the player. When idle, tapping can run the
script or automation selected under **Press action**.

[Add playlists or radio stations](web-configuration.md#playlists-and-stations)
in that player's **Playlists** tab first. Then:

1. Touch and hold until **Release now / Open playlists** appears (about half a second), then release.
2. Swipe through the list to highlight an entry.
3. Tap to start it. The screen returns to the media player.

Holding past the playlist cue (about 1.5 seconds) returns to the main view
instead. To open playlists with a tap while the player is idle, choose **When
idle → Open playlists** for that slot.

When the media player reports **off**, its view closes and returns to the main
view. **Idle** and **paused** keep the player open.

## Your own swipe and tap actions

The webpage's **Gestures** tab can replace the ordinary swipe up and swipe down
steps independently; held swipes are configured separately. For example, a
sensor can show computer volume while swiping runs the Home Assistant script
that changes it. Custom swipes apply while that control is open, so main-view
navigation is unchanged. Climate controls also accept a custom **Press action**.
[Set up custom actions and feedback](gesture-controls.md).

## Timers and reminders

Active supported timers can appear automatically, even when their pages are
hidden. Swipe to return to browsing; an active timer may return after ten idle
seconds. [Timer setup](web-configuration.md#timers-without-clutter).

Notifications from Home Assistant appear as a banner and stay in the
notification centre (pull down from the top edge, or tap the bell on the main
view). [Create reminders and calendar messages](display-notifications.md).

## When you leave it idle

Choose **Clock**, **Dashboard** or **ImmichFrame** as the idle screen. Timers
and scheduled sleep keep priority. [Display and sleep options](web-configuration.md#display-and-idle-screen).

## Reset Wi-Fi and device configuration

In normal mode, touch and hold the screen for **10 seconds** to clear saved
Wi-Fi credentials and device configuration, then reboot. You will need to
reconnect Wi-Fi and configure your devices again, or restore a configuration
backup. The reset is unavailable during sleep, while waking from standby and
while viewing an offline device. A touch used to dismiss a notification cannot
trigger the reset.

## Sensor graphs and value bars

Sensors with a graph open it at their default time (12 hours unless changed) with the maximum and minimum beside the gridlines.
Numbers, input numbers, fan percentages and percentage sensors show a straight
progress bar in the accent color, using the range advertised by Home Assistant.

## Light controls

Open a light to adjust brightness by default. Hold, then swipe to select RGB or
color temperature, release, then swipe to adjust. Only modes reported by Home
Assistant are available.

## Switch power and energy

In a switch slot's **Display → Power & energy**, select a secondary sensor in W
or kWh. The main view shows it beside On/Off. Open the switch for its 12-hour
history graph; a tap still toggles the switch.
