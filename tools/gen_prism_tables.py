#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Writes firmware/src/prism_tables.h: the lookup tables of the PRISM engine's models, taken from
plaits/resources.cc of pichenettes/eurorack (MIT, Emilie Gillet) and made integer (the float ones in Q15).
Run once, when a model is added:  tools/gen_prism_tables.py [EURORACK]   (default ~/fm1-firmware/ref/eurorack)"""
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
EURORACK = Path(sys.argv[1]) if len(sys.argv) > 1 else Path.home() / "fm1-firmware" / "ref" / "eurorack"
# (their name, ours, how): "i16" as it is, "q15" a float table scaled by 32767 and clamped
TABLES = [("lut_ws_inverse_tan", "PR_WS_0", "i16"), ("lut_ws_inverse_sin", "PR_WS_1", "i16"), ("lut_ws_linear", "PR_WS_2", "i16"),
          ("lut_ws_bump", "PR_WS_3", "i16"), ("lut_ws_double_bump", "PR_WS_4", "i16"), ("lut_fold", "PR_FOLD", "q15")]


def table(text, name):
    m = re.search(r"const (?:int16_t|float) %s\[\] = \{(.*?)\};" % re.escape(name), text, re.S)
    return [float(x) for x in re.findall(r"-?\d+\.?\d*(?:e[-+]?\d+)?", m.group(1))]


def main():
    text = (EURORACK / "plaits" / "resources.cc").read_text()
    L = ["/* SPDX-License-Identifier: MIT",
         " * Copyright 2016 Emilie Gillet (emilie.o.gillet@gmail.com)",
         " * Lookup tables of Plaits (plaits/resources.cc of github.com/pichenettes/eurorack), the float ones in Q15.",
         " * Written by tools/gen_prism_tables.py; the licence text: LICENSES/MIT-plaits.txt. */"]
    total = 0
    for theirs, ours, how in TABLES:
        v = table(text, theirs)
        if how == "q15":
            v = [max(-32767, min(32767, round(x * 32767))) for x in v]
        v = [int(x) for x in v]
        total += 2 * len(v)
        L.append(f"static const int16_t {ours}[{len(v)}] = {{   /* {theirs} */")
        for i in range(0, len(v), 16):
            L.append("    " + ", ".join(str(x) for x in v[i:i + 16]) + ",")
        L.append("};")
    L.append("static const int16_t *const PR_WS[5] = {PR_WS_0, PR_WS_1, PR_WS_2, PR_WS_3, PR_WS_4};")
    # ours: half a raised cosine, (cos(pi x) + 1) / 2 for x = 0..1 (the swarm's grain window), Q15
    import math
    L.append("static const int16_t PR_HANN[257] = {   /* (cos(pi x) + 1) / 2, x = 0 .. 1 */")
    v = [round(32767 * (math.cos(math.pi * i / 256) + 1) / 2) for i in range(257)]
    for i in range(0, 257, 16):
        L.append("    " + ", ".join(str(x) for x in v[i:i + 16]) + ",")
    L.append("};")
    total += 514
    (SRC / "firmware" / "src" / "prism_tables.h").write_text("\n".join(L) + "\n")
    print(f"prism tables: {len(TABLES)}, {total} B")


if __name__ == "__main__":
    main()
