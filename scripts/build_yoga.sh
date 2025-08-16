#!/bin/bash
set -e

YOGA_DIR="$(pwd)/../external/yoga"

if [ ! -d "$YOGA_DIR" ]; then
    echo "Error: Yoga directory not found at $YOGA_DIR"
    exit 1
fi

BUILD_DIR="$YOGA_DIR/build"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "Configuring Yoga build with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF

echo "Building Yoga..."
cmake --build . -- -j$(nproc)

echo "Yoga static library built successfully in $BUILD_DIR"
