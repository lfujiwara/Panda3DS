#!/bin/sh
# Run every ROM in the served roms/ dir through the browser smoke test. Needs wasm/serve.sh running.
# Usage: wasm/gauntlet.sh [roms-dir] [out-dir] [frames]
SRC="$(cd "$(dirname "$0")/.." && pwd)"
ROMS="${1:-$SRC/../roms}"
OUT="${2:-$SRC/../shots}"
FRAMES="${3:-180}"
mkdir -p "$OUT"
for rom in "$ROMS"/*.3dsx "$ROMS"/*.cxi "$ROMS"/*.3ds; do
	[ -f "$rom" ] || continue
	file="$(basename "$rom")"
	node "$SRC/wasm/smoke.mjs" "roms/$file" "$FRAMES" "$OUT/${file%.*}.png" 2>&1 | grep '^\[smoke\]'
done
