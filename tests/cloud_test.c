/* SPDX-License-Identifier: GPL-3.0-only */
/* Clouds' granular player in fixed point (firmware/src/cloud_dsp.c) against the float original (tests/cloud_ref,
 * the text files in build/cloud_ref: left and right, a sample a line). The same random numbers are asked in the
 * same order, so the same grains start at the same samples: the two outputs are compared sample by sample (they
 * differ by her Hermite reader against the port's straight line, and by the rounding). The levels must agree
 * to 2 % in every case.
 *   sh tests/cloud_ref/build.sh; cc -O1 -w -Ifirmware/src -o build/host/cloud_test tests/cloud_test.c -lm */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#define CL_READ_OFS 1
#include "cloud_dsp.c"

static int16_t ref_in(int i)
{
    double t = i / 32000.0;
    double v = 0.30 * sin(6.2831853 * 110.0 * t) + 0.22 * sin(6.2831853 * 277.0 * t + 1.0)
             + 0.15 * sin(6.2831853 * 523.0 * t + 2.0) * (0.5 + 0.5 * sin(6.2831853 * 1.7 * t));
    return (int16_t)lrint(v * 32767.0);
}

int main(int argc, char **argv)
{
    static const float C[6][7] = {
            {0.25f, 2048, 0, 0.625f, 0.75f, 0.5f, 0},   {0.0f, 1024, 12, 0.875f, 0.25f, 1.0f, 0},
        {0.75f, 8192, -12, 0.5f, 1.0f, 0.25f, 0}, {0.5f, 4096, 7, 0.75f, 0.625f, 0.75f, 1},
        {0.75f, 1500, -5, 0.375f, 0.0f, 0.0f, 1},  {0.25f, 6000, 19, 1.0f, 0.875f, 1.0f, 0},
    };
    static const int SIZE[6] = {2048, 1024, 8192, 4096, 1499, 5999};      /* (her table's values) */
    /* the last two: now and then one of her grains starts a sample (or a few blocks) from the port's, where her
     * floats round the other way; the same sound, but no longer the same samples: only roughly compared */
    static const double TOL[6] = {0.01, 0.01, 0.01, 0.01, 0.3, 0.3};
    const char *ref = argc > 1 ? argv[1] : "build/cloud_ref";
    int c, fails = 0;
    for (c = 0; c < 6; c++) {
        static int16_t ring[32768];
        static cl_t cl;
        cl_par_t p = {0};
        char path[512];
        FILE *f;
        double e = 0, a = 0, o = 0;
        int b, i, n = 0, grains = 0;
        uint32_t w = 0;
        snprintf(path, sizeof path, "%s/cloud-%d.txt", ref, c);
        f = fopen(path, "r");
        if (!f) { printf("cloud: no reference %s (tests/cloud_ref/build.sh)\n", path); return 2; }
        for (i = 0; i < 32768; i++) ring[i] = 0;
        cl_init(&cl, (uint32_t)(0x21 + c));
        p.position = (int32_t)(C[c][0] * 65536);
        p.size = SIZE[c];
        p.ratio = cl_ratio((int32_t)C[c][2]);
        p.overlap = (int32_t)(C[c][3] * 65536);
        p.window = (int32_t)(C[c][4] * 65536);
        p.spread = (int32_t)(C[c][5] * 65536);
        p.steady = C[c][6] != 0;
        for (b = 0; b < 3000; b++) {
            int32_t l[32] = {0}, r[32] = {0};
            for (i = 0; i < 32; i++) ring[w++ & 32767u] = ref_in(b * 32 + i);
            cl_play(&cl, &p, ring, 32767u, w, l, r, 32);
            { int q; for (q = 0; q < CL_NG; q++) grains += cl.g[q].on; }
            for (i = 0; i < 32; i++) {
                double xl, xr;
                if (fscanf(f, "%lf %lf", &xl, &xr) != 2) break;
                e += (xl - l[i] / 32768.0) * (xl - l[i] / 32768.0) + (xr - r[i] / 32768.0) * (xr - r[i] / 32768.0);
                a += xl * xl + xr * xr;
                o += (l[i] / 32768.0) * (l[i] / 32768.0) + (r[i] / 32768.0) * (r[i] / 32768.0);
                n++;
            }
        }
        fclose(f);
        e = a > 0 ? sqrt(e / a) : 1;
        printf("cloud %d  %d samples, %.1f grains at once, error %.4f (levels %.4f / %.4f)  %s\n", c, n, grains / 3000.0, e, sqrt(a / (2 * n)), sqrt(o / (2 * n)),
               n == 96000 && e < TOL[c] && fabs(sqrt(o / a) - 1.0) < 0.02 ? "ok" : "FAIL");
        fails += !(n == 96000 && e < TOL[c] && fabs(sqrt(o / a) - 1.0) < 0.02);
    }
    printf(fails ? "cloud: %d FAIL\n" : "cloud: all ok\n", fails);
    return fails ? 1 : 0;
}
