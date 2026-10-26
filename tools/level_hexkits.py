#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Measures the HEX kits' sounds (firmware/src/hex_drums.c) and sets each sound's LEVEL so that, within a kit, they
sit as tools/level_drumkits.py places the synthesised kits' (REL: dB against the kick, the loudest 100 ms, ITU-R
BS.1770 weighting). The kick keeps its LEVEL; a sound that would need more than 127 stays at 127 (and says so).

  cc -O2 -w -Ibuild/gen -Ifirmware/src -o build/host/hex_demo tests/hex_demo.c build/host/libplx.a -lc++ -lm
  build/host/hex_demo lanes build/host/hexlanes.raw && python3 tools/level_hexkits.py build/host/hexlanes.raw [--write]
Needs numpy and scipy."""
import re
import sys
from pathlib import Path

import numpy as np
from scipy.signal import lfilter

FS = 44100
N = FS * 2 // 32 * 32
SRC = Path(__file__).parent.parent / "firmware" / "src" / "hex_drums.c"
LANES = "KICK SNARE CLAP CHH OHH TOMLO TOMHI CRASH RIDE SHAKER CONGA RIM COWBELL CLAVE KICK2 SNARE2".split()
REL = dict(KICK=0, KICK2=0, SNARE=-1, SNARE2=-2.5, CLAP=-2, CHH=-13, OHH=-11, TOMLO=-3, TOMHI=-3.5,
           CRASH=-10, RIDE=-12, SHAKER=-15, CONGA=-5, RIM=-6, COWBELL=-6, CLAVE=-7)


def loudness(x):
    b1 = [1.53512485958697, -2.69169618940638, 1.19839281085285]
    a1 = [1, -1.69065929318241, 0.73248077421585]
    b2, a2 = [1, -2, 1], [1, -1.99004745483398, 0.99007225036621]
    k = lfilter(b2, a2, lfilter(b1, a1, x)) ** 2
    w = int(0.1 * FS)
    c = np.concatenate([[0], np.cumsum(k)])
    return 10 * np.log10(((c[w:] - c[:-w]) / w).max() + 1e-12) - 0.691


def main():
    raw = np.fromfile(sys.argv[1], dtype="<i4").astype(float) / 32768
    src = SRC.read_text()
    lines = [m for m in re.finditer(r"HS\((HM_\w+), [^)]*\)", src)]
    assert len(lines) == 80, len(lines)
    out = src
    for k in range(5):
        lv = [loudness(raw[(k * 16 + l) * N:(k * 16 + l + 1) * N]) for l in range(16)]
        print("kit %d: kick %.1f LUFS(100 ms)" % (k, lv[0]))
        for l in range(16):
            m = lines[k * 16 + l]
            f = [t.strip() for t in m.group(0)[3:-1].split(",")]
            level = int(f[7])
            want = lv[0] + REL[LANES[l]]
            new = int(round(level * 10 ** ((want - lv[l]) / 20)))
            pk = 20 * np.log10(np.abs(raw[(k * 16 + l) * N:(k * 16 + l + 1) * N]).max() + 1e-12)
            note = ""
            if new > 127:
                new, note = 127, " (short of %.1f dB)" % (20 * np.log10(level * 10 ** ((want - lv[l]) / 20) / 127))
            print("  %-8s %6.1f dB (want %6.1f)  peak %5.1f dBFS  level %3d -> %3d%s" % (LANES[l], lv[l], want, pk, level,
                                                                                      new, note))
            f[7] = str(new)
            out = out.replace(m.group(0), "HS(" + ", ".join(f) + ")", 1)
    if "--write" in sys.argv:
        SRC.write_text(out)
        print("written: " + str(SRC))


main()
