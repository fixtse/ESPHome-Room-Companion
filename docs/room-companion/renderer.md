# Optional rendered cards

The native panel works without this service. Today/Refresh requests a JPEG from
`/panel/ONX2432G028.jpg` (208×144) or `/panel/ONX3248G035.jpg` (288×288).
The small card shows fewer HA states. The server keeps HA credentials and sends
only rendered pixels. The initial card service is a Pillow state-card renderer,
not a browser or interactive dashboard stream.

On the service host, put these in ignored `renderer/.env`:

```dotenv
RENDERER_TOKEN=REPLACE_WITH_A_RANDOM_TOKEN_AT_LEAST_24_CHARACTERS
HA_URL=http://homeassistant.local:8123
HA_TOKEN=REPLACE_WITH_A_HOME_ASSISTANT_TOKEN
HA_ENTITIES=sensor.living_room_temperature,sensor.power,weather.home
RENDERER_BIND_ADDRESS=127.0.0.1
```

Run `docker compose -f renderer/compose.yaml --env-file renderer/.env up -d --build`
on devserv/the intended host. The default listener is loopback. Make it reachable
through your existing TLS reverse proxy, or deliberately bind its intended LAN
interface. Then set `renderer_base_url` in the panel substitutions and
`renderer_authorization: "Bearer <same RENDERER_TOKEN>"` in panel secrets.
Use a dedicated HA account/token with access appropriate to displayed data.
Tokens travel in an Authorization header, never in image URLs. Do not commit
`.env`, panel secrets or captured private images.

Refresh is manual, deferred while Assist is running. An image is marked old after
90 seconds; a failed request displays an unavailable status. An HA fetch failure
returns HTTP 503 instead of falsely reporting fresh data. No renderer is deployed
by project creation or validation. The renderer unit tests bind an ephemeral
loopback port and stop it before returning.
