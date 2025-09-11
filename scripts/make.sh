#!/bin/bash
set -euo pipefail

# Build application binary (a.out) into build/ using static libraries in build/{skia,yoga,lexbor,quickjs}.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
OUT_BIN="$BUILD_DIR/a.out"

SRC_DIR="$ROOT_DIR/src"

SKIA_LIB="$BUILD_DIR/skia/libskia.a"
YOGA_LIB="$BUILD_DIR/yoga/libyogacore.a"
LEXBOR_LIB="$BUILD_DIR/lexbor/liblexbor_static.a"
QUICKJS_LIB="$BUILD_DIR/quickjs/libqjs.a"

# If build/ directory does not exist, build the libs
# Ensure static libraries exist

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
STD="-std=c++20"

# Initialize flags to avoid unbound variable when DEBUG not set (set -u)
CXXFLAGS="${CXXFLAGS:-}"
LDFLAGS="${LDFLAGS:-}"
CFLAGS="${CFLAGS:-}"

if [ "${DEBUG:-0}" = "1" ]; then
  echo "[make.sh] DEBUG build with AddressSanitizer"
  CXXFLAGS+=" -O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined"
  CFLAGS+=" -O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined"
  LDFLAGS+=" -fsanitize=address,undefined"
else
  # Default release-style optimization if not explicitly provided
  if [[ "${CXXFLAGS}" != *"-O"* ]]; then
    if [ "${RELEASE_DBG:-0}" = "1" ]; then
      CXXFLAGS+=" -O2 -g -DNDEBUG"
    else
      CXXFLAGS+=" -O3 -flto -DNDEBUG"
    fi
  fi
  if [[ "${CFLAGS}" != *"-O"* ]]; then
    if [ "${RELEASE_DBG:-0}" = "1" ]; then
      CFLAGS+=" -O2 -g -DNDEBUG"
    else
      CFLAGS+=" -O3 -flto -DNDEBUG"
    fi
  fi
fi

# Feature flags from env
if [ "${DOM_DISABLE_STYLE:-0}" = "1" ]; then
  CXXFLAGS+=" -DDOM_DISABLE_STYLE"
fi

# Strict mode removes convenience non-standard helpers (className property & style.cssText object)
if [ "${DOM_STRICT:-0}" = "1" ]; then
  CXXFLAGS+=" -DDOM_STRICT"
fi

INCLUDES=(
  -I"$ROOT_DIR/external/skia"
  -I"$ROOT_DIR/external/quickjs"
  -I"$ROOT_DIR/external/yoga"
  -I"$ROOT_DIR/external/lexbor/source"
  -I"$SRC_DIR"
  -I"$SRC_DIR/wapis"
)

# C++ sources
SOURCES=(
  "$SRC_DIR/main.mm"
  "$SRC_DIR/renderer/sk_canvas_view.cpp"
  "$SRC_DIR/renderer/renderer.cpp"
  "$SRC_DIR/renderer/scheduler.cpp"
  "$SRC_DIR/renderer/element_data.cpp"
  "$SRC_DIR/wapis/dom_adapter.cpp"
  "$SRC_DIR/wapis/dom.cpp"
  "$SRC_DIR/renderer/layout_yoga.cpp"
  "$SRC_DIR/renderer/css_parser.cpp"
  "$SRC_DIR/wapis/whatwg.c"
  "$SRC_DIR/input/input.cpp"
  "$SRC_DIR/input/mac.mm"
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
  -framework Metal
  -framework QuartzCore
)


# Compile C and C++/ObjC++ sources separately to object files
OBJ_FILES=()
for src in "${SOURCES[@]}"; do
  ext="${src##*.}"
  obj="$BUILD_DIR/$(basename "$src" | sed 's/\.[^.]*$/.o/')"
  if [[ "$ext" == "c" ]]; then
    clang -c $CFLAGS "$src" "${INCLUDES[@]}" -o "$obj"
  else
  "$CXX" -c $STD $CXXFLAGS "$src" "${INCLUDES[@]}" -o "$obj"
  fi
  OBJ_FILES+=("$obj")
done

echo "Linking -> $OUT_BIN"
"$CXX" $STD $CXXFLAGS "${OBJ_FILES[@]}" "${LIBS[@]}" $LDFLAGS -o "$OUT_BIN"
echo "Done: $OUT_BIN"
