#!/usr/bin/env bash
# Run on devserv, in the pinned ESPHome container/devcontainer.
set -euo pipefail
cd "$(dirname "$0")/.."
created_secrets=0
cleanup() {
  if [[ "$created_secrets" = 1 ]]; then rm -f secrets.yaml; fi
  rm -f .validation-wake-word.yaml
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
for model in ONX2432G028 ONX3248G035; do
  esphome config "$model.yaml" > /tmp/room-companion-config.log
  if [[ "${1:-}" = --compile ]]; then esphome compile "$model.yaml"; fi
done
cat > .validation-wake-word.yaml <<'YAML'
packages:
  device: !include ONX2432G028.yaml
  wake_word: !include examples/room-companion/wake-word.yaml
  extension: !include examples/room-companion/extension.yaml
YAML
esphome config .validation-wake-word.yaml > /tmp/room-companion-wake-word-config.log
if [[ "${1:-}" = --compile ]]; then esphome compile .validation-wake-word.yaml; fi
