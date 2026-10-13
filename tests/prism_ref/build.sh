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
c++ -O2 -w -std=c++11 -DTEST -I"$EURORACK" -o build/prism_ref/ref_swarm tests/prism_ref/ref_swarm.cc \
    "$EURORACK/plaits/dsp/engine/swarm_engine.cc" "$EURORACK/plaits/resources.cc" "$EURORACK/stmlib/dsp/units.cc" \
    "$EURORACK/stmlib/utils/random.cc"
build/prism_ref/ref_swarm build/prism_ref
c++ -O2 -w -std=c++11 -DTEST -I"$EURORACK" -o build/prism_ref/ref_wave tests/prism_ref/ref_wave.cc \
    "$EURORACK/plaits/dsp/engine/wavetable_engine.cc" "$EURORACK/plaits/resources.cc" "$EURORACK/stmlib/dsp/units.cc"
build/prism_ref/ref_wave build/prism_ref
c++ -O2 -w -std=c++11 -DTEST -I"$EURORACK" -o build/prism_ref/ref_harm tests/prism_ref/ref_harm.cc \
    "$EURORACK/plaits/dsp/engine/additive_engine.cc" "$EURORACK/plaits/resources.cc" "$EURORACK/stmlib/dsp/units.cc"
build/prism_ref/ref_harm build/prism_ref
ls build/prism_ref | head -3
