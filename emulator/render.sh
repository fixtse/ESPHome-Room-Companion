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
#
# Compiles go through ccache and the comparison's npm packages are installed
# once, both in a cache shared by every checkout on the machine
# (RC_EMU_CACHE, default ~/.cache/rc-emulator), so a fresh checkout does not
# rebuild LVGL from scratch. The pinned LVGL source is cloned there once too. Safe for concurrent runs; delete it to reset.
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
# The same image plus ccache (a cached layer; rebuilt when the base changes).
printf 'FROM %s\nRUN apt-get update && apt-get install -y --no-install-recommends ccache && rm -rf /var/lib/apt/lists/*\n' "$image" |
  docker build -q -t "$image-ccache" - >/dev/null
cache="${RC_EMU_CACHE:-$HOME/.cache/rc-emulator}"
mkdir -p "$cache/ccache" "$cache/npm"
mounts+=(-v "$cache":/cache)
# Every checkout is mounted at /src, so ccache hits across checkouts.
cmake_args+=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
run() { docker run --rm --name "rc-emulator-$$-$RANDOM" -u "$(id -u):$(id -g)" -e HOME=/tmp \
  -e CCACHE_DIR=/cache/ccache -e CCACHE_BASEDIR=/src -e CCACHE_MAXSIZE=2G "${mounts[@]}" -w /src "$image-ccache" "$@"; }
# The LVGL source at the tag emulator/CMakeLists.txt pins, cloned once.
lvgl_tag=$(sed -n 's/.*LVGL_EMU_LVGL_VERSION "\(v[0-9.]*\)".*/\1/p' emulator/CMakeLists.txt)
if [ -n "$lvgl_tag" ]; then
  run sh -c "{ flock 9; [ -f /cache/lvgl-$lvgl_tag/.ready ] || { rm -rf /cache/lvgl-$lvgl_tag &&
    git clone -q --depth 1 -b $lvgl_tag https://github.com/lvgl/lvgl.git /cache/lvgl-$lvgl_tag && touch /cache/lvgl-$lvgl_tag/.ready; }; } 9>/cache/.lvgl.lock"
  cmake_args+=(-DFETCHCONTENT_SOURCE_DIR_LVGL=/cache/lvgl-$lvgl_tag)
fi
# Always recompile the panel code: synced files keep the source machine's
# timestamps, which can look older than the last build. ccache makes this cheap.
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
  docker run --rm --name "rc-emulator-compare-$$" -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD":/src -v "$cache/npm":/cache/npm -w /src \
    --entrypoint bash mcr.microsoft.com/playwright:v1.56.1-noble -lc \
    'm=/cache/npm/node_modules ok=/cache/npm/.ready-playwright-1.56.1-mdi-7.4.47
     { flock 9; [ -f $ok ] || { npm install --silent --prefix /cache/npm --no-audit --no-fund playwright@1.56.1 @mdi/svg@7.4.47 >/dev/null && touch $ok; }; } 9>/cache/npm/.lock &&
     MDI_SVG_DIR=$m/@mdi/svg/svg NODE_PATH=$m node emulator/preview-compare.cjs emulator/out'
fi
echo "screens: $(ls emulator/out/*.png | wc -l) in emulator/out"
