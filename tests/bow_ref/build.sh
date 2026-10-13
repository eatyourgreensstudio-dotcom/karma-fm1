#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# builds and runs the float reference of Elements' bow and blow (see ref_bow.cc) into build/bow_ref
set -e
cd "$(dirname "$0")/../.."
EURORACK="${EURORACK:-$HOME/fm1-firmware/ref/eurorack}"
[ -f "$EURORACK/stmlib/dsp/dsp.h" ] || { echo "bow_ref: no eurorack checkout with stmlib in $EURORACK"; exit 1; }
mkdir -p build/bow_ref
c++ -O2 -w -std=c++11 -DTEST -I"$EURORACK" -o build/bow_ref/ref_bow tests/bow_ref/ref_bow.cc \
    "$EURORACK/elements/dsp/exciter.cc" "$EURORACK/elements/dsp/resonator.cc" "$EURORACK/elements/dsp/tube.cc" \
    "$EURORACK/elements/resources.cc" "$EURORACK/stmlib/dsp/units.cc" "$EURORACK/stmlib/utils/random.cc"
build/bow_ref/ref_bow build/bow_ref
