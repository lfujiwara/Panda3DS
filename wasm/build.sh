#!/bin/sh
# Configure + build the Emscripten targets. Usage: wasm/build.sh [build-dir] [ninja targets...]
set -e
SRC="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$SRC/../build-wasm}"
[ $# -gt 0 ] && shift
if [ ! -f "$BUILD/build.ninja" ]; then
	emcmake cmake -S "$SRC" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_C_FLAGS="-fwasm-exceptions" -DCMAKE_CXX_FLAGS="-fwasm-exceptions" \
		-DCMAKE_EXE_LINKER_FLAGS="-fwasm-exceptions" \
		-DENABLE_VULKAN=OFF -DENABLE_METAL=OFF -DENABLE_LUAJIT=OFF -DENABLE_DISCORD_RPC=OFF \
		-DENABLE_RENDERDOC_API=OFF -DENABLE_QT_GUI=OFF -DENABLE_FASTMEM=OFF -DENABLE_HTTP_SERVER=OFF \
		-DENABLE_GIT_VERSIONING=OFF -DOPENGL_PROFILE=OpenGLES -DBUILD_TESTING=OFF -DENABLE_WAYLAND=OFF
fi
ninja -C "$BUILD" "$@"
