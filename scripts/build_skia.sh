#!/bin/bash
set -e

SKIA_DIR="$(pwd)/../external/skia"

if [ ! -d "$SKIA_DIR" ]; then
    echo "Error: Skia directory not found at $SKIA_DIR"
    exit 1
fi

cd "$SKIA_DIR"

if [ ! -d "third_party/gn" ]; then
    echo "Syncing dependencies..."
    python3 tools/git-sync-deps
else
    echo "Dependencies already synced, skipping git-sync-deps..."
fi

GN_BIN="$SKIA_DIR/third_party/gn/gn"
if [ ! -x "$GN_BIN" ]; then
    echo "Making gn binary executable..."
    chmod +x "$GN_BIN"
fi

echo "Configuring Skia build..."
"$GN_BIN" gen out/Static --args='
    is_official_build=true
    is_debug=false
    is_component_build=false

    skia_use_vulkan=true

    skia_use_system_libpng=false
    skia_use_system_icu=false
    skia_use_system_expat=false
    skia_use_system_harfbuzz=false
    skia_use_system_zlib=false

    skia_use_libjpeg_turbo_decode=false
    skia_use_libjpeg_turbo_encode=false
    skia_use_libwebp_decode=false
    skia_use_libwebp_encode=false

    skia_enable_pdf=false
'

echo "Building Skia..."
ninja -C out/Static

echo "Skia static library built successfully in $SKIA_DIR/out/Static"
