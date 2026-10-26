#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The simulator-page video: tests/op_v3.c's take (build/video/tour.{rgb,pcm,state}) drawn as KARMA's browser simulator
(stills of web/emu's page from headless Chrome: lights off / lit / glowing / held, build/video/sim_*.png at 2x of a
1400 px window), the live screen in its place, the LEDs as the page draws them, a pointer clicking and turning, framed as a
browser window at the site's address; then a card. Writes OUT/fm1-karma-sim-reel.mp4 (9:16: the page and a close-up of
the screen) and OUT/fm1-karma-sim.mp4 (16:9).
  python3 tools/sim_video.py OUTDIR"""
import math
import struct
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
V = ROOT / "build/video"
OUT = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT.parent / "videos")
sys.path.insert(0, str(ROOT / "tools"))
import gen_logo  # noqa: E402  (the KARMA letters)

FPS = 44100 / 704
DEV = (172.5, 24.0, 1040.0, 648.953125)          # the device on the page (CSS px, 1400 px window), the stills at 2x
CANVAS = (484.5, 78.078125, 249.59375, 249.59375)
BTN = {"FX": (777.8, 205, 56.2, 49.9), "SEL": (843.3, 205, 56.2, 49.9), "ENV": (908.8, 205, 56.2, 49.9),
       "LFO": (974.3, 205, 56.2, 49.9), "EDIT": (1039.9, 205, 56.2, 49.9), "GLO": (1105.4, 205, 56.2, 49.9),
       "HOME": (777.8, 269.4, 56.2, 49.9), "SAVE": (843.3, 269.4, 56.2, 49.9), "ARP": (908.8, 269.4, 56.2, 49.9),
       "SEQ": (974.3, 269.4, 56.2, 49.9), "PLAY": (1039.9, 269.4, 56.2, 49.9), "REC": (1105.4, 269.4, 56.2, 49.9),
       "OCT-": (234.9, 292.3, 93.6, 35.4), "OCT+": (349.3, 292.3, 93.6, 35.4)}
BORDER = ["FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"]
KNOB = [(256.7, 103, 49.9), (371.1, 103, 49.9), (256.7, 212.2, 49.9), (371.1, 212.2, 49.9),
        (784, 105.1, 45.8), (892.2, 105.1, 45.8), (1000.3, 105.1, 45.8), (1108.5, 105.1, 45.8)]
WHITE = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26]
BLACK = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25]
KEYX = [228.9, 287.6, 346.4, 405.2, 463.9, 522.7, 581.4, 640.2, 699, 757.7, 816.5, 875.2, 934, 992.8, 1051.5, 1110.3,
        258.3, 317, 375.8, 493.3, 552.1, 669.6, 728.3, 787.1, 904.6, 963.4, 1080.9]
KEY = {}
for i, x in enumerate(KEYX):
    KEY[WHITE[i] if i < 16 else BLACK[i - 16]] = (x, 512.4 if i < 16 else 379.3, 45.8, 113.2)
URL = "eatyourgreensstudio-dotcom.github.io/karma-fm1/emu/"
BG, INK, DIM = (12, 12, 17), (236, 235, 243), (150, 148, 168)
MENLO = "/System/Library/Fonts/Menlo.ttc"


def font(size, bold=False):
    return ImageFont.truetype(MENLO, size, index=1 if bold else 0)


def still(name):                                      # the device, cropped from a 2x still
    im = Image.open(V / f"sim_{name}.png").convert("RGB")
    x, y, w, h = DEV
    return im.crop((round(x * 2), round(y * 2), round((x + w) * 2), round((y + h) * 2)))


def box(r, k):                                        # a page rect (CSS) in a device image scaled by k (2x still = 2)
    x, y, w, h = r
    return (round((x - DEV[0]) * k), round((y - DEV[1]) * k), round((x - DEV[0] + w) * k), round((y - DEV[1] + h) * k))


def wordmark(cell):
    c, sq = cell, cell - max(1, cell // 7)
    im = Image.new("RGB", (29 * c, 7 * c), BG)
    d = ImageDraw.Draw(im)
    for g, pts in gen_logo.KARMA_CELLS.items():
        for x, y in pts:
            d.rectangle([x * c, y * c, x * c + sq - 1, y * c + sq - 1], fill=gen_logo.KARMA_COL[g])
    return im


def pointer(d, x, y, s, press):                       # an arrow cursor, tip at (x, y); a ring when pressing
    if press:
        r = 18 * s
        d.ellipse([x - r, y - r, x + r, y + r], outline=(255, 244, 214), width=max(2, round(3 * s)))
    pts = [(0, 0), (0, 21), (5, 16), (9, 25), (12, 24), (8, 15), (15, 15)]
    poly = [(x + px * s * 1.3, y + py * s * 1.3) for px, py in pts]
    d.polygon(poly, fill=(255, 255, 255), outline=(0, 0, 0))
    d.line(poly + [poly[0]], fill=(0, 0, 0), width=max(1, round(1.5 * s)))


def window(content, title_w):                         # a browser window around the page content
    w, h = content.size
    bar = 56
    im = Image.new("RGB", (w, h + bar), (142, 142, 147))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, w - 1, h + bar - 1], radius=18, fill=(142, 142, 147))
    d.rounded_rectangle([0, 0, w - 1, bar + 18], radius=18, fill=(43, 43, 48))
    d.rectangle([0, bar - 2, w - 1, bar], fill=(43, 43, 48))
    for i, c in enumerate([(255, 95, 87), (254, 188, 46), (40, 200, 64)]):
        d.ellipse([22 + i * 26, bar // 2 - 7, 36 + i * 26, bar // 2 + 7], fill=c)
    uw = min(title_w, w - 220)
    ux = (w - uw) // 2
    d.rounded_rectangle([ux, 12, ux + uw, bar - 12], radius=10, fill=(28, 28, 32))
    f = font(19)
    d.text((ux + uw // 2, bar // 2), "\U0001F512 " + URL if False else URL, font=f, fill=(210, 210, 216), anchor="mm")
    im.paste(content, (0, bar))
    return im


def main():
    rgb = (V / "tour.rgb").read_bytes()
    st = (V / "tour.state").read_bytes()
    n = min(len(rgb) // (240 * 240 * 3), len(st) // 32)
    off, lit, dim, held = still("still"), still("lit"), still("dim"), still("held")
    dims = off.size

    # where the pointer goes: a button, a key, a knob
    def centre(r):
        x, y, w, h = r
        return (x + w / 2, y + h / 2)
    px, py = centre(CANVAS)
    px += 160
    py += 160
    card_n = round(2.6 * FPS)
    procs = {}
    layouts = {"reel": (1080, 1920), "wide": (1920, 1080)}
    for name, (W, H) in layouts.items():
        out = OUT / ("fm1-karma-sim-reel.mp4" if name == "reel" else "fm1-karma-sim.mp4")
        dur = (n + card_n) / FPS
        procs[name] = subprocess.Popen(
            ["ffmpeg", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}",
             "-framerate", "44100/704", "-i", "-", "-f", "s16le", "-ar", "44100", "-ac", "2", "-i", str(V / "tour.pcm"),
             "-filter_complex", f"[1:a]afade=t=in:d=0.005,afade=t=out:st={n / FPS - 2.4:.2f}:d=2.3,apad[a]",
             "-map", "0:v", "-map", "[a]", "-t", f"{dur:.3f}", "-vf", "fps=60",
             "-c:v", "libx264", "-preset", "medium", "-crf", "16", "-pix_fmt", "yuv420p",
             "-c:a", "aac", "-b:a", "256k", "-movflags", "+faststart", str(out)], stdin=subprocess.PIPE)
    wm_reel, wm_wide = wordmark(9), wordmark(8)
    for f in range(n + card_n):
        if f < n:
            v = struct.unpack_from("<8I", st, f * 32)
            litB, litK, dimB, dimK, heldB, notes, cb, ck = v
            cb = cb if cb < 0x80000000 else -1
            ck = ck if ck < 0x80000000 else -1
            dev = off.copy()
            for b, nm in enumerate(BORDER):
                r = box(BTN[nm], 2)
                if (litB >> b) & 1:
                    dev.paste(lit.crop(r), r[:2])
                elif (dimB >> b) & 1:
                    dev.paste(dim.crop(r), r[:2])
                if (heldB >> b) & 1 or b == cb:
                    dev.paste(held.crop(r), r[:2])
            for k, rr in KEY.items():
                r = box(rr, 2)
                if (litK >> k) & 1 or (notes >> k) & 1:
                    dev.paste(lit.crop(r), r[:2])
                elif (dimK >> k) & 1:
                    dev.paste(dim.crop(r), r[:2])
            fr = Image.frombuffer("RGB", (240, 240), rgb[f * 172800:(f + 1) * 172800], "raw", "RGB", 0, 1)
            cr = box(CANVAS, 2)
            dev.paste(fr.resize((cr[2] - cr[0], cr[3] - cr[1]), Image.NEAREST), cr[:2])
            # the pointer: to what is held, a key down, a click, a knob turned; else it rests
            tgt, press = None, False
            for b, nm in enumerate(BORDER):
                if (heldB >> b) & 1 or b == cb:
                    tgt, press = centre(BTN[nm]), True
            if tgt is None and notes:
                k = (notes & -notes).bit_length() - 1
                if k in KEY:
                    tgt, press = centre(KEY[k]), True
            if tgt is None and ck >= 0:
                x, y, dd = KNOB[ck]
                tgt = (x + dd / 2, y + dd / 2)
            if tgt is None:
                tgt = (CANVAS[0] + CANVAS[2] + 30, CANVAS[1] + CANVAS[3] - 10)
            px += (tgt[0] - px) * 0.22
            py += (tgt[1] - py) * 0.22
            dd_ = ImageDraw.Draw(dev)
            if ck >= 0:                                 # a knob being turned: an arc running round it
                x, y, dd = KNOB[ck]
                a = (f * 9) % 360
                bx = box((x - 6, y - 6, dd + 12, dd + 12), 2)
                dd_.arc(bx, a, a + 200, fill=(255, 244, 214), width=5)
            pointer(dd_, (px - DEV[0]) * 2, (py - DEV[1]) * 2, 2.0, press)
            last = (dev, fr)
        else:
            dev, fr = last
        k_card = 0 if f < n else min(1.0, (f - n) / (0.5 * FPS))
        for name, (W, H) in layouts.items():
            img = Image.new("RGB", (W, H), BG)
            d = ImageDraw.Draw(img)
            if name == "reel":
                img.paste(wm_reel, ((W - wm_reel.width) // 2, 70))
                d.text((W // 2, 160), "now running in your browser", font=font(30), fill=DIM, anchor="mm")
                cw = 1020
                content = Image.new("RGB", (cw, round(cw * dims[1] / dims[0]) + 40), (142, 142, 147))
                content.paste(dev.resize((cw - 40, round((cw - 40) * dims[1] / dims[0])), Image.LANCZOS), (20, 20))
                win = window(content, 760)
                img.paste(win, ((W - win.width) // 2, 210))
                y0 = 210 + win.height + 40
                s = min(W - 120, H - y0 - 150)
                d.rounded_rectangle([(W - s) // 2 - 14, y0 - 14, (W + s) // 2 + 14, y0 + s + 14], radius=26, fill=(8, 8, 11))
                img.paste(fr.resize((s, s), Image.NEAREST), ((W - s) // 2, y0))
                d.text((W // 2, H - 70), "KARMA · free firmware for the M-VAVE FM-1", font=font(30, True), fill=INK, anchor="mm")
            else:
                cw = 1600
                content = Image.new("RGB", (cw, round(cw * dims[1] / dims[0]) + 50), (142, 142, 147))
                content.paste(dev.resize((cw - 50, round((cw - 50) * dims[1] / dims[0])), Image.LANCZOS), (25, 25))
                win = window(content, 900)
                sc = min(1.0, (H - 150) / win.height)
                if sc < 1:
                    win = win.resize((round(win.width * sc), round(win.height * sc)), Image.LANCZOS)
                img.paste(win, ((W - win.width) // 2, 110))
                img.paste(wm_wide, (60, 34))
                d.text((W - 60, 62), "now running in your browser", font=font(28), fill=DIM, anchor="rm")
            if k_card > 0:                              # the card: KARMA, try it now, the FM-1 soon
                card = Image.new("RGB", (W, H), BG)
                cd = ImageDraw.Draw(card)
                wm = wordmark(26 if name == "reel" else 30)
                cy = H // 2 - 170
                card.paste(wm, ((W - wm.width) // 2, cy))
                cd.text((W // 2, cy + wm.height + 80), "TRY IT IN YOUR BROWSER", font=font(48, True), fill=INK, anchor="mm")
                cd.text((W // 2, cy + wm.height + 140), "coming soon to your FM-1", font=font(36), fill=(255, 107, 31), anchor="mm")
                cd.text((W // 2, cy + wm.height + 210), URL, font=font(26), fill=DIM, anchor="mm")
                img = Image.blend(img, card, k_card)
            procs[name].stdin.write(img.tobytes())
        if f % 500 == 0:
            print(f"{f}/{n + card_n}", flush=True)
    for p in procs.values():
        p.stdin.close()
        p.wait()
    print("done")


if __name__ == "__main__":
    main()
