#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare ELEMENTS' BOW and BLOW at the full rate (DIR0) and at half the rate (DIR1): tests/elem_half_test.c's
renders, notes 36..84, 1.5 s held + 1 s release each. Per note: the pitch (autocorrelation), the level held (dB),
the brightness (spectral centroid, Hz, and the energy above 11 kHz), the release (time to -40 dB).
  elem_half_compare.py DIR0 DIR1"""
import sys, glob, os
import numpy as np

FS = 44100
NOTES = [36, 48, 60, 72, 84]
SEG = (5 * (FS // 32) // 2) * 32          # samples per note (the harness's blocks)
HOLD = (3 * (FS // 32) // 2) * 32


def pitch(x):
    x = x - x.mean()
    n = len(x)
    f = np.fft.rfft(x * np.hanning(n), 2 * n)
    ac = np.fft.irfft(np.abs(f) ** 2)[:n]
    lo, hi = int(FS / 2000), int(FS / 30)
    k = lo + int(np.argmax(ac[lo:hi]))
    if 0 < k < len(ac) - 1:                       # parabolic refinement
        a, b, c = ac[k - 1], ac[k], ac[k + 1]
        k = k + 0.5 * (a - c) / (a - 2 * b + c)
    return FS / k


def db(v):
    return 20 * np.log10(max(v, 1e-9))


def stats(x):
    hold = x[HOLD // 3: HOLD]
    rel = x[HOLD:]
    rms = np.sqrt(np.mean(hold.astype(np.float64) ** 2))
    sp = np.abs(np.fft.rfft(hold * np.hanning(len(hold))))
    fr = np.fft.rfftfreq(len(hold), 1 / FS)
    cen = float((sp * fr).sum() / max(sp.sum(), 1e-9))
    hi = float((sp[fr > 11000] ** 2).sum() / max((sp ** 2).sum(), 1e-9))
    env = np.sqrt(np.convolve(rel.astype(np.float64) ** 2, np.ones(441) / 441, "same"))
    below = np.where(env < rms * 0.01)[0]
    t40 = below[0] / FS if len(below) else len(rel) / FS
    return pitch(hold[: 16384].astype(np.float64)), db(rms), cen, hi, t40


def main():
    d0, d1 = sys.argv[1], sys.argv[2]
    worst = {"cents": 0, "db": 0}
    for p0 in sorted(glob.glob(os.path.join(d0, "elem-*.raw"))):
        name = os.path.basename(p0)[5:-4].replace("_", " ")
        a = np.fromfile(p0, dtype=np.int32).astype(np.float64)
        b = np.fromfile(os.path.join(d1, os.path.basename(p0)), dtype=np.int32).astype(np.float64)
        print(f"{name}")
        for j, n in enumerate(NOTES):
            sa, sb = stats(a[j * SEG:(j + 1) * SEG]), stats(b[j * SEG:(j + 1) * SEG])
            cents = 1200 * np.log2(sb[0] / sa[0])
            ddb = sb[1] - sa[1]
            worst["cents"] = max(worst["cents"], abs(cents) if abs(cents) < 600 else 0)
            worst["db"] = max(worst["db"], abs(ddb))
            print(f"  note {n:2}: pitch {sa[0]:7.1f} / {sb[0]:7.1f} Hz ({cents:+6.1f} c)  level {sa[1]:6.1f} / {sb[1]:6.1f} dB ({ddb:+5.1f})"
                  f"  centroid {sa[2]:5.0f} / {sb[2]:5.0f} Hz  >11k {100*sa[3]:4.1f} / {100*sb[3]:4.1f} %  release {sa[4]:4.2f} / {sb[4]:4.2f} s")
    print(f"worst: {worst['cents']:.1f} cents (octave errors left out), {worst['db']:.1f} dB")


main()
