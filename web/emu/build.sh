#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The firmware in the browser: web/emu/sloop_web.c compiled to WebAssembly with Emscripten, plus the page
# (after Felucca's web/emu/build.sh, hugelton/Felucca, GPL-3.0).
#   sh web/emu/build.sh   ->  build/emu/{index.html, worklet.js, sloop.wasm, fonts/}
# Run after one ./build.sh or tools/build.py generate() (build/gen: the tables, the fonts, the samples).
# -ffp-contract=off: no fused multiply-adds, as the host build.
set -e
cd "$(dirname "$0")/../.."
[ -f build/gen/felucca_tables.h ] || { echo "emu: run ./build.sh once first (build/gen)"; exit 1; }
command -v emcc >/dev/null 2>&1 || { echo "emu: no emcc (Emscripten)"; exit 1; }
mkdir -p build/emu/fonts
emcc -O2 -ffp-contract=off -std=gnu99 -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal -Itests \
    --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=33554432 -sFILESYSTEM=0 \
    -o build/emu/sloop.wasm web/emu/sloop_web.c
cp web/emu/index.html web/emu/worklet.js web/emu/cheatsheet.html build/emu/
cp web/emu/fonts/* build/emu/fonts/
echo "emu: build/emu ($(wc -c < build/emu/sloop.wasm | tr -d ' ') B wasm)"
