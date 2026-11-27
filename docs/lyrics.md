# Synced lyrics (optional)

The media screen shows the previous, current, and next lyrics lines over a
blurred copy of the album cover. It works with Spotify and other Home Assistant
media players that report title, artist, duration, and position.

1. [Set up the Room Companion integration](setup.md#set-up-the-room-companion-integration).
2. Open **Configure** on the Room Companion entry for your panel, choose its
   **Media players**, and leave **Send synced lyrics** enabled.

The integration fetches timed lyrics from [LRCLIB](https://lrclib.net) when a
track starts. The panel uses the playback position to keep the lines in sync.
Tracks without timed lyrics show the artwork. Tap the lyrics to return to the
artwork and quick actions. Lyrics return on the next track.

With firmware **2026.11.1.25** or later, opening a player mid-song can request
missing lyrics. Home Assistant serves the blurred backdrop, and the panel's
**Lyrics offset** entity applies changes live from −10 to +10 seconds.
Positive values show each line earlier.

## Notes

- Lyrics are matched by title, artist, album and duration. LRCLIB doesn't have
  every song.
- The panel keeps at most 400 lines (12 KB) per track, in PSRAM. Tap the lyrics
  to bring back the artwork and the quick actions button.
- To send lyrics from another source, call the action yourself with `entity_id`,
  `title` (must equal the player's `media_title`) and `lrc` (LRC text with
  `[mm:ss.xx]` tags). An empty `lrc` clears the lyrics.
