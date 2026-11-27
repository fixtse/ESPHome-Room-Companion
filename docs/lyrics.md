# Synced lyrics (optional)

The media screen can show synced lyrics in place of the album art, over a soft,
blurred copy of the cover: the previous line,
the current line, and the next one. It works with Spotify and any other Home
Assistant media player that reports the title, artist, duration and position.
It's off until you install the blueprint below.

**How it works:** when a new track starts, a Home Assistant blueprint asks
[LRCLIB](https://lrclib.net) for its timed lyrics. LRCLIB is free and needs no
account. Home Assistant sends the lyrics to the panel once per track. The panel
keeps the current line in time on its own, from the track position it already
shows, so nothing else is sent while the song plays. Tracks without timed lyrics
show the artwork as usual.

Tap the lyrics to show the artwork again. They return on the next track.

**Easier:** the [Room Companion integration](https://github.com/fixtse/ha-room-companion)
(HACS) does the same without the blueprint or `rest_command`: add the panel,
choose its media players under **Configure**, and leave **Send synced lyrics**
on. Use either the integration or the blueprint below, not both.

The integration also:

- **Fills in lyrics mid-song.** When you open a player on the panel that has
  no lyrics yet (it started before Home Assistant did, or it is not one of the
  chosen players), the panel asks for them and they appear within a second or
  two.
- **Adds a Lyrics offset** setting to the panel's device page (seconds, -10 to
  10). Positive values show each line earlier, negative later. The change
  applies on the panel at once, without reloading the lyrics.
- **Makes the blurred background in Home Assistant**, so the panel only
  downloads it. Without the integration the panel blurs the cover itself.

## Setup with the blueprint

1. Add this once to `configuration.yaml`, then restart Home Assistant:

   ```yaml
   rest_command:
     room_companion_lrclib:
       url: "https://lrclib.net/api/{{ path }}"
       method: GET
       headers:
         User-Agent: "Room Companion lyrics (https://github.com/fixtse/ESPHome-Room-Companion)"
       timeout: 15
   ```

2. Import the blueprint:

   [![Import the blueprint](https://my.home-assistant.io/badges/blueprint_import.svg)](https://my.home-assistant.io/redirect/blueprint_import/?blueprint_url=https%3A%2F%2Fgithub.com%2Ffixtse%2FESPHome-Room-Companion%2Fblob%2Fmain%2Fblueprints%2Fautomation%2Froom_companion_lyrics.yaml)

3. Create an automation from **Room Companion synced lyrics**:
   - **Media player:** your Spotify player, for example `media_player.spotify_you`.
   - **Room Companion panels:** pick one or more panels from the ESPHome
     device list.

   Have more than one player? Create one automation for each.

## Notes

- If the panel restarts in the middle of a song, its lyrics come back on the
  next track or the next Home Assistant restart.
- Lyrics are matched by title, artist, album and duration. LRCLIB doesn't have
  every song.
- The panel keeps at most 400 lines (12 KB) per track, in PSRAM. Tap the lyrics
  to bring back the artwork and the quick actions button.
- To send lyrics from another source, call the action yourself with `entity_id`,
  `title` (must equal the player's `media_title`) and `lrc` (LRC text with
  `[mm:ss.xx]` tags). An empty `lrc` clears the lyrics.
