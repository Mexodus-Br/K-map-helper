#!/bin/bash
set -e

if ! command -v emcc &> /dev/null; then
    echo "Error: emcc not found. Please install Emscripten and activate it (source emsdk_env.sh)."
    exit 1
fi

mkdir -p web

emcc main.cpp -O3 -std=c++17 --bind \
    -s WASM=1 \
    -s ALLOW_MEMORY_GROWTH=1 \
    -o web/kmap.js

echo "Build complete: web/kmap.js and web/kmap.wasm"
