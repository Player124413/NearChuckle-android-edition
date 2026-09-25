#!/bin/sh
# Launch Far Cry on the original OpenGL renderer (the reference), optionally straight into a map.
#   tools/run-gl.sh            menu
#   tools/run-gl.sh pier       load the Pier level
RENDERER=OpenGL exec "$(dirname "$0")/run-gles.sh" "$@"
