#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# build/host/libplx.a: firmware/mi (Plaits' models + plx.cc) for the host tests and the web simulator's C side.
#   sh tools/plx_host.sh [CXX]        (CXX: c++, or em++ for the simulator -> build/emu/libplx.a)
set -e
cd "$(dirname "$0")/.."
CXX="${1:-c++}"
OUT=build/host; [ "$CXX" = "em++" ] && OUT=build/emu
mkdir -p "$OUT/plx"
objs=""
for f in $(find firmware/mi -name '*.cc' | sort); do
    o="$OUT/plx/$(echo "$f" | tr / _ | sed 's/\.cc$/.o/')"
    if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ firmware/mi/plx.h -nt "$o" ]; then
        $CXX -O2 -w -std=c++11 -DTEST -ffp-contract=off -fno-exceptions -fno-rtti -Ifirmware/mi -c "$f" -o "$o"
    fi
    objs="$objs $o"
done
rm -f "$OUT/libplx.a"
if [ "$CXX" = "em++" ]; then emar rcs "$OUT/libplx.a" $objs; else ar rcs "$OUT/libplx.a" $objs; fi
echo "plx: $OUT/libplx.a"
