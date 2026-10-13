#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# builds and runs the float reference of the strike exciters (see ref_strike.cc) into build/strike_ref
set -e
cd "$(dirname "$0")/../.."
EURORACK="${EURORACK:-$HOME/fm1-firmware/ref/eurorack}"
[ -f "$EURORACK/stmlib/dsp/dsp.h" ] || { echo "strike_ref: no eurorack checkout with stmlib in $EURORACK"; exit 1; }
mkdir -p build/strike_ref
c++ -O2 -w -std=c++11 -DTEST -I"$EURORACK" -o build/strike_ref/ref_strike tests/strike_ref/ref_strike.cc \
    "$EURORACK/elements/dsp/exciter.cc" "$EURORACK/elements/resources.cc" "$EURORACK/stmlib/dsp/units.cc" \
    "$EURORACK/stmlib/utils/random.cc"
build/strike_ref/ref_strike build/strike_ref
