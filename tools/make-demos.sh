#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

ninja -C build

export XDG_RUNTIME_DIR
XDG_RUNTIME_DIR=$(mktemp -d)
chmod 700 "$XDG_RUNTIME_DIR"

FRAMES=$(mktemp -d)
export BBAI_DEMO_DIR="$FRAMES"

WLR_BACKENDS=headless WLR_RENDERER=pixman meson test -C build --suite demo

mkdir -p demos
for d in "$FRAMES"/*/; do
  name=$(basename "$d")
  ffmpeg -y -framerate 3 -pattern_type glob -i "$d/*.png" \
    -c:v libopenh264 -pix_fmt yuv420p -vf "scale=trunc(iw/2)*2:trunc(ih/2)*2" \
    "demos/$name.mp4" </dev/null
  echo "wrote demos/$name.mp4"
done

echo "demos in: $(pwd)/demos"
