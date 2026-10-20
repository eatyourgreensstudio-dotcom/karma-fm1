#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""firmware/mi/{plaits,elements}/resources.cc: only the tables of Plaits' and Elements' resources.cc (Emilie Gillet,
MIT) that the vendored sources in firmware/mi use (the full files hold tables, samples, of what is not built).
  tools/gen_plaits_resources.py [EURORACK]      (default ~/fm1-firmware/ref/eurorack)"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MI = ROOT / "firmware/mi"


def main():
    eur = Path(sys.argv[1] if len(sys.argv) > 1 else Path.home() / "fm1-firmware/ref/eurorack")
    for mod in ("plaits", "elements"):
        one(eur, mod)


# tables reached only through a table of tables, by index (elements/dsp/multistage_envelope.h: LUT_ENV_LINEAR + shape)
KEEP_ALSO = {"elements": {"lut_env_linear", "lut_env_expo", "lut_env_quartic"}}


def one(eur, mod):
    src = (eur / mod / "resources.cc").read_text()
    used = "\n".join(p.read_text() for p in (MI / mod).rglob("*") if p.suffix in (".h", ".cc") and p.name not in ("resources.h", "resources.cc"))
    used += "\n" + (MI / "plx.cc").read_text() + "\n" + "\n".join(p.read_text() for p in MI.glob("*.cc"))
    head = src[:src.index("namespace %s {" % mod) + len("namespace %s {" % mod)]
    body = src[len(head):]
    out, kept, dropped = [head], [], []
    defs = list(re.finditer(r"\n(const [^\n=]*?\b(\w+)\[\] = \{(.*?)\n\};)", body, re.S))
    direct = {m.group(2) for m in defs if re.search(r"\b%s\b" % m.group(2), used)} | KEEP_ALSO.get(mod, set())
    # a table of tables keeps only the entries in use (the others: 0); a table used only through one: kept
    for m in defs:
        name, text = m.group(2), m.group(1)
        if name not in direct:
            continue
        if "*" in text.split("=")[0]:
            def entry(e):
                n_ = e.group(2)
                return e.group(1) + (n_ if n_ in direct or not re.fullmatch(r"\w+", n_) else "0") + e.group(3)
            if mod != "plaits":                          # (Plaits' fm_patches_table: every bank is used)
                text = re.sub(r"(\n\s*)(\w+)(,)", entry, text)
            for n_ in re.findall(r"\b(\w+)\b", m.group(3)):
                if n_ in {d.group(2) for d in defs} and (mod == "plaits" or n_ in direct):
                    direct.add(n_)
    for m in defs:
        name = m.group(2)
        if name in direct:
            text = m.group(1)
            if "*" in text.split("=")[0] and mod != "plaits":
                text = re.sub(r"(\n\s*)(\w+)(,)", lambda e: e.group(1) + (e.group(2) if e.group(2) in direct else "0") + e.group(3), text)
            out.append(text); kept.append(name)
        else:
            dropped.append(name)
    # (a table used before its definition, e.g. a bank of patches listed after: defined first)
    out = [out[0]] + sorted(out[1:], key=lambda t: "*" in t.split("=")[0])
    out.append("\n}  // namespace %s\n" % mod)
    (MI / mod / "resources.cc").write_text("\n".join(out))
    print(f"{mod} resources: {len(kept)} tables kept, {len(dropped)} left out ({', '.join(dropped[:12])}{' ...' if len(dropped) > 12 else ''})")


if __name__ == "__main__":
    main()
