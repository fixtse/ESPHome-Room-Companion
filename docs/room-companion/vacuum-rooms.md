# Vacuum rooms and maps

A vacuum's screen on the panel can show its map and clean chosen rooms in the
order you tap them. Both are set up per vacuum on the configuration page, in
the vacuum's entry.

## Map

Under **Map**, choose the entity that has the vacuum's map picture:

| Integration | Map entity |
| --- | --- |
| Roborock (Home Assistant) | `image.<vacuum>_map` (an image entity) |
| Xiaomi Cloud Map Extractor (Xiaomi, Roborock, Dreame, Viomi, Roidmi) | `camera.xiaomi_cloud_map_extractor` (a camera) |
| Dreame Vacuum (custom) | `camera.<vacuum>_map` |
| Any other camera or image entity | that entity |

The map replaces the battery ring on the vacuum screen (battery, state and fan
speed stay in the header; tap the map to refresh it) and fills the vacuum's
card on a main page when at least 40 px fit above its buttons.

Pictures come through the [Room Companion integration](https://github.com/fixtse/ha-room-companion),
which scales them to the panel's frame: version 0.4.0 or newer for cameras,
0.4.4 or newer for image entities. Image entities are not streamed: Room
Companion reads the picture again only when the image entity reports a new
one, and the panel checks for a new picture every 10 seconds (3 seconds while
the vacuum screen is open).

## Rooms

Under **Room cleaning**, choose how the vacuum cleans rooms. With rooms set
up, the vacuum screen gets a **Rooms** row (next to **Speed** when the vacuum
has fan speeds). It opens the room list: tap rooms in the order to clean
them (each shows its place; tap a picked room to remove it), then **Clean N
rooms**.

### Home Assistant areas (most integrations)

Home Assistant's own `vacuum.clean_area` action cleans Home Assistant areas
mapped to the vacuum's rooms. Roborock, Ecovacs, Matter, MQTT (Valetudo),
Neato and template vacuums support it.

1. In Home Assistant, open the vacuum's settings and map its rooms (segments)
   to areas.
2. On the configuration page, choose **Home Assistant areas** and press **Load
   rooms from Home Assistant**. Each mapped area appears as `Name = area_id`.
   Remove lines for areas you do not want on the panel.
3. Save.

The panel then sends:

```yaml
action: vacuum.clean_area
target:
  entity_id: vacuum.robot
data:
  cleaning_area_id: [kitchen, hallway]   # in the order you tapped them
```

### A script (any integration)

For vacuums without `vacuum.clean_area` (Xiaomi Miio, Xiaomi MIoT, Dreame,
Ecovacs' older integrations, or anything that needs its own steps), the panel
calls a script of yours with the picked rooms:

| Script variable | Value |
| --- | --- |
| `<room field>` (default `rooms`) | The picked room names joined by the separator, in order, e.g. `Kitchen\|Hallway` |
| `room_ids` | The rooms' ids joined the same way (only when rooms have ids) |
| `vacuum_entity` | The vacuum, e.g. `vacuum.robot` |

On the configuration page choose **A script**, then:

- **Script**: the script entity, e.g. `script.clean_vacuum_room`.
- **Room field**: the variable your script reads the rooms from.
- **Separator**: what joins the rooms (default `|`).
- **Rooms**: one per line, `Name` or `Name = id`. The names are what your
  script receives, so use the names your script or vacuum knows. **Load rooms
  from Home Assistant** fills the list from a `rooms` attribute on the map
  entity or the vacuum when there is one (Xiaomi Cloud Map Extractor, Dreame).

A minimal script for segment-based integrations, using the ids:

```yaml
clean_rooms:
  alias: Clean rooms
  fields:
    rooms: {description: "Room names joined by |"}
    room_ids: {description: "Segment ids joined by |"}
    vacuum_entity: {description: "The vacuum"}
  sequence:
    # Xiaomi Miio:      xiaomi_miio.vacuum_clean_segment, segments
    # Dreame (custom):  dreame_vacuum.vacuum_clean_segment, segments
    - action: xiaomi_miio.vacuum_clean_segment
      target:
        entity_id: "{{ vacuum_entity }}"
      data:
        segments: "{{ room_ids.split('|') | map('int') | list }}"
```

#### Example: an ordered job by room name (Xiaomi MIoT)

A script can take room names and do everything else itself. This setup's
`script.clean_vacuum_room` takes `cleaning_room: "Dormitorio|Baño|Pasillo"`,
looks the names up in the robot's own room list, checks the robot is ready,
starts one ordered pass over those rooms (without docking between them) and
reports back. For the panel:

- **Script**: `script.clean_vacuum_room`
- **Room field**: `cleaning_room`
- **Separator**: `|`
- **Rooms**: the robot's room names, exactly as the script matches them:

  ```text
  Dormitorio
  Baño
  Pasillo
  Cocina
  ```

The script ignores `room_ids` and `vacuum_entity`; extra variables do no harm.

## Checks

- The panel needs Home Assistant actions allowed for the device (the same
  setting as its other controls).
- When nothing happens, check Home Assistant's log: `vacuum.clean_area` names
  areas that are not mapped, and a script logs its own errors. The panel logs
  each request (`vacuum` tag) with the rooms it sent.
- Up to 16 rooms per vacuum; names and ids up to 48 characters (UTF-8 bytes).
