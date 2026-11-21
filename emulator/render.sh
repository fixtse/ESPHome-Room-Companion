#!/usr/bin/env bash
# Screen validation: renders every page and detail screen of a scenario on the
# 2.8" and 3.5" panels with the firmware's own UI code, draws the web
# configuration preview of each screen, and writes side-by-side sheets.
# Run on devserv under the task lease, from any checkout:
#
#   emulator/render.sh [--scenario FILE] [--model 28|35] [--only TEXT] [--no-compare] [--landscape]
#   (--landscape adds 28L-/35L- renders of the quarter-turned panel)
#
# Output (emulator/out/):
#   28-NN-<screen>.png, 35-NN-<screen>.png   device renders
#   web/*.png                                web previews of the same screens
#   compare/*.png, compare/28-all.png, ...   emulator | web preview sheets
#
# LVGL_EMU_DIR=/path/to/esphome-lvgl-emulator builds against a local checkout
# instead of github.com/fixtse/esphome-lvgl-emulator.
set -euo pipefail
cd "$(dirname "$0")/.."
scenario=emulator/scenarios/all-entities.json
models=(28 35)
only=()
compare=1
landscape=0
while [ $# -gt 0 ]; do
  case "$1" in
    --scenario) scenario="$2"; shift 2 ;;
    --model) models=("$2"); shift 2 ;;
    --only) only=(--only "$2"); shift 2 ;;
    --no-compare) compare=0; shift ;;
    --landscape) landscape=1; shift ;;
    *) sed -n '2,20p' "$0"; exit 2 ;;
  esac
done
image=esphome-lvgl-emulator
mounts=(-v "$PWD":/src)
cmake_args=()
if [ -n "${LVGL_EMU_DIR:-}" ]; then
  docker build -q -t "$image" "$LVGL_EMU_DIR" >/dev/null
  mounts+=(-v "$(cd "$LVGL_EMU_DIR" && pwd)":/lvgl-emu:ro)
  cmake_args+=(-DFETCHCONTENT_SOURCE_DIR_ESPHOME_LVGL_EMULATOR=/lvgl-emu)
else
  docker build -q -t "$image" https://github.com/fixtse/esphome-lvgl-emulator.git#main >/dev/null
fi
run() { docker run --rm --name "rc-emulator-$$-$RANDOM" -u "$(id -u):$(id -g)" -e HOME=/tmp "${mounts[@]}" -w /src "$image" "$@"; }
# Always recompile the panel code: synced files keep the source machine's
# timestamps, which can look older than the last build. LVGL stays cached.
[ -d emulator/build/CMakeFiles/rc-emulator.dir ] && find emulator/build/CMakeFiles/rc-emulator.dir -name "*.o" -delete
rm -f emulator/build/rc-emulator
run sh -c "cmake -S emulator -B emulator/build -DCMAKE_BUILD_TYPE=Release ${cmake_args[*]} >/dev/null &&
  cmake --build emulator/build -j\$(nproc) > emulator/build/build.log 2>&1" ||
  { grep -E 'error' emulator/build/build.log | head -20; echo "emulator build failed (emulator/build/build.log)"; exit 1; }
rm -rf emulator/out
for model in "${models[@]}"; do
  run emulator/build/rc-emulator --model "$model" --scenario "$scenario" --out emulator/out --preview emulator/out/preview "${only[@]}"
  if [ "$landscape" = 1 ]; then run emulator/build/rc-emulator --model "$model" --landscape 1 --scenario "$scenario" --out emulator/out --preview emulator/out/preview "${only[@]}"; fi
done
if [ "$compare" = 1 ]; then
  docker run --rm --name "rc-emulator-compare-$$" -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD":/src -w /src \
    --entrypoint bash mcr.microsoft.com/playwright:v1.56.1-noble -lc \
    'npm install --silent --prefix /tmp/rc-compare --no-audit --no-fund playwright@1.56.1 @mdi/svg@7.4.47 >/dev/null &&
     MDI_SVG_DIR=/tmp/rc-compare/node_modules/@mdi/svg/svg NODE_PATH=/tmp/rc-compare/node_modules node emulator/preview-compare.cjs emulator/out'
fi
echo "screens: $(ls emulator/out/*.png | wc -l) in emulator/out"
