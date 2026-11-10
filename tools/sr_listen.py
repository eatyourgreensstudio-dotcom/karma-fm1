#!/usr/bin/env python3
"""A minimal screen-reader client (fm1-reader protocol, PROTOCOL.md): HELLO every 2 s, WHERE once, print what the
FM-1 says.   sr_listen.py SECONDS [--where]"""
import sys, time, mido
secs = float(sys.argv[1]) if len(sys.argv) > 1 else 8
pin = next(n for n in mido.get_input_names() if "FM-1" in n)
pout = next(n for n in mido.get_output_names() if "FM-1" in n)
HELLO = [0x7D, 0x53, 0x52, 1, 1]; WHERE = [0x7D, 0x53, 0x52, 3]; BYE = [0x7D, 0x53, 0x52, 2]
with mido.open_input(pin) as i, mido.open_output(pout) as o:
    t0 = last = time.time(); o.send(mido.Message("sysex", data=HELLO)); asked = False
    while time.time() - t0 < secs:
        if time.time() - last > 2: o.send(mido.Message("sysex", data=HELLO)); last = time.time()
        for m in i.iter_pending():
            if m.type != "sysex" or list(m.data[:3]) != [0x7D, 0x53, 0x52]: continue
            d = list(m.data)
            if d[3] == 1:
                name, _, link = bytes(d[5:]).partition(b"\0")
                print(f"{time.time()-t0:5.2f} HELLO name={name.decode()!r} words={link.decode()!r}")
                if "--where" in sys.argv and not asked: time.sleep(0.05); o.send(mido.Message("sysex", data=WHERE)); asked = True
            elif d[3] == 16:
                print(f"{time.time()-t0:5.2f} SAY{' now' if d[4] & 1 else ''}{' keep' if d[4] & 2 else ''}: {bytes(d[5:]).decode()}")
        time.sleep(0.01)
    o.send(mido.Message("sysex", data=BYE))
