#!/bin/sh
# Run every ROM in the served roms/ dir through the browser smoke test. Needs wasm/serve.sh running.
# Usage: wasm/gauntlet.sh [roms-dir] [out-dir] [frames]
SRC="$(cd "$(dirname "$0")/.." && pwd)"
ROMS="${1:-$SRC/../roms}"
OUT="${2:-$SRC/../shots}"
FRAMES="${3:-180}"
mkdir -p "$OUT"
for rom in "$ROMS"/*.3dsx; do
	name="$(basename "$rom" .3dsx)"
	node "$SRC/wasm/smoke.mjs" "roms/$name.3dsx" "$FRAMES" "$OUT/$name.png" 2>&1 | grep '^\[smoke\]'
done
