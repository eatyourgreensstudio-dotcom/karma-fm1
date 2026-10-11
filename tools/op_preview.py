#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the host UI test, run it and lay its screenshots out on one sheet (no hardware, no toolchain):
  tools/op_preview.py OUT.png [screen ...]     (screens: names of build/host/*.ppm, default a few)
Needs build/gen (tools/build.py makes it) and Pillow."""
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
HOST = ROOT / "build" / "host"
DEFAULT = ["page-tracks", "live-grid", "live-kit", "page-edit", "page-env", "layer-mix"]


def main():
    out = Path(sys.argv[1])
    names = sys.argv[2:] or DEFAULT
    HOST.mkdir(parents=True, exist_ok=True)
    exe = HOST / "ui_pages_test"
    subprocess.run(["cc", "-O2", "-w", "-Ibuild/gen", "-Ifirmware/src", "-Ifirmware/hal", "-o", str(exe),
                    "tests/ui_pages_test.c", "-lm"], cwd=ROOT, check=True)
    r = subprocess.run([str(exe), str(HOST)], cwd=ROOT, capture_output=True, text=True)
    bad = [l for l in r.stdout.splitlines() if not l.rstrip().endswith(("ok", "PASS"))]
    print("\n".join(bad[-12:]) or "ui test: all ok")
    s, pad, lab, cols = 2, 16, 18, min(3, len(names))
    rows = (len(names) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * 240 * s + (cols + 1) * pad, rows * (240 * s + lab + pad) + pad), (40, 40, 44))
    d = ImageDraw.Draw(sheet)
    for i, n in enumerate(names):
        im = Image.open(HOST / f"{n}.ppm").convert("RGB")
        x, y = pad + (i % cols) * (240 * s + pad), pad + (i // cols) * (240 * s + lab + pad)
        sheet.paste(im.resize((240 * s, 240 * s), Image.NEAREST), (x, y))
        d.text((x, y + 240 * s + 4), n, fill=(220, 220, 220))
    sheet.save(out)
    print(out, sheet.size)


if __name__ == "__main__":
    main()
