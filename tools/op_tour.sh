#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# A tour of the screens in one take, for posting: tests/op_tour.c renders it, ffmpeg makes fm1-tour.mp4 (1080 x 1080)
# and fm1-tour-reel.mp4 (1080 x 1920), H.264 / AAC, the screen 4x with sharp pixels on a dark ground.
#   sh tools/op_tour.sh [OUTDIR [PATTERN]]   (default ../videos, tests/op_tour_pattern.fm1p: a SAVE PATTERN file of the simulator)
#   TAKE=op_build sh tools/op_tour.sh   the second take (tests/op_build.c: from scratch), fm1-build{,-reel}.mp4
set -e
cd "$(dirname "$0")/.."
OUT="${1:-../videos}"
mkdir -p build/video build/host "$OUT"
sh tools/plx_host.sh >/dev/null
TAKE="${TAKE:-op_tour}"
PAT="${2:-tests/${TAKE}_pattern.fm1p}"
NAME=$( [ "$TAKE" = op_tour ] && echo fm1-tour || echo fm1-build )
cc -O2 -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal -Itests -o "build/host/$TAKE" "tests/$TAKE.c" build/host/libplx.a -lc++ -lm
DUR=$("build/host/$TAKE" build/video "$PAT" | sed -n "s/.* frames, \([0-9.]*\) s/\1/p")
FADE=$(echo "$DUR - 0.6" | bc)
for v in "1080:60:60:$NAME" "1920:60:480:$NAME-reel"; do
    H=${v%%:*}; R=${v#*:}; X=${R%%:*}; R=${R#*:}; Y=${R%%:*}; N=${R#*:}
    ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgb24 -s 240x240 -framerate 44100/704 -i build/video/tour.rgb \
        -f s16le -ar 44100 -ac 2 -i build/video/tour.pcm \
        -vf "scale=960:960:flags=neighbor,pad=1080:$H:$X:$Y:color=0x0c0c0e,fps=60" \
        -af "afade=t=in:d=0.005,afade=t=out:st=$FADE:d=0.5" \
        -c:v libx264 -preset slow -crf 14 -pix_fmt yuv420p -c:a aac -b:a 256k -movflags +faststart -shortest "$OUT/$N.mp4"
done
rm -f build/video/tour.rgb build/video/tour.pcm
ls -la "$OUT"/$NAME*
