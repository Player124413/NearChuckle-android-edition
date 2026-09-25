#!/bin/sh
# Launch Far Cry on the GLES renderer, optionally straight into a map.
#   tools/run-gles.sh                 menu
#   tools/run-gles.sh pier            load the Pier level
#   tools/run-gles.sh pier -DEVMODE   extra engine arguments are passed through
# FARCRY_DIR points at the game install (default below); RENDERER=OpenGL runs the GL reference.
# The FARCRY_* development variables (FARCRY_GLES_DEBUG, FARCRY_FPS_EVERY, ...) pass through the environment.
GAME=${FARCRY_DIR:-/Users/emilebelanger/Android/Games/FarCry}
RENDERER=${RENDERER:-GLES}
MAP=$1
[ $# -gt 0 ] && shift
cd "$GAME/macos" || { echo "no $GAME/macos (see BUILDING-macOS.md)"; exit 1; }
if [ -n "$MAP" ]; then
  exec ./FarCry -DEVMODE "-RENDERER:$RENDERER" "\"map $MAP\"" "$@"
else
  exec ./FarCry -DEVMODE "-RENDERER:$RENDERER" "$@"
fi
