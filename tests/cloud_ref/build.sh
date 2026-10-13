#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# builds and runs the float reference of Clouds' granular player (see ref_cloud.cc) into build/cloud_ref
set -e
cd "$(dirname "$0")/../.."
EURORACK="${EURORACK:-$HOME/fm1-firmware/ref/eurorack}"
[ -f "$EURORACK/stmlib/dsp/dsp.h" ] || { echo "cloud_ref: no eurorack checkout with stmlib in $EURORACK"; exit 1; }
mkdir -p build/cloud_ref
c++ -O2 -w -std=c++11 -DTEST -I"$EURORACK" -o build/cloud_ref/ref_cloud tests/cloud_ref/ref_cloud.cc \
    "$EURORACK/clouds/resources.cc" "$EURORACK/stmlib/dsp/units.cc" "$EURORACK/stmlib/utils/random.cc"
build/cloud_ref/ref_cloud build/cloud_ref
