#!/bin/bash
# Clone Preact and build UMD bundles (unminified, uncompressed) for core and hooks
# Usage: bash scripts/build_preact.sh

set -e

PREACT_DIR="external/preact"


if [ ! -d "$PREACT_DIR" ]; then
  echo "Error: $PREACT_DIR does not exist. Please initialize the submodule first."
  exit 1
fi

cd "$PREACT_DIR"

# Install dependencies
if [ ! -d "node_modules" ]; then
  npm install
fi

# Build preact core UMD (unminified, uncompressed)
npx microbundle build --no-minify --no-compress -f umd --cwd .

# Build preact hooks UMD (unminified, uncompressed)
npx microbundle build --no-minify --no-compress -f umd --cwd hooks

# Copy resulting UMD files to src/
cd - > /dev/null
cp external/preact/dist/preact.umd.js build/preact.js
cp external/preact/hooks/dist/hooks.umd.js build/preact_hooks.js

# Clean untracked files from the preact submodule to avoid dirty state
cd external/preact
git clean -fdx
cd - > /dev/null

echo "Preact and hooks UMD builds complete, copied to build/, and submodule cleaned."
