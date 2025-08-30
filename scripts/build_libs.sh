#!/bin/bash
set -e

# Native libraries
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT_ROOT="$ROOT_DIR/build"
mkdir -p "$OUT_ROOT"

cpu_count() { command -v sysctl >/dev/null && sysctl -n hw.ncpu 2>/dev/null || nproc; }

build_skia() {
  local SKIA_DIR="$ROOT_DIR/external/skia"
  local OUT_DIR="$OUT_ROOT/skia"
  [ -d "$SKIA_DIR" ] || { echo "Skia dir missing: $SKIA_DIR"; return 1; }
  echo "[skia] deps";
  if [ ! -d "$SKIA_DIR/third_party/gn" ]; then (cd "$SKIA_DIR" && python3 tools/git-sync-deps); fi
  local GN_BIN="$SKIA_DIR/third_party/gn/gn"; [ -x "$GN_BIN" ] || chmod +x "$GN_BIN"
  echo "[skia] gen -> $OUT_DIR";
  ( cd "$SKIA_DIR" && "$GN_BIN" gen "$OUT_DIR" --args='is_official_build=true is_debug=false is_component_build=false skia_use_vulkan=true skia_use_system_libpng=false skia_use_system_icu=false skia_use_system_expat=false skia_use_system_harfbuzz=false skia_use_system_zlib=false skia_use_libjpeg_turbo_decode=false skia_use_libjpeg_turbo_encode=false skia_use_libwebp_decode=false skia_use_libwebp_encode=false skia_enable_tools=false skia_enable_pdf=false cc="clang" cxx="clang++"' )
  echo "[skia] build";
  ninja -C "$OUT_DIR"
  echo "[skia] done"
}

build_yoga() {
  local YOGA_DIR="$ROOT_DIR/external/yoga"
  local OUT_DIR="$OUT_ROOT/yoga"
  [ -d "$YOGA_DIR" ] || { echo "Yoga dir missing: $YOGA_DIR"; return 1; }
  mkdir -p "$OUT_DIR"; echo "[yoga] cmake (library only) -> $OUT_DIR";
  (cd "$OUT_DIR" && cmake "$YOGA_DIR/yoga" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF)
  echo "[yoga] build"; cmake --build "$OUT_DIR" -- -j$(cpu_count);
  echo "[yoga] done"
}

build_lexbor() {
  local LEXBOR_DIR="$ROOT_DIR/external/lexbor"
  local OUT_DIR="$OUT_ROOT/lexbor"
  [ -d "$LEXBOR_DIR" ] || { echo "Lexbor dir missing: $LEXBOR_DIR"; return 1; }
  mkdir -p "$OUT_DIR"; echo "[lexbor] cmake -> $OUT_DIR";
  (cd "$OUT_DIR" && cmake "$LEXBOR_DIR" -DCMAKE_BUILD_TYPE=Release -DLEXBOR_BUILD_SHARED=OFF -DLEXBOR_BUILD_STATIC=ON -DBUILD_SHARED_LIBS=OFF)
  echo "[lexbor] build"; cmake --build "$OUT_DIR" -- -j$(cpu_count);
  echo "[lexbor] done"
}

build_quickjs() {
  local QJS_DIR="$ROOT_DIR/external/quickjs"
  local OUT_DIR="$OUT_ROOT/quickjs"
  [ -d "$QJS_DIR" ] || { echo "QuickJS dir missing: $QJS_DIR"; return 1; }
  mkdir -p "$OUT_DIR"; echo "[quickjs-ng] cmake build";
  (cd "$QJS_DIR" && cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS="-DNDEBUG")
  (cd "$QJS_DIR" && cmake --build build --config Release)
  cp "$QJS_DIR/build/libqjs.a" "$OUT_DIR/"
  echo "[quickjs-ng] done"
}

build_preact() {
  PREACT_DIR="$ROOT_DIR/external/preact"
  if [ ! -d "$PREACT_DIR" ]; then
    echo "Error: $PREACT_DIR does not exist. Please initialize the submodule first."
    exit 1
  fi
  cd "$PREACT_DIR"
  if [ ! -d "node_modules" ]; then
    npm install
  fi
  npx microbundle build --no-minify --no-compress -f umd --cwd .
  npx microbundle build --no-minify --no-compress -f umd --cwd hooks
  cd - > /dev/null
  cp "$PREACT_DIR/dist/preact.umd.js" "$OUT_ROOT/preact.js"
  cp "$PREACT_DIR/hooks/dist/hooks.umd.js" "$OUT_ROOT/preact_hooks.js"
  cd "$PREACT_DIR"
  git clean -fdx
  cd - > /dev/null
  echo "Preact and hooks UMD builds complete, copied to build/, and submodule cleaned."
}

build_all() { build_skia; build_yoga; build_lexbor; build_quickjs; build_preact; echo "[all] outputs -> $OUT_ROOT"; }

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
  if [ $# -eq 0 ]; then build_all; else
    for t in "$@"; do
      case $t in
        skia) build_skia;; yoga) build_yoga;; lexbor) build_lexbor;; quickjs) build_quickjs;; preact) build_preact;; all) build_all;;
        *) echo "Unknown target $t"; exit 1;;
      esac
    done
  fi
fi
