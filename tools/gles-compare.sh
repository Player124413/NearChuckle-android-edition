#!/bin/sh
# Render the same frame of a map with the OpenGL and GLES renderers and diff the captures.
#   tools/gles-compare.sh <game dir> <map> [frame] [out dir]
# Runs from <game dir>/macos (the per-file symlink layout of BUILDING-macOS.md). The map loads
# behind the menu; FARCRY_RESUME_FRAME=100 dismisses it and gameplay is on screen from about
# frame 1000 on the Pier map. Needs python3 with Pillow for the diff.
set -e
GAME=${1:?game dir}; MAP=${2:?map name}; FRAME=${3:-1300}; OUT=${4:-compare}
QUIT=$((FRAME + 30))
TOOLS=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
cd "$GAME/macos"
for R in OpenGL GLES; do
  echo "== $R: $MAP frame $FRAME"
  FARCRY_RESUME_FRAME=100 FARCRY_QUIT_FRAME=$QUIT FARCRY_DEPTH_PROBE=1 FARCRY_FPS_EVERY=300 \
  FARCRY_SCREENSHOT_FRAME=$FRAME FARCRY_SCREENSHOT_FILE="$OUT/${MAP}_$R.jpg" \
    ./FarCry -DEVMODE -RENDERER:$R "\"map $MAP\"" > "$OUT/${MAP}_$R.log" 2>&1 || true
  grep -h "^FPS\|^DEPTH" "$OUT/${MAP}_$R.log" | tail -2
  grep -h "unimplemented entry points" -A 40 "$OUT/${MAP}_$R.log" | grep "GLES:   " || true
done
python3 "$TOOLS/imgdiff.py" "$OUT/${MAP}_GLES.jpg" "$OUT/${MAP}_OpenGL.jpg" "$OUT/${MAP}_diff.jpg"
