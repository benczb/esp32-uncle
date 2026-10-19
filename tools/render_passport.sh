#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S tests/passport_render -B build/passport-render
cmake --build build/passport-render -j "${PASSPORT_BUILD_JOBS:-8}"
mkdir -p build/screens
build/passport-render/render build/screens/passport
