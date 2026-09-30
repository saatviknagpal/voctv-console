#!/usr/bin/env bash
# Runs the scripted demo on a virtual X display and renders docs/screenshot.png and docs/demo.gif.
set -euo pipefail
BUILD_DIR="${BUILD_DIR:-build}"
APP="$BUILD_DIR/voctv-console"
export QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
unset WAYLAND_DISPLAY
XVFB=(xvfb-run -a -s "-screen 0 1600x1000x24")
"${XVFB[@]}" "$APP" --screenshot docs/screenshot.png
rm -rf frames
"${XVFB[@]}" "$APP" --capture-frames frames
ffmpeg -loglevel error -y -framerate 10 -i frames/frame_%04d.png \
    -vf "fps=10,scale=960:-1:flags=lanczos,split[a][b];[a]palettegen[p];[b][p]paletteuse" docs/demo.gif
rm -rf frames
echo "Wrote docs/screenshot.png and docs/demo.gif"
