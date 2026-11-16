# Room Companion

Read `/home/fixt/AGENTS.md`. Builds, installs, tests and services run on devserv
under a task lease and isolated checkout. Do not flash a board unless requested.

The software foundation is the existing fixtSE knob project. Preserve its history,
AGPL license, copyright and original entry point. Prefer extracting/reusing its
state/configuration logic over reimplementing it. New rectangular firmware lives
in `packages/room_companion/`; both model roots use one shared application.

Pin ESPHome to 2026.10.2. Validate and compile both model roots after shared
changes. If wake-word/extension examples change, also compile the example matrix
using `scripts/validate-room-companion.sh --compile` in the pinned container.
Never commit secrets, keys, `.env`, firmware blobs or private HA state. Keep
hardware-tested status separate from configuration/build validation.
