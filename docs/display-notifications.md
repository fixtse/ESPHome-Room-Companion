# Reminders and calendar messages on your panel

[All guides](../README.md#find-your-guide) · [Timers](web-configuration.md#timers-without-clutter)

Put a useful message where you can see it: washing finished, a door left open,
or your next meeting. Home Assistant sends the message; you do not need to give
it a display slot. Tap the screen to dismiss, or swipe to scroll longer text.
The underlying control is not changed by those movements.

## Send your first reminder without writing YAML

1. In Home Assistant, open **Settings → Automations & scenes** and create an automation.
2. Choose when it should run, such as a time of day or a sensor changing state.
3. Add an action and search for **show_message** under your panel's ESPHome device name.
4. Fill in **title** (for example, “Reminder”) and **message** (“Take the washing out”). Set **image_url** to a reachable image URL, or an empty string for text only.
5. Set **timeout_seconds** to 60, save the automation, and try its message action.

For a different duration, change **timeout_seconds**. Use 120 for two minutes, or 0 to keep it until you dismiss it.
Use **dismiss_message** in an automation to clear it remotely. If the actions
are missing, update the panel and let its ESPHome integration reconnect.

Notifications cover timer popups by default; timers keep counting underneath and
reappear when the message closes. In website **Device settings → Notifications**,
turn off **Allow notifications to cover timers** to give timer popups priority.
Messages wait while the screen is deliberately off, during scheduled sleep or
during setup. Only the newest message is kept,
and messages do not survive restarting the panel.

## Copy-and-adapt examples

These examples are for Home Assistant's YAML editor. The calendar example below
shows a meeting title automatically, rather than needing to type it each time.

The panel exposes ESPHome actions in Home Assistant for reminders, calendar events,
and messages from other automations. Install the firmware and let Home Assistant
reconnect to discover the new actions. In Developer Tools → Actions, search for
`show_message` under your panel's ESPHome device name.

The examples use `onx3248g035_dbbc54`; replace that prefix if your device has a
different name.

## Show a reminder

```yaml
action: esphome.onx3248g035_dbbc54_show_message
data:
  title: Reminder
  message: Take the washing out.
  image_url: ""
  timeout_seconds: 60
```

The message appears for 60 seconds. Tap the screen to dismiss it, or swipe to
scroll longer text. Dismissing and scrolling do not operate the page underneath.
The underlying page is retained.

Holding a notification-dismiss touch for 10 seconds does not reset the device or
erase its settings. The entire touch is consumed until release, including if the
message times out or is dismissed remotely. A notification appearing during an
existing hold also consumes that touch. See [reset Wi-Fi and device configuration](controls.md#reset-wi-fi-and-device-configuration)
for the separate 10-second reset gesture in normal mode.

For a custom timeout, use `show_message`:

```yaml
action: esphome.onx3248g035_dbbc54_show_message
data:
  title: Next meeting
  message: Team catch-up in ten minutes. Bring the project notes.
  image_url: ""
  timeout_seconds: 120
```

Set `timeout_seconds: 0` to keep a message until dismissed. Positive timeouts are
limited to one hour. An empty title displays as “Reminder”. Empty messages are
ignored. Titles are limited to 96 UTF-8 bytes and messages to 768 UTF-8 bytes.
Only the newest message is retained, including while the display is unavailable.
Messages are temporary and do not survive a reboot.

To dismiss remotely:

```yaml
action: esphome.onx3248g035_dbbc54_dismiss_message
```

## Calendar reminder

Replace `calendar.work` with your calendar entity. This automation displays each
meeting's title ten minutes before it starts:

```yaml
alias: Meeting reminder on panel
triggers:
  - trigger: calendar
    entity_id: calendar.work
    event: start
    offset: "-00:10:00"
actions:
  - action: esphome.onx3248g035_dbbc54_show_message
    data:
      title: Meeting in 10 minutes
      message: "{{ trigger.calendar_event.summary }}"
      image_url: ""
      timeout_seconds: 60
mode: queued
```

Use a future calendar event when checking the automation; manually running its
actions does not provide `trigger.calendar_event`.

## Display priority

Messages appear over the current view, including the idle clock or idle media
player. Timer popups have priority and pause a message's timeout. Scheduled sleep,
a manually switched-off screen, and provisioning defer messages until the display
is available. The timeout starts when the message becomes visible and resumes
after a timer popup closes.

References: [ESPHome user-defined actions](https://esphome.io/components/api/#user-defined-actions),
[Home Assistant calendar automations](https://www.home-assistant.io/integrations/calendar/),
and [calendar trigger data](https://www.home-assistant.io/docs/automation/trigger-data/#calendar).

## Include an image

The single `show_message` action accepts `image_url` and `timeout_seconds`. Set the URL to
an empty string (`""`) for a text-only notification. After this update, add that
field and `timeout_seconds` to existing Home Assistant calls. The separate
`show_message_with_timeout` action has been consolidated into `show_message`. The URL must be an absolute HTTP or HTTPS
address reachable directly from the panel; browser sign-in and Home Assistant
session cookies are not forwarded.

```yaml
action: esphome.onx3248g035_dbbc54_show_message
data:
  title: Front door
  message: Someone is at the door.
  image_url: "https://example.com/door-snapshot.jpg"
  timeout_seconds: 60
```

Use a publicly readable or privately hosted image URL that the panel can access
without authentication. JPEG, PNG, and BMP are supported where the decoder can
read them; progressive JPEG is unsupported. Downloads are limited to 512 KiB,
source images to 1,048,576 pixels. The rectangular panels show the image as a
small thumbnail beside the notification title in the dropdown, expanded view,
and arrival popup, leaving the message area for text. Dropdown thumbnails use a 44-unit square; popup and expanded-title icons use
a compact 28-unit square. Both are scaled for the panel and are retained in PSRAM while their notifications
remain in the history. A bell appears until the image loads, or if it fails.

The centre retains up to twelve notifications. Opening it clears the arrival
popup; closing it reveals the prepared normal card without the legacy layout
flashing underneath. The popup's left accent bar shrinks as its display time
runs out. Notification titles and bodies use the shared color emoji downloader,
with monochrome fallback where available. The `→` text arrow is included in the
fonts; common ornamental variants such as `➜` and `➔` use that same glyph.

Configuration/build checks do not establish hardware visual or audio behavior.

## Message formatting

HTML line breaks (`<br>`, `<br/>`, `<br />`, including uppercase and escaped
forms) become real newlines. Paragraphs, divisions, and list items introduce line
breaks; inline markup is removed. Common HTML entities and numeric character
references are decoded. Literal newlines, accented text, and UTF-8 characters
remain intact within the message length limit.

## On the panel

A new message shows a banner at the top; tap it to open the message. Every
message is kept in the notification centre: pull down from the top edge on any
screen, or tap the bell (with its unread count) on the main view. The centre
lists recent messages with their age; open one for the full text and picture,
or dismiss it. See [panel screen behaviour](room-companion/panel-ui.md).

## Notification centre updates

On rectangular panels, a new notification with the same title and image URL
replaces the previous entry and moves to the top as unread. This keeps cumulative
chat updates in a single card. Different titles or image URLs stay separate;
notifications with an empty title also stay separate. Use a stable sender or
conversation title and avatar URL. The current `show_message` action does not
receive an app identifier: include the app in the title if the same sender can
arrive from several apps and should have a separate card for each.

## Phone and desktop notifications (KDE Connect)

Use [KDE for Home Assistant](https://github.com/Radeaus/kdeforhomeassistant) to
mirror notifications from your phone or computer onto the panel, including app
icons and automatic dismissal when you clear them on the originating device. For
**Linux** and **Windows** desktops, install the
[KDE Connect Rich Notifications](https://github.com/fixtse/kdeconnect-rich-notifications)
plugin to forward desktop notifications with chat avatars, website icons, and
screenshot previews on Linux, or notification text and application logos via its
experimental Windows port.

1. Install and pair your devices with
   [KDE for Home Assistant](https://github.com/Radeaus/kdeforhomeassistant), and
   choose which devices and apps share notifications on its sidebar page.
2. Create a **Text** helper (`input_text`, max length 255) in Home Assistant to
   track the active notification on the display.
3. Import the included blueprint:

   [![Import the blueprint](https://my.home-assistant.io/badges/blueprint_import.svg)](https://my.home-assistant.io/redirect/blueprint_import/?blueprint_url=https%3A%2F%2Fgithub.com%2Ffixtse%2FOpenNextion-Room-Companion%2Fblob%2Fmain%2Fblueprints%2Fautomation%2Fnotifications_on_esphome_display.yaml)

4. Create an automation from **KDE for Home Assistant · notifications on an
   ESPHome display**, selecting your KDE Connect device(s), the Text helper, and
   your Room Companion panel. Set **Icon server URL** if Home Assistant’s
   default URL is not directly reachable from the panel’s LAN.
