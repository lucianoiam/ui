#!/bin/bash
set -euo pipefail

# Build application binary (a.out) into top-level build/ using previously
# built static libraries under build/{skia,yoga,lexbor,quickjs}.
# Script can be invoked from any directory.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
OUT_BIN="$BUILD_DIR/a.out"

SRC_DIR="$ROOT_DIR/src"

SKIA_LIB="$BUILD_DIR/skia/libskia.a"
YOGA_LIB="$BUILD_DIR/yoga/libyogacore.a"
LEXBOR_LIB="$BUILD_DIR/lexbor/liblexbor_static.a"
QUICKJS_LIB="$BUILD_DIR/quickjs/libquickjs.a"

missing=()
for lib in "$SKIA_LIB" "$YOGA_LIB" "$LEXBOR_LIB" "$QUICKJS_LIB"; do
  [ -f "$lib" ] || missing+=("$lib")
done

if [ ${#missing[@]} -ne 0 ]; then
  echo "Missing static libs:" >&2
  for m in "${missing[@]}"; do echo "  $m" >&2; done
  echo "Run: scripts/build_static_libs.sh (or specific targets) first." >&2
  exit 1
fi

mkdir -p "$BUILD_DIR"

CXX=${CXX:-c++}
STD="-std=c++17"

INCLUDES=(
  -I"$ROOT_DIR/external"
  -I"$ROOT_DIR/external/skia"
  -I"$ROOT_DIR/external/yoga"
  -I"$ROOT_DIR/external/lexbor/source"
)

# C++ sources
SOURCES=(
  "$SRC_DIR/main.mm"
  "$SRC_DIR/SkiaDisplay.mm"
  "$SRC_DIR/preact_js.cpp"
)

LIBS=(
  "$SKIA_LIB"
  "$YOGA_LIB"
  "$LEXBOR_LIB"
  "$QUICKJS_LIB"
  -lpthread -lm
  -framework Cocoa
  -framework CoreFoundation
  -framework CoreGraphics
  -framework CoreText
  -framework CoreServices
  -framework ApplicationServices
  -framework JavaScriptCore
)

echo "Linking -> $OUT_BIN"
"$CXX" $STD "${SOURCES[@]}" "${INCLUDES[@]}" "${LIBS[@]}" -o "$OUT_BIN"
echo "Done: $OUT_BIN"
echo "Running..."
"$OUT_BIN" &
APP_PID=$!
echo "(launched pid $APP_PID)"
