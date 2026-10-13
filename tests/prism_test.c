/* SPDX-License-Identifier: GPL-3.0-only */
/* The PRISM engine's models (firmware/src/prism_dsp.c, fixed point) against their float originals (Plaits, built by
 * tests/prism_ref/build.sh into build/prism_ref): the same notes and settings, the two outputs compared sample by
 * harmonic. Passes when the difference is small against the signal (in dB below the original's).
 *   sh tests/prism_ref/build.sh && cc -O1 -w -Ifirmware/src -o build/host/prism_test tests/prism_test.c -lm
 *   build/host/prism_test [REFDIR [WAVDIR]]     (WAVDIR: ours and hers side by side, to listen to) */
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include "prism_tables.h"
#include "prism_dsp.c"

#define N 44096                                     /* a second, in blocks of 32 */
static const float CASES[][4] = {                   /* f0, harmonics, timbre, morph: as tests/prism_ref/ref_fold.cc */
    {0.002f, 0.5f, 0.0f, 0.5f}, {0.002f, 0.5f, 0.5f, 0.5f}, {0.002f, 0.5f, 1.0f, 0.5f}, {0.005f, 0.0f, 0.6f, 0.5f},
    {0.005f, 1.0f, 0.6f, 0.5f}, {0.005f, 0.25f, 0.8f, 0.0f}, {0.005f, 0.75f, 0.8f, 1.0f}, {0.02f, 0.5f, 0.7f, 0.3f},
    {0.0005f, 0.9f, 1.0f, 0.9f}, {0.05f, 0.3f, 0.9f, 0.6f}};

static void wav(const char *path, const float *a, const float *b)   /* stereo: left ours, right hers */
{
    FILE *f = fopen(path, "wb");
    uint32_t h[11] = {0x46464952, 36 + N * 4, 0x45564157, 0x20746d66, 16, 0x00020001, 44100, 44100 * 4, 0x00100004, 0x61746164, N * 4};
    int i;
    if (!f) return;
    fwrite(h, 4, 11, f);
    for (i = 0; i < N; i++) {
        int16_t s[2] = {(int16_t)(a[i] * 24000), (int16_t)(b[i] * 24000)};
        fwrite(s, 2, 2, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *ref = argc > 1 ? argv[1] : "build/prism_ref";
    static float hers[N], ours[N];
    static int16_t o[32];
    int c, i, fails = 0;
    for (c = 0; c < (int)(sizeof CASES / sizeof CASES[0]); c++) {
        char path[512];
        pr_fold_t s;
        FILE *f;
        double es = 0, ed = 0, db;
        double f0 = CASES[c][0];
        uint32_t inc;
        snprintf(path, sizeof path, "%s/fold-%d.f0", ref, c);       /* (the frequency she plays for the note) */
        f = fopen(path, "r");
        if (f) {
            if (fscanf(f, "%lf", &f0) != 1) f0 = CASES[c][0];
            fclose(f);
        }
        inc = (uint32_t)(f0 * 4294967296.0);
        f0 = inc / 4294967296.0;
        snprintf(path, sizeof path, "%s/fold-%d.f32", ref, c);
        f = fopen(path, "rb");
        if (!f || fread(hers, sizeof(float), N, f) != N) {
            printf("prism: no reference %s (tests/prism_ref/build.sh)\n", path);
            return 2;
        }
        fclose(f);
        pr_fold_init(&s);
        for (i = 0; i < N; i += 32) {
            int k;
            pr_fold_render(&s, inc, (int32_t)(CASES[c][1] * 32767), (int32_t)(CASES[c][2] * 32767), (int32_t)(CASES[c][3] * 32767), o, 32);
            for (k = 0; k < 32; k++)
                ours[i + k] = o[k] / 32767.0f;
        }
        {   /* The two do not start in step (her oscillator glides to the note over its first block), so they are
             * compared as what they are, periodic: the harmonics of each (amplitude and phase, over whole cycles
             * of the second half), hers turned by the time shift that fits best */
            static double A[64][2], B[64][2];
            double w = 2 * 3.141592653589793 * f0, best = 1e300, bs = 0, sh, step;
            int k, K = (int)(0.5 / f0), len = N / 2, i0 = N - len, pass;
            K = K > 64 ? 64 : K;
            for (k = 1; k < K; k++) {
                double ar = 0, ai = 0, br = 0, bi = 0;
                for (i = 0; i < len; i++) {
                    double hann = 0.5 - 0.5 * cos(6.283185307179586 * i / len);   /* (not whole cycles: windowed) */
                    double cs = hann * cos(w * k * (i0 + i)), sn = hann * sin(w * k * (i0 + i));
                    ar += ours[i0 + i] * cs; ai -= ours[i0 + i] * sn;
                    br += hers[i0 + i] * cs; bi -= hers[i0 + i] * sn;
                }
                A[k][0] = ar; A[k][1] = ai; B[k][0] = br; B[k][1] = bi;
                es += br * br + bi * bi;
            }
            for (pass = 0, sh = 0, step = 6.283185307179586 / 4096; pass < 2; pass++) {   /* the shift that fits best */
                double from = pass ? bs - 2 * step : 0, to = pass ? bs + 2 * step : 6.283185307179586, st = pass ? step / 64 : step;
                for (sh = from; sh < to; sh += st) {
                    double e = 0;
                    for (k = 1; k < K; k++) {
                        double cr = cos(sh * k), ci = sin(sh * k), rr = B[k][0] * cr - B[k][1] * ci, ri = B[k][0] * ci + B[k][1] * cr;
                        e += (A[k][0] - rr) * (A[k][0] - rr) + (A[k][1] - ri) * (A[k][1] - ri);
                    }
                    if (e < best) { best = e; bs = sh; }
                }
            }
            ed = best;
        }
        db = 10 * log10(ed / (es + 1e-12) + 1e-12);
        printf("fold  f0 %.4f  harm %.2f  timbre %.2f  morph %.2f   difference %6.1f dB  %s\n", CASES[c][0], CASES[c][1],
               CASES[c][2], CASES[c][3], db, db < -30 ? "ok" : "FAIL");
        fails += !(db < -30);
        if (argc > 2) {
            snprintf(path, sizeof path, "%s/fold-%d.wav", argv[2], c);
            wav(path, ours, hers);
        }
    }
    printf(fails ? "prism: FAILED\n" : "prism: the fixed-point models match the originals\n");
    return fails != 0;
}
