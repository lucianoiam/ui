#!/bin/bash
set -e

LEXBOR_DIR="$(pwd)/../external/lexbor"

if [ ! -d "$LEXBOR_DIR" ]; then
    echo "Error: Lexbor directory not found at $LEXBOR_DIR"
    exit 1
fi

BUILD_DIR="$LEXBOR_DIR/build"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "Configuring Lexbor build with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF

echo "Building Lexbor..."
cmake --build . -- -j$(nproc)

echo "Lexbor static library built successfully in $BUILD_DIR"
