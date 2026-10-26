#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Builds the LXR firmware's own drum voices (Erica Synths, GPL-3.0-only) on the Mac with the stubs in
# tests/lxr_ref/stub, renders the sounds of tests/lxr_ref/cases.h into build/lxr_ref (ref_lxr.c), then builds and
# runs tests/lxr_test.c, which plays the same sounds through KARMA's port (firmware/src/lxr_dsp.c) and compares.
#   sh tests/lxr_ref/build.sh            LXR: their checkout (default ~/fm1-firmware/ref/lxr)
#   (WAVDIR=dir: ours and theirs side by side, a stereo WAV per sound)
set -e
cd "$(dirname "$0")/../.."
LXR="${LXR:-$HOME/fm1-firmware/ref/lxr}"
D="$LXR/Core/Src/Soundengine/DSPAudio"
[ -f "$D/DrumVoice.c" ] || { echo "lxr_ref: no LXR firmware checkout in $LXR"; exit 1; }
OUT=build/lxr_ref
mkdir -p "$OUT/inc/Soundengine"
# their config.h at the FM-1's rate, without the STM32's memory sections
sed -e 's/^#define REAL_FS[[:space:]].*/#define REAL_FS (float)44100.f/' -e 's/^#define INDTCM[[:space:]].*/#define INDTCM/' \
    -e 's/^#define INDTCMZ[[:space:]].*/#define INDTCMZ/' -e 's/^#define IN_ITCM_RAM[[:space:]].*/#define IN_ITCM_RAM/' \
    "$LXR/Core/Inc/Soundengine/config.h" > "$OUT/inc/Soundengine/config.h"
cc -O2 -w -std=gnu99 -I"$OUT/inc" -Itests/lxr_ref/stub -Itests/lxr_ref -I"$LXR/Core/Inc" -o "$OUT/ref_lxr" \
    tests/lxr_ref/ref_lxr.c "$D/DrumVoice.c" "$D/Snare.c" "$D/HiHat.c" "$D/CymbalVoice.c" "$D/Oscillator.c" \
    "$D/transientGenerator.c" "$D/transientTables.c" "$D/SlopeEg2.c" "$D/Decay.c" "$D/snapEg.c" "$D/ResonantFilter.c" \
    "$D/distortion.c" "$D/1PoleLp.c" "$D/wavetable.c" "$D/random.c" -lm
"$OUT/ref_lxr" "$OUT"
mkdir -p build/gen build/host
[ build/gen/felucca_tables.h -nt tools/gen_tables.py ] || python3 tools/gen_tables.py build/gen/felucca_tables.h
cc -O2 -w -Ibuild/gen -Ifirmware/src -Itests/lxr_ref -o build/host/lxr_test tests/lxr_test.c -lm
build/host/lxr_test "$OUT" ${WAVDIR:+"$WAVDIR"}
