/* SPDX-License-Identifier: GPL-3.0-only */
/* Elements' strike exciters in fixed point (firmware/src/strike_dsp.c) against the float originals (tests/strike_ref,
 * the text files in build/strike_ref, a line a hit: its sample and level). The same random numbers are asked in the same order,
 * so the same hits must fall: each one within a few samples and 1 % of hers.
 *   sh tests/strike_ref/build.sh; cc -O1 -w -Ifirmware/src -o build/host/strike_test tests/strike_test.c -lm */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "strike_dsp.c"

static const float P[4] = {0.1f, 0.5f, 0.8f, 0.95f};
int main(int argc, char **argv)
{
    const char *ref = argc > 1 ? argv[1] : "build/strike_ref";
    int c, fails = 0;
    for (c = 0; c < 4; c++) {                        /* bounce: 4 s held, at her 32 kHz (0.15 s = 4800 samples) */
        char path[512], line[64];
        el_bounce_t s = {(uint32_t)(0x21 + c), 0, 0, 0};
        FILE *f;
        int i, n = 0, bad = 0, missing = 0;
        double worst_t = 0, worst_a = 0;
        snprintf(path, sizeof path, "%s/bounce-%d.txt", ref, c);
        f = fopen(path, "r");
        if (!f) { printf("strike: no reference %s (tests/strike_ref/build.sh)\n", path); return 2; }
        el_bounce_start(&s);
        for (i = 0; i < 4 * 32000; i++) {
            int32_t h = el_bounce_tick(&s, (int32_t)(P[c] * 65536), 4800);
            if (h) {
                int t; double a;
                if (!fgets(line, sizeof line, f) || sscanf(line, "%d %lf", &t, &a) != 2) {
                    missing += h > 4;                /* (hers lists no hit once its level is 0 in float: -90 dB) */
                    continue;
                }
                n++;
                if (fabs((double)(i - t)) > worst_t) worst_t = fabs((double)(i - t));
                if (fabs(h / 65536.0 - a) > worst_a) worst_a = fabs(h / 65536.0 - a);
                bad += fabs((double)(i - t)) > 3 + 0.002 * t || fabs(h / 65536.0 - a) > 0.01;
            }
        }
        while (fgets(line, sizeof line, f)) missing++;
        fclose(f);
        printf("bounce  lasts %.2f   %3d hits, as hers; the furthest %2.0f samples and %.4f of level off   %s\n", P[c], n, worst_t, worst_a,
               !bad && !missing ? "ok" : "FAIL");
        fails += bad || missing;
    }
    for (c = 0; c < 4; c++) {                        /* pick: the catch, the pluck, the muting */
        char path[512], line[64];
        el_pick_t s = {0, 0, 0};
        FILE *f;
        int i, n = 0, bad = 0;
        double worst_d = 0;
        snprintf(path, sizeof path, "%s/pick-%d.txt", ref, c);
        f = fopen(path, "r");
        if (!f) { printf("strike: no reference %s\n", path); return 2; }
        el_pick_start(&s, (int32_t)(P[c] * 65536), (int32_t)(0.25f * c * 65536), 65536);
        for (i = 0; i < 8000; i++) {
            int32_t h = el_pick_tick(&s);
            long pos = ftell(f);
            int t; double a;
            if (!fgets(line, sizeof line, f)) break;
            if (line[0] == 'd' && sscanf(line + 1, "%d %lf", &t, &a) == 2 && t == i) {   /* her muting at this sample */
                if (fabs(s.damp / 65536.0 - a) > worst_d) worst_d = fabs(s.damp / 65536.0 - a);
                if (h) bad++;
                continue;
            }
            if (line[0] != 'd' && sscanf(line, "%d %lf", &t, &a) == 2 && t == i) {      /* a pulse of hers here */
                n++;
                bad += !h || fabs(h / 65536.0 - a) > 0.002;
                continue;
            }
            fseek(f, pos, SEEK_SET);                 /* (not this sample's line) */
            bad += h != 0;
        }
        fclose(f);
        printf("pick    bend %.2f catch %.2f   %d pulses at her samples and levels, the muting within %.4f   %s\n", P[c], 0.25f * c, n, worst_d,
               !bad && n == 2 && worst_d < 0.01 ? "ok" : "FAIL");
        fails += bad || n != 2 || !(worst_d < 0.01);
    }
    printf(fails ? "strike: FAILED\n" : "strike: the fixed-point exciters match the originals\n");
    return fails != 0;
}
