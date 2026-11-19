FROM strangev/remote-webview-server:1.3.0

# Patch the pinned server's two constants. Fail if the upstream code differs.
RUN node -e '\
  const fs = require("node:fs"); \
  const file = "/app/dist/deviceManager.js"; \
  let source = fs.readFileSync(file, "utf8"); \
  for (const [before, after] of [ \
    ["const ACK_WINDOW_FRAMES = 2;", "const ACK_WINDOW_FRAMES = 1;"], \
    ["const ACK_TIMEOUT_MS = 1000;", "const ACK_TIMEOUT_MS = 3000;"], \
  ]) { \
    if (source.split(before).length !== 2) throw new Error("Unexpected upstream ACK setting: " + before); \
    source = source.replace(before, after); \
  } \
  fs.writeFileSync(file, source);'
