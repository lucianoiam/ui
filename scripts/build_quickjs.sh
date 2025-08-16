#!/bin/bash
set -e

QUICKJS_DIR="$(pwd)/../external/quickjs"

if [ ! -d "$QUICKJS_DIR" ]; then
    echo "Error: QuickJS directory not found at $QUICKJS_DIR"
    exit 1
fi

cd "$QUICKJS_DIR"

echo "Building QuickJS..."
make -j$(nproc) libquickjs.a

echo "QuickJS static library built successfully in $QUICKJS_DIR"
