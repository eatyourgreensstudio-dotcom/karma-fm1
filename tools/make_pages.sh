#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# KARMA's website for GitHub Pages (served from docs/ on the main branch):
#   docs/index.html, img/, fonts/   the landing page (web/landing)
#   docs/emu/                       the browser simulator (build/emu: build it first, sh web/emu/build.sh)
#   docs/webapp/editor/             the web editor (web/editor.html + its icon font)
#   docs/webapp/installer/          "coming soon" until KARMA has been tested on hardware (then: web/make_site.py)
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
cp web/editor.html docs/webapp/editor/index.html
cp web/fukiai.ttf web/FUKIAI-LICENSE.txt docs/webapp/editor/
cat > docs/webapp/installer/index.html <<'HTML'
<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>KARMA installer</title>
<style>
:root { --bg: #0c0c11; --fg: #ecebf3; --muted: #a09eb3; --acc: #ff6b1f; color-scheme: dark; }
@media (prefers-color-scheme: light) { :root { --bg: #f2f1f6; --fg: #15141c; --muted: #5d5b6e; --acc: #c64a07; color-scheme: light; } }
body { margin: 0; background: var(--bg); color: var(--fg); font: 17px/1.6 "Helvetica Neue", Arial, sans-serif; }
main { max-width: 36rem; margin: 0 auto; padding: 18vh 16px 4rem; }
h1 { font-size: 2rem; margin: 0 0 1rem; } p { color: var(--muted); } a { color: var(--acc); }
</style></head><body><main>
<h1>KARMA installer: coming soon</h1>
<p>The installer opens once KARMA has been tested on a real FM-1. The first unit arrives this week.</p>
<p>Meanwhile, the <a href="../../emu/">browser simulator</a> runs the same firmware. <a href="../../">Back to KARMA</a>.</p>
</main></body></html>
HTML
du -sh docs
