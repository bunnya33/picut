#!/usr/bin/env bash
set -euo pipefail
work="$(mktemp -d)"
trap 'rm -r "$work"' EXIT
iconset="$work/picut.iconset"
mkdir "$iconset"
for size in 16 32 128 256 512; do
    sips -s format png -z "$size" "$size" "$1" --out "$iconset/icon_${size}x${size}.png" >/dev/null
    sips -s format png -z "$((size * 2))" "$((size * 2))" "$1" \
        --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$iconset" -o "$2"
