#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Makes firmware/src/lxr_transients.h and firmware/src/lxr_waves.h from the LXR firmware (Erica Synths,
# GPL-3.0-only), for KARMA's port of its drum voices (firmware/src/lxr_dsp.c):
#   lxr_transients.h  its transient samples (Core/Src/Soundengine/DSPAudio/transientTables.c). Each sample is cut
#                     after its last sample louder than TRIM_BELOW (int8; what follows is silence or the last of a
#                     tail) or at KEEP's length, with a 32-sample fade; only those in KEEP are kept (the flash of the
#                     FM-1); a transient not kept plays the one in FALLBACK
#   lxr_waves.h       of its band-limited wavetables (wavetable.c: 11 per wave, by octave) the one it plays below
#                     440 Hz (band 4), for the saw (half of it: the other half is its mirror) and the square
#   python3 tools/gen_lxr_tables.py [LXR_CHECKOUT]    (default ~/fm1-firmware/ref/lxr)
import os, re, sys

LXR = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/fm1-firmware/ref/lxr")
SRC = os.path.join(LXR, "Core/Src/Soundengine/DSPAudio/transientTables.c")
OUT = os.path.join(os.path.dirname(__file__), "..", "firmware", "src", "lxr_transients.h")

NAMES = ["CLK", "CK2", "TICK", "KICK", "RIM", "DRIP", "HAT", "CLAP", "KK2", "SNR", "TOM", "SP2"]   # their menu: Clk Ck2 Tik ..
KEEP = {"CLK": 400, "CK2": 900, "TICK": 1100, "KICK": 1300, "RIM": 1000, "DRIP": 0, "HAT": 1100, "CLAP": 1300,
        "KK2": 0, "SNR": 1300, "TOM": 1000, "SP2": 0}   # the most samples kept of each (0: not kept)
FALLBACK = {"DRIP": "TICK", "KK2": "KICK", "SP2": "SNR"}
TRIM_BELOW = 2
FADE = 32

src = open(SRC).read()
body = src[src.index("transientData"):]
blocks = re.findall(r"(//[^\n]*)?\s*\{([^{}]*)\}", body)
tables = []
for comment, b in blocks:
    b = re.sub(r"//[^\n]*", "", b)
    vals = [int(t, 16) if t.lower().startswith("0x") else int(t) for t in re.findall(r"-?0[xX][0-9a-fA-F]+|-?\d+", b)]
    vals = [v - 256 if v > 127 else v for v in vals]
    tables.append(vals)
comments = re.findall(r"//\s*([^\n]*)", body[:body.rindex("}")])
comments = [c.strip() for c in comments if c.strip()][:12]
assert len(tables) == 12, len(tables)

out = []
w = out.append
w("/*")
w(" * lxr_transients.h: the transient samples of the LXR-02 firmware (its transient generator's waveforms 2..13)")
w(" *")
w(" * Copyright (C) 2012-2026 Erica Synths")
w(" * SPDX-License-Identifier: GPL-3.0-only")
w(" *")
w(" * This file is part of the LXR firmware. It is licensed under the GNU General")
w(" * Public License version 3, with the additional permission and terms in")
w(" * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.")
w(" * (In KARMA: their NOTICE.md is LICENSES/LXR-NOTICE.md.)")
w(" *")
w(" * Portions Copyright (C) 2026 Ariel Muchnik: for KARMA (M-VAVE FM-1 custom firmware), made by")
w(" * tools/gen_lxr_tables.py from their Core/Src/Soundengine/DSPAudio/transientTables.c: trimmed (after the last")
w(" * sample louder than %d, a %d-sample fade) and not all of them; one not kept plays another (lx_tr_of)." % (TRIM_BELOW, FADE))
w(" *")
w(" * Their origin, as transientTables.h and transientTables.c credit it:")
w(" *   signed 8 bit pcm")
w(" *   4102__patchen__atik-2-10-stereoatik")
w(" * and per sample (the comments in transientTables.c):")
for n, c in zip(NAMES, comments):
    w(" *   %-5s %s" % (n, c))
w(" */")
w("")
total = 0
kept = []
for i, (n, t) in enumerate(zip(NAMES, tables)):
    t = t[:2205]
    if not KEEP[n]:
        continue
    last = max([j for j, v in enumerate(t) if abs(v) > TRIM_BELOW] or [0]) + 1
    ln = min(last, KEEP[n])
    t = t[:ln]
    for j in range(max(0, ln - FADE), ln):                  # fade the cut
        t[j] = int(round(t[j] * (ln - j) / (FADE + 1)))
    kept.append(n)
    total += ln
    w("static const int8_t LX_TR_%s[%d] = {   /* %s: %d of 2205 samples */" % (n, ln, comments[i], ln))
    for j in range(0, ln, 24):
        w("    " + ", ".join(str(v) for v in t[j:j + 24]) + ",")
    w("};")
w("static const struct { const int8_t *d; uint16_t n; } LX_TR[12] = {")
for n in NAMES:
    k = n if KEEP[n] else FALLBACK[n]
    w("    {LX_TR_%s, %d},%s" % (k, 0, "" if KEEP[n] else "   /* %s: not kept, %s */" % (n, k)))
w("};")
out_s = "\n".join(out) + "\n"
# the lengths into the table
for n in NAMES:
    k = n if KEEP[n] else FALLBACK[n]
    m = re.search(r"LX_TR_%s\[(\d+)\]" % k, out_s)
    out_s = out_s.replace("{LX_TR_%s, 0}" % k, "{LX_TR_%s, %s}" % (k, m.group(1)), 1)
out_s += """static inline const int8_t *lx_tr_of(uint32_t i, uint32_t *len)   /* transient sample i (0..11): data, length */
{
    *len = LX_TR[i % 12u].n;
    return LX_TR[i % 12u].d;
}
"""
open(OUT, "w").write(out_s)
print("lxr_transients.h: %d of 12 kept (%s), %d bytes" % (len(kept), " ".join(kept), total))

# ---- the wavetables' band 4
WSRC = os.path.join(LXR, "Core/Src/Soundengine/DSPAudio/wavetable.c")
WOUT = os.path.join(os.path.dirname(__file__), "..", "firmware", "src", "lxr_waves.h")
ws = open(WSRC).read()
ws = re.sub(r"//[^\n]*", "", ws)
ws = re.sub(r"/\*.*?\*/", "", ws, flags=re.S)
def table(name):
    i = ws.index(name)
    k = ws.index("{", ws.index("=", i))
    depth = 0
    for p in range(k, len(ws)):
        if ws[p] == "{":
            depth += 1
        elif ws[p] == "}":
            depth -= 1
            if depth == 0:
                break
    return [int(x) for x in re.findall(r"-?\d+", ws[k:p])]
saw = table("sawTable[")[4 * 1024:5 * 1024]
rec = table("recTable[")[4 * 1024:5 * 1024 + 1]   # (+ the next row's first: their interpolation reads it)
assert all(saw[(1024 - i) % 1024] == -saw[i] for i in range(1024))    # the saw: its mirror
o = []
w = o.append
w("/*")
w(" * lxr_waves.h: two of the LXR-02 firmware's band-limited wavetables: its saw and its square below 440 Hz (band 4)")
w(" *")
w(" * Copyright (C) 2012-2026 Erica Synths")
w(" * SPDX-License-Identifier: GPL-3.0-only")
w(" *")
w(" * This file is part of the LXR firmware. It is licensed under the GNU General")
w(" * Public License version 3, with the additional permission and terms in")
w(" * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.")
w(" * (In KARMA: their NOTICE.md is LICENSES/LXR-NOTICE.md.)")
w(" *")
w(" * Portions Copyright (C) 2026 Ariel Muchnik: for KARMA (M-VAVE FM-1 custom firmware), made by")
w(" * tools/gen_lxr_tables.py from their Core/Src/Soundengine/DSPAudio/wavetable.c: sawTable[4] (its first half and")
w(" * the middle: the rest is the mirror, -saw[1024 - i]) and recTable[4] (and recTable[5][0], which their linear")
w(" * interpolation reads after its last point), unchanged.")
w(" */")
w("static const int16_t LX_SAW4[513] = {")
for j in range(0, 513, 16):
    w("    " + ", ".join(str(v) for v in saw[j:min(j + 16, 513)]) + ",")
w("};")
w("static const int16_t LX_REC4[1025] = {")
for j in range(0, 1025, 16):
    w("    " + ", ".join(str(v) for v in rec[j:j + 16]) + ",")
w("};")
open(WOUT, "w").write("\n".join(o) + "\n")
print("lxr_waves.h: saw (513) and square (1025) of band 4, %d bytes" % ((513 + 1025) * 2))
