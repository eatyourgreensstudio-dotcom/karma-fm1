#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# builds and runs the float references of the PRISM engine's models (see ref_fold.cc) into build/prism_ref
set -e
cd "$(dirname "$0")/../.."
EURORACK="${EURORACK:-$HOME/fm1-firmware/ref/eurorack}"
[ -f "$EURORACK/stmlib/dsp/dsp.h" ] || { echo "prism_ref: no eurorack checkout with stmlib in $EURORACK"; exit 1; }
mkdir -p build/prism_ref
c++ -O2 -w -std=c++11 -DTEST -I"$EURORACK" -o build/prism_ref/ref_fold tests/prism_ref/ref_fold.cc \
    "$EURORACK/plaits/dsp/engine/waveshaping_engine.cc" "$EURORACK/plaits/resources.cc" "$EURORACK/stmlib/dsp/units.cc"
build/prism_ref/ref_fold build/prism_ref
ls build/prism_ref | head -3
