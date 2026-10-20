#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""firmware/mi/plaits/resources.cc: only the tables of Plaits' resources.cc (Emilie Gillet, MIT) that the
vendored sources in firmware/mi use (the full file holds tables of the models this firmware does not build).
  tools/gen_plaits_resources.py [EURORACK]      (default ~/fm1-firmware/ref/eurorack)"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MI = ROOT / "firmware/mi"


def main():
    eur = Path(sys.argv[1] if len(sys.argv) > 1 else Path.home() / "fm1-firmware/ref/eurorack")
    src = (eur / "plaits/resources.cc").read_text()
    used = "\n".join(p.read_text() for p in MI.rglob("*") if p.suffix in (".h", ".cc") and p.name not in ("resources.h", "resources.cc"))
    head = src[:src.index("namespace plaits {") + len("namespace plaits {")]
    body = src[len(head):]
    out, kept, dropped = [head], [], []
    # each definition: `const T name[] = { ... };` (also `const T* const name[] = { ... };`)
    for m in re.finditer(r"\n(const [^\n=]*?\b(\w+)\[\] = \{.*?\n\};)", body, re.S):
        name = m.group(2)
        if re.search(r"\b%s\b" % name, used) or re.search(r"\b%s\b" % name, "\n".join(o for o in out[1:])):
            out.append(m.group(1)); kept.append(name)
        else:
            dropped.append(name)
    # (a table used only by another kept table, e.g. a bank of patches: a second pass)
    for m in re.finditer(r"\n(const [^\n=]*?\b(\w+)\[\] = \{.*?\n\};)", body, re.S):
        name = m.group(2)
        if name in dropped and re.search(r"\b%s\b" % name, "\n".join(out[1:])):
            out.insert(1, m.group(1)); kept.append(name); dropped.remove(name)
    out.append("\n}  // namespace plaits\n")
    (MI / "plaits/resources.cc").write_text("\n".join(out))
    print(f"plaits resources: {len(kept)} tables kept, {len(dropped)} left out ({', '.join(dropped[:12])}{' ...' if len(dropped) > 12 else ''})")


if __name__ == "__main__":
    main()
