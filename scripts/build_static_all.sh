#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cd "$SCRIPT_DIR"

./build_skia.sh
./build_yoga.sh
./build_lexbor.sh
./build_quickjs.sh
