#!/usr/bin/env bash
# Run on devserv under the caller's lease.
set -euo pipefail
cd "$(dirname "$0")/.."
output_dir="$(mktemp -d /tmp/rc-panel-tests.XXXXXX)"
trap 'rm -rf "$output_dir"' EXIT
c++ -std=c++17 tests/test_panel_layout.cpp -o "$output_dir/layout"
"$output_dir/layout"
c++ -std=c++17 -Itests/touch_stubs tests/test_touch_input.cpp -o "$output_dir/touch"
"$output_dir/touch"
mkdir -p tests/artifacts
docker run --rm --name "rc-panel-tests-$$" -v "$PWD":/config -w /config \
  --entrypoint bash mcr.microsoft.com/playwright:v1.56.1-noble -lc \
  'npm install --prefix /tmp/rc-panel-web --no-audit --no-fund playwright@1.56.1 && NODE_PATH=/tmp/rc-panel-web/node_modules node tests/test_panel_pages.cjs && NODE_PATH=/tmp/rc-panel-web/node_modules node tests/test_panel_startup.cjs'
