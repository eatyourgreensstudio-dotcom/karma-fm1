#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""karma_pcost.py [OUT.json] [ENGINE:FIRST-LAST] [VARIANT] : where each ELEMENTS and PLAITS preset spends the FM-1's time, by stage (3.0.5).
Needs the measuring build (KARMA_PCOST=1 sh build.sh -> build/karma-pcost.fwsc) on the FM-1, transport stopped.
Each preset is picked by Program Change (track 2 = MIDI ch 2, track 3 = ch 3), a 4-note chord is held 2 s, and
the editor command 91 returns the time each stage took (dsp.c PC_*), cleared at each read. Printed as % of one
core (us per second / 10,000). Changes the sounds on tracks 2 and 3 of the project in memory (not the saved one).
Needs mido + python-rtmidi (~/fm1-firmware/venv)."""
import json
import sys
import time
import mido

STAGES = ["setup", "bank", "bowloop", "bownoise", "blow", "upsample", "out", "omin", "plaits", "plaits_out", "prism",
          "om_osc", "om_iir", "om_fir", "om_rest", "plx_init", "init_max"]
CHORD = [48, 55, 60, 64]


def port(names):
    for n in names:
        if "KARMA" in n or "FM-1" in n or "Felucca" in n:
            return n
    raise SystemExit("no FM-1 MIDI port")


def ask(i, o, cmd, args=()):
    for _ in i.iter_pending():
        pass
    o.send(mido.Message("sysex", data=[0x7D, 0x46, 0x4C, cmd, *args]))
    end = time.time() + 1.0
    while time.time() < end:
        for m in i.iter_pending():
            if m.type == "sysex" and len(m.data) > 4 and list(m.data[:4]) == [0x7D, 0x46, 0x4C, cmd]:
                b = list(m.data[4:])
                return [sum(b[k + j] << (7 * j) for j in range(5)) for k in range(0, len(b) - 4, 5)]
        time.sleep(0.005)
    return None


def pcost(i, o):
    v = ask(i, o, 91)
    if not v or len(v) < 2 * len(STAGES):
        raise SystemExit("no answer to command 91: is the measuring build (karma-pcost.fwsc) installed?")
    return {s: (v[2 * k], v[2 * k + 1]) for k, s in enumerate(STAGES)}, v[2 * len(STAGES)], v[2 * len(STAGES) + 1]


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "pcost.json"
    i = mido.open_input(port(mido.get_input_names()))
    o = mido.open_output(port(mido.get_output_names()))
    if len(sys.argv) > 3:                                # a stage's variant (editor command 92)
        print("variant", ask(i, o, 92, [int(sys.argv[3])]))
    pcost(i, o)
    res = []
    only = sys.argv[2] if len(sys.argv) > 2 else ""      # e.g. ELEMENTS:11-13 (one engine, those presets)
    for trk, count, eng in ((2, 14, "ELEMENTS"), (3, 58, "PLAITS")):
        ch = trk - 1
        rng = range(count)
        if only:
            if not only.startswith(eng + ":"):
                continue
            lo, _, hi = only.split(":")[1].partition("-")
            rng = range(int(lo), int(hi or lo) + 1)
        for p in rng:
            o.send(mido.Message("program_change", channel=ch, program=p))
            time.sleep(0.6)
            st0, _, _ = pcost(i, o)                       # (clear: the preset change, the old notes)
            late0 = None
            t0 = time.time()
            for n in CHORD:
                o.send(mido.Message("note_on", channel=ch, note=n, velocity=100))
            time.sleep(2.0)
            st, late, cpu = pcost(i, o)
            secs = time.time() - t0
            for n in CHORD:
                o.send(mido.Message("note_off", channel=ch, note=n, velocity=0))
            pct = {s: round(st[s][0] / secs / 10000.0, 2) for s in STAGES if st[s][0] and s not in ("plx_init", "init_max")}
            total = round(sum(pct.values()), 1)
            ini = st["plx_init"]
            res.append(dict(engine=eng, program=p, total_pct=total, stages=pct, late=late, cpu_q8=cpu,
                            init_us=ini[0] // max(1, ini[1]), init_n=ini[1], init_max_us=st["init_max"][0]))
            top = ", ".join(f"{s} {v:.1f}%" for s, v in sorted(pct.items(), key=lambda x: -x[1])[:4])
            print(f"{eng:8} {p:2}  {total:5.1f}% of a core   {top}" +
                  (f"   set-up {ini[0] // ini[1]} us x{ini[1]}, longest {st['init_max'][0]} us" if ini[1] else ""), flush=True)
            time.sleep(0.8)
    json.dump(res, open(out, "w"), indent=1)
    print("saved", out)


if __name__ == "__main__":
    main()
