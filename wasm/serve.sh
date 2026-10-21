#!/bin/sh
# Stage the web build + ROMs into one dir and serve it on :8301 with COOP/COEP (needed later for pthreads).
# Usage: wasm/serve.sh [build-dir] [roms-dir]
set -e
SRC="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$SRC/../build-wasm}"
ROMS="${2:-$SRC/../roms}"
SITE="$BUILD/site"
mkdir -p "$SITE"
cp "$SRC/wasm/web/index.html" "$BUILD/AlberWeb.js" "$BUILD/AlberWeb.wasm" "$SITE/"
ln -sfn "$ROMS" "$SITE/roms"
exec python3 "$SRC/wasm/serve.py" "$SITE" 8301
