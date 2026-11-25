# Make swiping do what you want

[All guides](../README.md#find-your-guide) · [Web configuration](web-configuration.md#custom-swipe-actions)

Make swipes inside a control run your own Home Assistant routines. For example,
swipe up to raise your computer's volume, swipe down to lower it, and show the
new volume on screen. Your computer needs to be controllable from Home Assistant
already; this feature does not install a computer connection for you.

## Choose your actions

1. Open the panel webpage, select a slot and choose the entity you want on screen.
2. Open **Gestures** and use **Choose action** for **Swipe up** and **Swipe down**. Pick your existing Home Assistant script or automation.
3. Leave **Hold + swipe** at Default unless you want separate actions for swipes that start with a held touch.
4. Set **Movement step** to how much each swipe step should change the value. Start with 1.
5. Click **Save slot**, then open that control on the panel and swipe up or down.

Ordinary swipes do **not** need a held touch. The optional held swipes are
separate. Actions run only inside the opened control, so you can still swipe to
browse the main view. Swipe left or right on an opened control to go back.

Choose **Default** to keep a gesture's usual behavior. To ignore a particular
gesture, type `none` in its action field. For a climate control's short press,
use the separate **Press action** tab; Default keeps the usual mode control.

## Show the result

Choose a **Feedback entity**, such as a sensor reporting computer volume. Leave
it empty to use the displayed entity. Enable **Show a brief popup** to see the
new value briefly after swiping, without interrupting further input.

If your script returns a value directly, you can enable **Read script responses**
instead. Scripts must be prepared to return the value as shown below. You can
leave responses and events off for a simple setup. A brief feedback popup does
not change the Home Assistant sensor itself.

If nothing happens, check the chosen action in Home Assistant and enable the
panel's [permission to perform actions](setup.md#add-it-to-home-assistant).

## Advanced automation and script examples

The remaining sections are for preparing Home Assistant scripts or asking someone
to connect an existing desktop integration. Replace example names with your own.

In the panel's web configuration, choose an entity and open **Gestures**. You can
assign a script or automation independently to:

- Swipe up (or left).
- Swipe down (or right).
- Touch and hold, then swipe up.
- Touch and hold, then swipe down.

An empty binding preserves that gesture's native behavior. `none` consumes the
gesture without performing an action. The left-edge swipe still exits the
detail view. Gestures run only while the entity is opened, with Home Assistant
connected; they do not replace carousel navigation, playlist browsing or reminder
interaction. Existing configurations keep their native controls.

Set **Movement step** to the desired increment, from 1 to 100. The panel's normal
sensitivity setting still applies. Consecutive swipe steps in the same direction are
combined and dispatched at up to ten action calls per second. Use the provided
signed `delta`, rather than assuming every call represents one step. The
outgoing queue retains up to eight successive gesture groups and is cleared when
the view closes or becomes unavailable.

Home Assistant's ESPHome integration must allow the device to perform Home
Assistant actions. The desktop command itself belongs in your Home Assistant
script, automation, shell command or desktop integration; the panel does not
connect directly to the computer.

## Values passed to the action

The same script can be assigned to both swipe directions.

| Variable | Meaning |
| --- | --- |
| `delta` | Signed movement total, including the configured step. Up is positive. |
| `direction` | `up`, `down`, `held_up` or `held_down`. |
| `pressed` | Whether the touch was held before swiping. |
| `panel_entity` | Display entity whose detail view received the gesture. |
| `value` | Most recently received feedback state, or the displayed entity's state. |
| `unit` | Unit associated with that value, when available. |

Use `delta | int` and `pressed | bool` in templates to normalize values from the
ESPHome API. `value` is context, not a guaranteed fresh desktop reading. Read the
real desktop state in the receiving script when needed. Automations receive
these variables through `automation.trigger`; their conditions are respected.

## Desktop volume example

1. Choose `sensor.desktop_volume` as the displayed entity. It should report the
   real computer volume through your existing Home Assistant setup.
2. Assign `script.desktop_volume_panel` to swipe up and swipe down.
3. Choose a step such as 2.
4. Leave **Feedback entity** empty to use `sensor.desktop_volume`, or select a
   different feedback entity.
5. Enable **Show a brief popup** for a non-blocking value popup after feedback
   changes following a swipe.

The primary sensor remains subscribed and updates the screen normally. A
separate feedback entity supplies the action's `value`/`unit` and the popup; for a
sensor detail view, its latest received value is also shown when configured.
The first subscription value does not produce a popup. Feedback updates within
five seconds of a gesture can show a popup lasting 1.5 seconds.

For direct script responses, enable **Read script responses**. The response must
be a mapping with `value` and optionally `unit`. Only the newest in-flight
request's response is displayed. A missing unit retains the current feedback
unit. Responses update the display feedback, not Home Assistant's sensor state.

This example assumes an existing `shell_command.desktop_volume` that accepts a
signed `delta` and prints the resulting volume to stdout. Replace it with your
actual desktop-control action and adapt the returned value:

```yaml
script:
  desktop_volume_panel:
    alias: Desktop volume from panel
    mode: queued
    max: 10
    fields:
      delta:
        description: Signed movement amount
      direction:
        description: Panel direction
      pressed:
        description: Whether the panel was held
      panel_entity:
        description: Originating display entity
      value:
        description: Last known feedback value
      unit:
        description: Last known feedback unit
    sequence:
      - action: shell_command.desktop_volume
        data:
          delta: "{{ delta | int }}"
        response_variable: desktop_result
      - variables:
          feedback:
            value: "{{ desktop_result.stdout | trim }}"
            unit: "%"
      - stop: Desktop command completed
        response_variable: feedback
```

Leave response capture off for scripts that do not return data. Automations can
update the feedback sensor, or explicitly call the panel's feedback action:

```yaml
action: esphome.onx3248g035_dbbc54_show_control_feedback
data:
  entity_id: "{{ panel_entity }}"
  value: "{{ states('sensor.desktop_volume') }}"
  unit: "%"
```

Replace the device prefix if your panel has a different name. This brief popup
never consumes swipes. Disable the popup checkbox to keep only the live
value. [Full reminder popups](display-notifications.md) remain available for
messages that should wait for dismissal.

## Route movements through events

Leave the gesture action bindings empty and enable **Publish swipe events to Home
Assistant** to use an event-triggered automation. Enabling this option consumes
all four swipe gestures in the opened entity view. Events are named
`esphome.panel_gesture` and contain `device`, `entity_id`, `direction`, `delta`,
`pressed`, `value` and `unit`.

```yaml
alias: Desktop control from panel events
triggers:
  - trigger: event
    event_type: esphome.panel_gesture
    event_data:
      device: onx3248g035-dbbc54
      entity_id: sensor.desktop_volume
actions:
  - action: script.desktop_volume_panel
    data:
      delta: "{{ trigger.event.data.delta | int }}"
      direction: "{{ trigger.event.data.direction }}"
      pressed: "{{ trigger.event.data.pressed | bool }}"
      panel_entity: "{{ trigger.event.data.entity_id }}"
      value: "{{ trigger.event.data.value }}"
      unit: "{{ trigger.event.data.unit }}"
mode: queued
```

Direct bindings and events can also be enabled together. Avoid routing the same
movement to the same desktop command through both paths.

## Climate short press

Climate entities now accept a script or automation under **Press action**, as
well as **No action**. **Default** retains native HVAC-mode cycling. Native
rotation and held setting selection remain in place unless you assign those
gestures separately. Short-press actions retain the existing no-argument press
behavior; movement variables are supplied by swipe actions.

References: [ESPHome Home Assistant actions and responses](https://esphome.io/components/api/#homeassistantaction-action),
[Home Assistant scripts and returned data](https://www.home-assistant.io/docs/script/),
and [automation trigger implementation](https://github.com/home-assistant/core/blob/dev/homeassistant/components/automation/services.py).

## Press action results

In **Press action**, enable **Show the returned value after pressing** for a
script or automation. This setting is independent of the gesture popup checkbox
and is off by default. Existing gesture bindings are preserved on upgrade.

A script must be called directly to return data. With this option enabled, the
panel calls your selected script directly, without adding arguments, and reads
`value` and optional `unit` from its returned mapping. For example:

```yaml
alias: Check room temperature
sequence:
  - variables:
      result:
        value: "{{ states('sensor.madrid_piso_temperature') }}"
        unit: "°C"
  - stop: Temperature read
    response_variable: result
```

Automations do not return a value through `automation.trigger`. With the option
enabled, the panel supplies `panel_entity` as an automation variable. Send the
result back from the automation using:

```yaml
action: esphome.onx3248g035_dbbc54_show_control_feedback
data:
  entity_id: "{{ panel_entity }}"
  value: "{{ states('sensor.madrid_piso_temperature') }}"
  unit: "°C"
```

Replace the device action prefix with yours. Results appear as the same brief,
non-blocking popup used for gesture feedback. Playback controls retain their
normal behavior; a media player's custom press action runs only when idle.
