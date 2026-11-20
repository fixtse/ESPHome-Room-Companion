# Make the panel yours

[All guides](../README.md#find-your-guide) · [First-time setup](setup.md) · [Using the panel](controls.md)

Open **Visit** on the panel's Home Assistant device page, or open the web address
shown on the panel. Use a phone or computer on a network that can reach it.
Settings stay on the panel after you save them.

## Add and arrange controls

1. Open **Display & actions** and select a slot. A slot is a place for one control.
2. In **Display**, click **Choose entity**. Search by name or room and select the Home Assistant item you want.
3. Give it a shorter **Display name** if you like, then click **Save slot**.
4. Use **Add slot** for more controls, up to **20**. Move controls earlier/later or duplicate one into an empty slot.

Start with a few favorites. Empty slots do not need to be filled. The main screen
shows readings as you browse, and the bottom bar keeps the selected page in the
center. Tap to open a control.

**Save slot** saves the selected control. **Save all** saves all changed slots.
Unsaved changes are marked; **Discard changes** restores the last saved version.
If a save fails, already saved slots stay saved and the others remain marked.
The browser can offer to recover an unsaved draft after a reload—still save your
work before closing it.

The **On your panel** preview shows live readings when available. Switch between
**Main screen** and **Opened entity** to see each layout. A new entity needs to be
saved before its readings can load.

## Shortcut button

The panels have no physical button: tap a card to open its control screen, then
use that screen's own buttons, sliders and lists. Open a slot's **Shortcut** tab
to add one extra button at the bottom of that screen.

- **Choose script or automation** adds a button that runs it.
- **Remove shortcut** removes the button.

Controls whose screen already has its own action (fan, number, select, scene,
button) do not take a shortcut. Media players run the shortcut from the player's
own press when idle. Automation conditions still apply.

Enable **Show the returned value after the shortcut runs** to display a script's
returned `value` and optional `unit` as a brief popup. For automations, send the
value back using the panel's feedback action.
[Response examples](gesture-controls.md#press-action-results).

## Swipes

The **Swipes** tab sets what a swipe up or down does on the control's screen
(outside its buttons). Leave it empty for the normal adjustment shown in the
preview, or enter `none` to ignore the swipe. There are no hold gestures.

[Everyday press controls](controls.md) · [Custom climate and swipe actions](gesture-controls.md)

## Custom swipe actions

In **Gestures**, choose separate actions for **Swipe up** and **Swipe
down**. These are ordinary swipes. **Hold + swipe** is a separate,
optional pair of actions. Assignments apply only while that control is opened;
browsing the main view still works normally.

For example, show a computer-volume sensor and make swiping run the Home
Assistant script that changes your computer's volume. Choose an optional
**Feedback entity** to show the resulting value, and enable the brief popup if
you want a quick confirmation. Leave a gesture at **Default** to keep its usual
behavior. Advanced script responses and event routing are optional.

[Set up gestures and feedback, with desktop examples](gesture-controls.md)

## Playlists and stations

1. Choose a media player slot and open **Playlists**.
2. Click **Add playlist or station** and choose the script or automation that starts it.
3. Edit the friendly name if needed, arrange the entries and **Save slot**.

You can add up to **eight entries per player**. Set the intended player and music
source in the Home Assistant action itself; adding a row does not create a music
playlist in Home Assistant.

On the panel, open the player, hold until **Release now / Open playlists** appears,
then release. Swipe through the list and tap your choice. The
panel sends the action and **returns to the player automatically**. A checkmark
means the request was sent; the player may take a moment to start.

[Media controls and holding gestures](controls.md#music-and-playlists)

## Sensors and graphs

Choose a sensor slot and open **Display**. Keep **Value** for a simple reading,
or choose **12-hour graph when opened** for a numeric sensor. The main screen
still shows its current value. Graphs use Home Assistant's recorded statistics
where available; otherwise they show readings collected since the panel started.
Some sensors do not have recorded statistics. Gaps stay empty, and locally
collected history is lost on restart.

Add a **Secondary entity** for a second piece of information beneath the main
reading. **Secondary text** can turn that reading into a useful sentence:

| Write this | What it shows |
| --- | --- |
| `{value} {unit}` | The secondary reading and its unit |
| `Resets {relative}` | A relative time for a date/time entity |
| `Resets {relative} ({time})` | Relative time plus the clock time |
| `Next: {date}` | A date using your chosen format |
| `{datetime}` | Both the date and time |

For example, show remaining allowance as the main sensor and its reset time as
the secondary entity. Secondary text replaces the usual name below the reading.
Missing data is shown explicitly rather than as a made-up reading.

## Timers without clutter

Use **Add timer** under **Timers** in the page editor to register a Home
Assistant `timer.*` or a supported timer sensor without giving it a page. Its
automatic popup and the timer screen still show it. A registered timer can
also be placed on pages. Each timer counts once toward the **40 entities**.

The same entity can appear on several pages and in the timer list. It keeps a
single entry (one shortcut, gesture and quick-action setup) and one Home
Assistant subscription. Only one page can use it as its leading (large/top)
entity.

Active timers, including the ones Assist sets on the panel itself, appear as a
countdown chip beside the bell on the main screen. Tap it to open the timer
screen, even for timers that have no page.

The panel supports Home Assistant `timer.*`, compatible Voice PE/SoundSense voice
timer sensors and Google Home integration timer sensors. Ordinary text sensors
are not automatically treated as timers.

Ringing timers take priority, followed by the nearest running timer and paused
timers. Active timers appear while browsing, or after ten idle seconds on an
opened control. Swipe to return to browsing; an active timer can return after ten
idle seconds. Timer control remains in Home Assistant or the originating speaker.
Settings, scheduled sleep and a deliberately switched-off screen are not interrupted.

## Display and idle screen

Open **Device settings**, make your choices, then **Save device settings**.

| Setting | Choose what suits you |
| --- | --- |
| Screen brightness | 10–100%. Saving brightness turns the screen on. |
| Swipe sensitivity | Fast, Balanced or Fine. |
| Standby after | How long to wait before the automatic idle view, from 1–900 seconds. |
| Automatic idle screen | Allow it from the main screen only, from any screen, or never automatically. |
| Idle screen | **Clock** is the default. **Playing media** shows an actively playing configured player from the main carousel, with the clock when stopped. |
| Standby weather | Choose the weather entity used by the standby display. |
| Date and clock | Choose day/month order, named months, 12/24-hour time and optional AM/PM. |
| Accent color | Pick a preset or your own color for progress bars and timers, the selected navigator item, filled buttons and graphs. |

The media idle view uses a player already configured on the panel. Timers and
scheduled sleep keep priority. Tap or swipe to leave the idle view before using
the underlying control.

### Scheduled sleep

Enable the sleep schedule, choose its start/end times, and set how long to wait
before switching off the screen during those hours. The schedule uses Home
Assistant's clock. Matching start/end times mean all day. This is separate from
the idle clock/media view.

## Reminders and calendar messages

Home Assistant automations can show a message over the current view, such as
“Take the washing out” or “Meeting in 10 minutes.” No display slot is needed.
Tap to dismiss it, or swipe to scroll longer text. A normal reminder lasts
60 seconds; an automation can choose a different duration or keep it until dismissed.

Timer popups take priority. Messages wait during setup, scheduled sleep or when
the screen is deliberately off. Only the latest message is kept.

[Create a reminder or calendar popup](display-notifications.md)

## Presets

A preset is a named set of controls: every slot's entity, display name, icon,
tap and swipe actions, playlists and sensor choices. Keep one for each room,
time of day or person, and switch between them without restarting the panel.

Presets live in the **Preset** card above the slot list in **Display & actions**.
It starts collapsed and shows what you are editing: **On the panel** (marked
**Live**) or a preset's name. Click it to expand; the panel page remembers your
choice in this browser.

- **Save as preset** (while editing the panel) stores the panel's saved controls
  under a name. Unsaved slot changes are not included.
- **+** starts a new, empty preset. Add controls, then **Save all** to create it.
- Choose a preset from the list to edit its slots. While a preset is selected,
  **Save slot** and **Save all** store the preset only; the panel does not change.
- **▶ Apply** replaces the panel's controls with the selected preset. Only the
  slots that differ are rewritten, and controls you no longer use are released
  from memory. Save the preset's changes before applying it.
- The pencil and bin buttons rename and delete the selected preset.

Up to **8** presets fit on the panel. **Active** marks the preset the panel's
controls currently match. Editing a slot on the panel by hand afterwards marks the
controls as **Custom** until you apply or save a preset again.

### Switch presets from Home Assistant

The panel adds a **Preset** select to its Home Assistant device. Choose an option
to switch, or use it in automations and dashboards:

```yaml
action: select.select_option
target:
  entity_id: select.onx3248g035_dbbc54_preset
data:
  option: Evening
```

The panel also provides three actions. Replace the device prefix with yours:

```yaml
action: esphome.onx3248g035_dbbc54_apply_preset
data:
  name: Evening
```

`save_preset` saves the current controls under `name` (replacing a preset with
the same name) and `delete_preset` removes one. The select shows **Custom** when
the controls match no saved preset. When the list of presets changes, Home
Assistant reconnects to the panel briefly so the select shows the new options.

## Home Assistant connection

The **Home Assistant** section lets your browser sign in to search entities and
sync their icons. This is separate from adding the panel's ESPHome integration.
Enable **Allow the device to perform Home Assistant actions** in that integration
so the panel can control devices.

The status strip shows three different connections: reaching the panel, the
panel's connection to Home Assistant, and browser sign-in. Use **Refresh entity
list** after changing an icon or name in Home Assistant. **Retry panel entity
states** asks for fresh readings without restarting or replaying old commands.

[Connection troubleshooting](setup.md#if-something-does-not-work) · [Icons and album art](icons-and-media.md)

## Backups and diagnostics

In **Backup & diagnostics**, click **Download saved configuration** before making
large changes. Backups include saved controls, names, tap/swipe actions,
playlists, sensor display choices, saved presets and device settings. They
exclude credentials.

**Import configuration** opens a backup as an unsaved draft. Review it, then save
slots and device settings separately to apply it. Importing alone does not change
the panel. Older supported backups receive defaults for newer settings.

When a backup contains presets, the panel offers to restore them right away.
Presets with the same name are replaced; other presets on the panel are kept.
Restoring presets does not change the current controls.

**Download diagnostics** provides firmware, uptime and connection information
for troubleshooting, without entity names, addresses or credentials. After a
crash, its `last_event_before_boot` entry is named `panic` and lists the crash
address and backtrace. When available, `panic_message` includes the abort detail,
such as the task name in a FreeRTOS stack overflow. The panel also logs the
retained panic at startup. Wireless updates are available through the
**Firmware Update** entity in Home Assistant.

After a panic, `last_event_before_boot.http_request` identifies the last
Room Companion web request the handler saw, whether it returned, and the
HTTP task's minimum free stack measured at entry and exit. These measurements
are task-lifetime low-water marks. The route comes from a fixed list, so the
record excludes query parameters and request bodies.

## Display orientation

The panels have no accelerometer, so choose **Device settings → Display
orientation** — *Portrait*, *Landscape*, *Portrait · upside down* or
*Landscape · flipped* — and save. The panel restarts to apply it: the display
controller's MADCTL address order and the touch mapping are set before the
display, touchscreen and LVGL start, so landscape runs as a native 480×320
(3.5") or 320×240 (2.8") screen without software framebuffer rotation. The
setting persists and is included in configuration backups, and the webpage
preview follows it. See [panel-ui.md](room-companion/panel-ui.md#orientation).

## Clock seconds and connected sleep

Device settings are grouped into Screen & controls, Clock & idle screen,
Media & accent appearance, Notifications, and Sleep & wake.

Under **Clock & idle screen**, enable **Show seconds around the clock edge** to
show a seconds bar that fills once each minute. It is off by default and needs a valid
Home Assistant time. The shared **Accent color** also applies to clock seconds,
notification expiry, numeric values and media playback.

The Home Assistant **Low Power Sleep** switch keeps the device reachable while
sleeping the display panel, switching off the backlight, pausing LVGL rendering
and enabling Wi-Fi modem power saving. Turn the switch off or tap the screen to
wake. The wake tap is consumed and does not trigger an entity action. Turning
the display light on also leaves this mode. Rebooting starts awake.

This is connected low-power sleep, not ESP32 deep sleep: Home Assistant can wake
it because Wi-Fi remains active. Actual power savings depend on the hardware and
network and have not been measured. Your existing sleep schedule still controls
screen-only sleep; automate the Low Power Sleep switch in Home Assistant for a
night-time power-saving routine.
