/* SPDX-License-Identifier: GPL-3.0-only */
/* Elements' bow and blow in fixed point (firmware/src/bow_dsp.c) against the float originals (tests/bow_ref, the
 * text files in build/bow_ref): her friction curve point by point, the bow's noise sample by sample (the same
 * random numbers), the bowed waveguides (they feed back through the friction, so the two drift apart with time:
 * the first second by its shape, the ten seconds by how loud they settle), the tube and the diffuser sample by
 * sample.
 *   sh tests/bow_ref/build.sh; cc -O1 -w -Ifirmware/src -o build/host/bow_test tests/bow_test.c -lm */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define EL_DIFF_D1 125                               /* her loops, at her 32 kHz */
#define EL_DIFF_D2 179
#define EL_DIFF_D3 268
#define EL_DIFF_D4 443
#include "strike_dsp.c"
#include "bow_dsp.c"

static uint32_t ref_seed;
static double ref_noise(void)
{
    ref_seed = ref_seed * 1664525u + 1013904223u;
    return (double)(ref_seed >> 16) / 65536.0 - 0.5;
}
static FILE *ref_open(const char *dir, const char *name, int c)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, name, dir, c);
    f = fopen(path, "r");
    if (!f) { printf("bow: no reference %s (tests/bow_ref/build.sh)\n", path); exit(2); }
    return f;
}
/* her samples x[] against ours y[]: the error's rms over hers, the two levels */
static double cmp(const double *x, const double *y, int n, double *rx, double *ry)
{
    double e = 0, a = 0, b = 0;
    int i;
    for (i = 0; i < n; i++) { e += (x[i] - y[i]) * (x[i] - y[i]); a += x[i] * x[i]; b += y[i] * y[i]; }
    *rx = sqrt(a / n);
    *ry = sqrt(b / n);
    return a > 0 ? sqrt(e / a) : sqrt(e / n);
}

int main(int argc, char **argv)
{
    const char *ref = argc > 1 ? argv[1] : "build/bow_ref";
    static double X[32000], Y[32000];
    int c, i, fails = 0;
    FILE *f;
    {                                                /* the friction curve */
        double x, v, o, worst = 0;
        f = ref_open(ref, "%s/bowtable.txt", 0);
        while (fscanf(f, "%lf %lf %lf", &x, &v, &o) == 3) {
            double e = fabs(el_bow_table((int32_t)lrint(x * 4194304.0), (int32_t)lrint(v * 65536.0)) / 4194304.0 - o);
            if (e > worst) worst = e;
        }
        fclose(f);
        printf("bow table     worst error %.6f  %s\n", worst, worst < 2e-4 ? "ok" : "FAIL");
        fails += !(worst < 2e-4);
    }
    {
        static const float P[3] = {0.7f, 0.3f, 0.95f};
        for (c = 0; c < 3; c++) {                    /* the bow's noise */
            el_flow_t s = {(uint32_t)(0x21 + c), 0};
            double sc = (double)P[c] * P[c] * P[c] * P[c], o, worst = 0;
            f = ref_open(ref, "%s/flow-%d.txt", c);
            for (i = 0; i < 8000 && fscanf(f, "%lf", &o) == 1; i++) {
                double e = fabs(el_flow_tick(&s, (int32_t)lrint(sc * 65536.0), (uint32_t)((0.0001 + sc * 0.125) * 4294967296.0)) / 65536.0 - o);
                if (e > worst) worst = e;
            }
            fclose(f);
            printf("flow %.2f      %d samples, worst error %.6f  %s\n", P[c], i, worst, i == 8000 && worst < 1e-4 ? "ok" : "FAIL");
            fails += !(i == 8000 && worst < 1e-4);
        }
    }
    for (c = 0; c < 6; c++) {                        /* the bowed waveguides */
        static el_bow_t E;
        el_bowg_t G[EL_BOW_N];
        int nw, k, n = 0, ok = 1, b;
        double vel, g, r, amp, e, rx, ry, pk_her[10], pk = 0;
        char line[128];
        memset(&E, 0, sizeof E);
        f = ref_open(ref, "%s/wg-%d.txt", c);
        fscanf(f, "%d %lf", &nw, &vel);
        for (k = 0; k < nw; k++) {
            int d;
            double h;
            fscanf(f, "%lf %lf %d %lf", &g, &r, &d, &amp);
            h = 1.0 / (1.0 + r * g + g * g);
            G[k].g = (int32_t)lrint(g * 268435456.0);
            G[k].r = (int32_t)lrint(r * 268435456.0);
            G[k].rpg = G[k].g + G[k].r;
            G[k].gh = (int32_t)lrint(g * h * 1073741824.0);
            G[k].w = (int32_t)lrint(amp * 8.0 * 16777216.0);
            G[k].delay = el_bow_delay((uint32_t)k, (uint32_t)d);
            if (G[k].delay != (uint32_t)d) ok = 0;   /* (the cases fit our shorter loops) */
        }
        b = 0;
        while (fgets(line, sizeof line, f))
            if (line[0] == 'p') { int at; sscanf(line + 1, "%d %lf", &at, &pk_her[b < 10 ? b++ : 9]); }
            else if (n < 32000 && sscanf(line, "%lf", &X[n]) == 1) n++;
        fclose(f);
        for (b = 0; b < 10; b++) {
            pk = 0;
            for (i = 0; i < 2000; i++) {
                int32_t in[16] = {0}, out[16] = {0};
                int j;
                el_bow_run(&E, G, (uint32_t)nw, in, (int32_t)lrint(vel * 65536.0), (int32_t)lrint(vel * 65536.0), out, 16);
                for (j = 0; j < 16; j++) {
                    double y = out[j] / 1048576.0;
                    if (b == 0) Y[i * 16 + j] = y;
                    if (fabs(y) > pk) pk = fabs(y);
                }
            }
            if (b == 0) e = cmp(X, Y, n, &rx, &ry);
        }
        {
            double q = pk_her[9] > 1e-4 ? pk / pk_her[9] : (pk < 1e-3 ? 1.0 : 99.0);
            int good = ok && n == 32000 && e < 0.15 && q > 0.7 && q < 1.4;
            printf("waveguides %d  first second: error %.3f (levels %.5f / %.5f); settled peak hers %.3f ours %.3f  %s\n",
                   c, e, rx, ry, pk_her[9], pk, good ? "ok" : "FAIL");
            fails += !good;
        }
    }
    {
        static const float T[5][4] = {{220.0f / 32000.0f, 0.5f, 0.5f, 0.25f}, {110.0f / 32000.0f, 0.9f, 1.0f, 0.5f},
            {523.0f / 32000.0f, 0.1f, 0.2f, 0.1f}, {40.0f / 32000.0f, 0.7f, 0.8f, 0.4f}, {10.0f / 32000.0f, 0.5f, 0.6f, 0.3f}};
        for (c = 0; c < 5; c++) {                    /* the tube */
            static el_tube_t t;
            el_tube_blk_t B;
            double e, rx, ry;
            int b, n = 0;
            memset(&t, 0, sizeof t);
            ref_seed = 12345u + (uint32_t)c;
            f = ref_open(ref, "%s/tube-%d.txt", c);
            while (n < 16000 && fscanf(f, "%lf", &X[n]) == 1) n++;
            fclose(f);
            for (b = 0; b < 1000; b++) {
                int32_t io[16], in[16];
                double env = b < 200 ? b / 200.0 : b < 700 ? 1.0 : (1000 - b) / 300.0;
                for (i = 0; i < 16; i++) in[i] = io[i] = (int32_t)lrint((double)(float)ref_noise() * 0.5 * 1048576.0);
                el_tube_block(&B, (uint32_t)((double)T[c][0] * 4294967296.0), (int32_t)(T[c][1] * 65536), (int32_t)(T[c][2] * 65536));
                el_tube_run(&t, &B, (int32_t)lrint(env * 65536.0), (int32_t)(T[c][3] * 65536), io, 16);
                for (i = 0; i < 16; i++) Y[b * 16 + i] = (io[i] - in[i]) / 1048576.0;
            }
            e = cmp(X, Y, n, &rx, &ry);
            printf("tube %d        error %.4f (levels %.4f / %.4f)  %s\n", c, e, rx, ry, n == 16000 && e < 0.03 ? "ok" : "FAIL");
            fails += !(n == 16000 && e < 0.03);
        }
    }
    {                                                /* the diffuser */
        static el_diff_t d;
        double e, rx, ry;
        int b, n = 0;
        ref_seed = 777u;
        f = ref_open(ref, "%s/diff.txt", 0);
        while (n < 8000 && fscanf(f, "%lf", &X[n]) == 1) n++;
        fclose(f);
        for (b = 0; b < 500; b++) {
            int32_t io[16];
            for (i = 0; i < 16; i++) io[i] = b < 200 ? (b == 0 && i == 0 ? 1048576 : 0) : (int32_t)lrint((double)(float)ref_noise() * 1048576.0);
            el_diff_run(&d, io, 16);
            for (i = 0; i < 16; i++) Y[b * 16 + i] = io[i] / 1048576.0;
        }
        e = cmp(X, Y, n, &rx, &ry);
        printf("diffuser      error %.4f (levels %.4f / %.4f)  %s\n", e, rx, ry, n == 8000 && e < 0.01 ? "ok" : "FAIL");
        fails += !(n == 8000 && e < 0.01);
    }
    printf(fails ? "bow: %d FAIL\n" : "bow: all ok\n", fails);
    return fails ? 1 : 0;
}
