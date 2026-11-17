#!/usr/bin/env bash
# Run on devserv, in the pinned ESPHome container/devcontainer.
# Validates (and with --compile, builds) both Room Companion panels,
# including the wake-word root and the extension example.
set -euo pipefail
cd "$(dirname "$0")/.."
created_secrets=0
cleanup() {
  if [[ "$created_secrets" = 1 ]]; then rm -f secrets.yaml; fi
  rm -f .validation-extension.yaml
}
trap cleanup EXIT
if [[ ! -e secrets.yaml ]]; then
  created_secrets=1
  python3 - <<'PY'
import base64, os
from pathlib import Path
text = Path('secrets.example.yaml').read_text()
text = text.replace('REPLACE_WITH_GENERATED_BASE64_KEY', base64.b64encode(os.urandom(32)).decode())
Path('secrets.yaml').write_text(text)
os.chmod('secrets.yaml', 0o600)
PY
fi
cat > .validation-extension.yaml <<'YAML'
packages:
  device: !include ONX2432G028.yaml
  extension: !include examples/room-companion/extension.yaml
YAML
for config in ONX2432G028.yaml ONX3248G035.yaml ONX3248G035.wake-word.yaml .validation-extension.yaml; do
  echo "== $config"
  esphome config "$config" > /tmp/room-companion-config.log
  if [[ "${1:-}" = --compile ]]; then esphome compile "$config"; fi
done
