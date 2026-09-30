#!/usr/bin/env bash
# Builds a release and packs it as voctv-console-linux-x86_64.tar.gz.
# Runtime needs the Qt 6 libraries: sudo apt install libqt6widgets6 libqt6charts6 libqt6datavisualization6 qt6-qpa-plugins
set -euo pipefail
BUILD_DIR="${BUILD_DIR:-build}"
cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR"
rm -rf dist && mkdir -p dist/voctv-console
install -m 755 "$BUILD_DIR/voctv-console" dist/voctv-console/
install -m 644 README.md dist/voctv-console/
tar -C dist --mode="u+rwX,go+rX" -czf voctv-console-linux-x86_64.tar.gz voctv-console
echo "Packaged voctv-console-linux-x86_64.tar.gz"
