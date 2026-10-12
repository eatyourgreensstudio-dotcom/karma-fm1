#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Clips of the screens for posting: tests/op_video.c renders them, ffmpeg makes 1080 x 1080 H.264 / AAC files
# (the screen 4x, sharp pixels, on a dark ground): one per screen, and fm1-all.mp4, the three in one take.   sh tools/op_video.sh [OUTDIR]   (default ../videos)
set -e
cd "$(dirname "$0")/.."
OUT="${1:-../videos}"
mkdir -p build/video build/host "$OUT"
cc -O2 -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal -Itests -o build/host/op_video tests/op_video.c -lm
build/host/op_video build/video
for n in home drums bass all; do
    [ "$n" = all ] && END=23.55 || END=7.82
    ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgb24 -s 240x240 -framerate 44100/704 -i "build/video/$n.rgb" \
        -f s16le -ar 44100 -ac 2 -i "build/video/$n.pcm" \
        -vf "scale=960:960:flags=neighbor,pad=1080:1080:60:60:color=0x0c0c0e,fps=60" \
        -af "afade=t=in:d=0.005,afade=t=out:st=$END:d=0.05" \
        -c:v libx264 -preset slow -crf 14 -pix_fmt yuv420p -c:a aac -b:a 256k -movflags +faststart -shortest "$OUT/fm1-$n.mp4"
    rm -f "build/video/$n.rgb" "build/video/$n.pcm"
done
ls -la "$OUT"
