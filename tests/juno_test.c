/* SPDX-License-Identifier: GPL-3.0-only */
/* JUNO (firmware/src/juno_dsp.c) against FM-106's own code (github.com/PatttF/fm-106 firmware/src/fm106_osc.h,
 * fm106_vcf.h, fm106_chorus.h, in the reference checkout): the DCO, the filter (coefficients once a block here, per
 * sample there) and the chorus's left channel must give the same samples.
 *   cc -O1 -w -Ifirmware/src -I$FM106/firmware/src -o build/host/juno_test tests/juno_test.c -lm
 *      (FM106: ~/fm1-firmware/ref/fm-106) */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "fm106_tables.h"
#include "fm106_osc.h"
#include "fm106_vcf.h"
#include "fm106_chorus.h"
#include "juno_dsp.c"

static uint32_t rs = 1;
static uint32_t rnd(void) { rs = rs * 1664525u + 1013904223u; return rs >> 8; }

int main(void)
{
    int fails = 0, c, i;
    long n = 0;
    for (c = 0; c < 40; c++) {                     /* DCO, then the VCF on it, then the chorus */
        fm106_dco_t a;
        jn_dco_t b;
        fm106_vcf_t fa;
        jn_vcf_t fb;
        fm106_chorus_t ca;
        jn_chorus_t cb;
        int saw = c & 1, pulse = (c >> 1) & 1 || !saw, bad = 0;
        uint32_t sub = rnd() % 128u, noi = c % 3 ? 0 : rnd() % 128u, inc = 2000000u + rnd() % 400000000u;
        int32_t pw = (int32_t)(rnd() % 110u);
        uint32_t mode = (uint32_t)c % 4u;
        fm106_dco_init(&a, (uint32_t)c);
        a.saw_on = (uint8_t)saw; a.pulse_on = (uint8_t)pulse; a.sub_level = (uint8_t)sub; a.noise_level = (uint8_t)noi;
        memset(&b, 0, sizeof b);
        b.noise_seed = a.noise_seed;
        fm106_vcf_init(&fa);
        memset(&fb, 0, sizeof fb);
        fb.noise_seed = fa.noise_seed;
        fm106_chorus_init(&ca);
        ca.mode = (uint8_t)mode;
        memset(&cb, 0, sizeof cb);
        for (i = 0; i < 8192; i++) {
            static jn_coef_t k;
            static uint16_t dac;
            static uint8_t res;
            int32_t x, y, ya, yb, l, r;
            if (i % 32 == 0) {                         /* a new block: the cutoff and resonance move */
                dac = (uint16_t)(rnd() % 16384u);
                res = (uint8_t)(rnd() % 128u);
                jn_vcf_coef(&k, dac, res);
            }
            x = fm106_dco_process(&a, inc, (uint8_t)pw, 1, 0, 0);
            y = jn_dco(&b, inc, saw, pulse, pw, sub, noi);
            bad += x != y;
            ya = fm106_vcf_process(&fa, x, dac, res);
            yb = jn_vcf_run(&fb, &k, y);
            bad += ya != yb;
            fm106_chorus_process(&ca, ya, &l, &r);
            bad += l != jn_chorus(&cb, mode, yb);
            n++;
        }
        if (bad) {
            printf("juno: case %d (saw %d pulse %d sub %u noise %u pw %d chorus %u): %d samples differ FAIL\n", c, saw, pulse,
                   sub, noi, pw, mode, bad);
            fails++;
        }
    }
    {   /* the saturation's divides as multiplies: every input */
        int32_t x, bad = 0;
        for (x = -120000; x <= 120000; x++)
            bad += fm106_ota_sat(x) != jn_ota_sat(x);
        if (bad) {
            printf("juno: OTA saturation, %d inputs differ FAIL\n", bad);
            fails++;
        }
    }
    printf(fails ? "juno: FAILED\n" : "juno: DCO, VCF and chorus (L) as FM-106's, %ld samples x 3: all ok\n", n);
    return fails != 0;
}
