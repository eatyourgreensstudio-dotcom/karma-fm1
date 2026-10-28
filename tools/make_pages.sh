#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# KARMA's website for GitHub Pages (served from docs/ on the main branch):
#   docs/index.html, img/, fonts/   the landing page (web/landing)
#   docs/emu/                       the browser simulator (build/emu: build it first, sh web/emu/build.sh)
#   docs/webapp/editor/             the web editor (web/editor.html + its icon font, and its offline copy)
#   docs/webapp/installer/          the web installer (web/make_site.py: build/karma.fwsc, the dual-core build)
#   docs/firmware/karma-VER.fwsc    the package it installs (KARMA_VERSION, default 3.0)
#   sh tools/make_pages.sh
set -e
cd "$(dirname "$0")/.."
[ -f build/emu/sloop.wasm ] || { echo "build the simulator first: sh web/emu/build.sh"; exit 1; }
rm -rf docs
mkdir -p docs/emu docs/webapp/editor docs/webapp/installer
touch docs/.nojekyll
cp web/landing/index.html docs/index.html
cp -R web/landing/img web/landing/fonts docs/
cp build/emu/index.html build/emu/worklet.js build/emu/sloop.wasm build/emu/cheatsheet.html docs/emu/
cp -R build/emu/fonts docs/emu/
# the installer, the editor (+ its offline copy) and the package: web/make_site.py, from the dual-core build
[ -f build/karma.fwsc ] || { echo "build the firmware first: sh build.sh"; exit 1; }
SITE=$(mktemp -d)
python3 web/make_site.py build/karma.fwsc "${KARMA_VERSION:-3.0}" "$SITE"
cp -R "$SITE/webapp" "$SITE/firmware" docs/
rm -rf "$SITE"
du -sh docs
