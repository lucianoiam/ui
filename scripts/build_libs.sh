#!/bin/bash
set -e

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
  local ENABLE_GPU=${SKIA_GPU:-1}
  # Minimal stable set of args; GPU (Metal) is on by default in Skia when platform supported.
  # We only explicitly disable Metal when SKIA_GPU=0 to avoid adding unused args that trigger warnings.
  local ARGS="is_debug=false is_official_build=false is_component_build=false skia_enable_tools=false"
  if [ "$ENABLE_GPU" = "0" ]; then
    ARGS+=" skia_use_metal=false skia_use_vulkan=false"
  else
    ARGS+=" skia_use_metal=true"
  fi
  ARGS+=" cc=\"clang\" cxx=\"clang++\""
  echo "[skia] gen -> $OUT_DIR (gpu=$ENABLE_GPU, metal=$([ "$ENABLE_GPU" = "0" ] && echo off || echo on))";
  ( cd "$SKIA_DIR" && "$GN_BIN" gen "$OUT_DIR" --args="$ARGS" )
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
  PREACT_OUT_JS="$OUT_ROOT/preact.js"
  PREACT_HOOKS_OUT_JS="$OUT_ROOT/preact_hooks.js"
  if [ -f "$PREACT_OUT_JS" ] && [ -f "$PREACT_HOOKS_OUT_JS" ]; then
    echo "[preact] Skipping build: $PREACT_OUT_JS and $PREACT_HOOKS_OUT_JS already exist."
    return 0
  fi
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
  cp "$PREACT_DIR/dist/preact.umd.js" "$PREACT_OUT_JS"
  cp "$PREACT_DIR/hooks/dist/hooks.umd.js" "$PREACT_HOOKS_OUT_JS"
  cd "$PREACT_DIR"
  git clean -fdx
  cd - > /dev/null
  echo "Preact and hooks UMD builds complete, copied to build/, and submodule cleaned."
}

build_htm() {
  HTM_DIR="$ROOT_DIR/external/htm"
  HTM_OUT_JS="$OUT_ROOT/htm.js"
  if [ -f "$HTM_OUT_JS" ]; then
    echo "[htm] Skipping build: $HTM_OUT_JS already exists."
    return 0
  fi
  if [ ! -d "$HTM_DIR" ]; then
    echo "Error: $HTM_DIR does not exist. Please initialize the submodule first."
    exit 1
  fi
  cd "$HTM_DIR"
  if [ ! -d "node_modules" ]; then
    npm install
  fi
  npx microbundle src/index.mjs -f umd --no-sourcemap --no-minify --no-compress --target web
  cd - > /dev/null
  if [ -f "$HTM_DIR/dist/htm.umd.js" ]; then
    rm -f "$OUT_ROOT/htm.umd.js" 2>/dev/null || true
    cp "$HTM_DIR/dist/htm.umd.js" "$HTM_OUT_JS"
    (cd "$HTM_DIR" && git clean -fdx)
    echo "[htm] Single UMD build (unminified) copied to build/htm.js and submodule cleaned."
  else
    echo "[htm] ERROR: dist/htm.umd.js not produced." >&2
    exit 1
  fi
}

build_all() { build_skia; build_yoga; build_lexbor; build_quickjs; build_preact; build_htm; echo "[all] outputs -> $OUT_ROOT"; }

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
  if [ $# -eq 0 ]; then build_all; else
    for t in "$@"; do
      case $t in
  skia) build_skia;; yoga) build_yoga;; lexbor) build_lexbor;; quickjs) build_quickjs;; preact) build_preact;; htm) build_htm;; all) build_all;;
        *) echo "Unknown target $t"; exit 1;;
      esac
    done
  fi
fi
