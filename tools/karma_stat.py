#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""karma_stat.py [SECONDS] : the FM-1's load and USB over MIDI SysEx (editor command 90, KARMA 3.0.4), once a second,
in any USB AUDIO mode (USB STEMS has no console). Per line: dropouts (late halves) and notes cut since the last line,
the CPU, the worst half, both cores, each track's peak cost, the USB audio glitches, the BPM. Needs mido + python-rtmidi.
  karma_stat.py 120 > log.txt"""
import sys
import time
import mido

FIELDS = ["ms", "halves", "late", "max_us", "last_us", "cpu_q8", "shed", "kills", "playing", "bpm",
          "core1", "c1_faults", "c1_timeouts", "c1_mask", "pk1", "pk2", "pk3", "pkd",
          "stems", "underruns", "overruns", "missed", "stalls", "nested", "t5_us", "worst_blk", "midi_rx"]


def port(names):
    for n in names:
        if "KARMA" in n or "FM-1" in n or "Felucca" in n:
            return n
    raise SystemExit("no FM-1 MIDI port")


def stat(i, o):
    o.send(mido.Message("sysex", data=[0x7D, 0x46, 0x4C, 90]))
    end = time.time() + 1.0
    while time.time() < end:
        for m in i.iter_pending():
            if m.type == "sysex" and len(m.data) > 4 and list(m.data[:4]) == [0x7D, 0x46, 0x4C, 90]:
                b = list(m.data[4:])
                vals = [sum(b[k + j] << (7 * j) for j in range(5)) for k in range(0, len(b) - 4, 5)]
                return dict(zip(FIELDS, vals))
        time.sleep(0.005)
    return None


def main():
    secs = float(sys.argv[1]) if len(sys.argv) > 1 else 60.0
    i = mido.open_input(port(mido.get_input_names()))
    o = mido.open_output(port(mido.get_output_names()))
    prev = None
    t0 = time.time()
    while time.time() - t0 < secs:
        s = stat(i, o)
        if s is None:
            print(f"{time.time() - t0:6.1f}s no answer", flush=True)
        elif prev:
            d = lambda k: s[k] - prev[k]
            print(f"{time.time() - t0:6.1f}s play={s['playing']} bpm={s['bpm']} cpu={s['cpu_q8'] * 100 // 256}% "
                  f"late+={d('late')} cut+={d('shed')} last_us={s['last_us']} max_us={s['max_us']} worst_blk={s['worst_blk']} "
                  f"core1={'on' if s['core1'] & 1 else 'OFF'}{' BARRED' if s['core1'] & 2 else ''} mask={s['c1_mask']} "
                  f"pk={s['pk1']},{s['pk2']},{s['pk3']},{s['pkd']} stems={s['stems']} usb_under+={d('underruns')} "
                  f"usb_over+={d('overruns')} usb_missed+={d('missed')} t5_nested_us={s['t5_us']}", flush=True)
        prev = s or prev
        time.sleep(max(0.0, 1.0 - 0.05))


if __name__ == "__main__":
    main()
