# remote_webview (vendored)

Source: <https://github.com/strange-v/RemoteWebViewClient>, `components/remote_webview`
at commit `1dae5a280b6649514bed2b537b8c264e1d7a67aa` (2026-10-28).
Copyright (c) 2025 strange_v. MIT License; see `LICENSE` in this directory.

Room Companion changes, so the stream can share the panel with the native LVGL UI:

- `server` and `url` are optional in YAML; they are normally set at runtime.
- `setup()` creates queues and mutexes only. `start()` creates the WebSocket and
  decode tasks, so a panel without a configured server never allocates them.
- `set_active(bool)` hands the display over. While inactive, frame packets are
  discarded without decoding, touch is not forwarded, and no full-frame repair is
  requested. Deactivation waits for an in-progress draw. Activation requests a
  full frame.
- `is_connected()`, `is_started()`, `has_server()` and `is_active()` accessors.
- Touch forwarding starts disabled.
- `set_url()` takes the state mutex, so it can be called while the stream runs.
- `set_offscreen(bool)` (camera view): frames are decoded only into the PSRAM
  frame copy and never drawn to the display; touch stays with the native UI.
  `take_offscreen_frame()` hands the newest complete frame to the main loop,
  and the decode task waits (bounded) until it is taken before reusing the
  other buffer.

`web_client/`, the examples and the images are not included.
