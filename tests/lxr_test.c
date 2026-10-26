/* SPDX-License-Identifier: GPL-3.0-only */
/* KARMA's port of the LXR's drum voices (firmware/src/lxr_dsp.c) against the LXR firmware's own (Erica Synths), built
 * and rendered by tests/lxr_ref/build.sh into build/lxr_ref: the sounds of tests/lxr_ref/cases.h, a second each, in
 * blocks of 32 (as the drum track renders them; theirs in 64). Two runs: with their 12 transient samples whole (the
 * port's own error), and with the ones the firmware keeps (lxr_transients.h: trimmed, 3 replaced).
 * The measures, each sound:
 *   RES   the difference, sample by sample, against their signal: 10 log10(sum (ours - theirs)^2 / sum theirs^2)
 *   ENV   the level over the second, in 10 ms windows: the largest difference in dB (windows above -50 dB of the peak)
 *   SPEC  the spectrum of the first 0.37 s in third-octave bands, 40 Hz .. 16 kHz: the largest difference in dB
 *         (bands above -40 dB of the strongest)
 * Passes when every sound made of sines and noise matches sample for sample (RES under -40 dB) and every sound matches
 * in level and spectrum (ENV under 1.5 dB, SPEC under 3 dB): their saw, square, triangle and PWM come from band-
 * limited tables, ours are computed (polyBLEP), so those differ in the waveform's finer detail, not in pitch,
 * envelope or the spectrum's shape.
 *   build/host/lxr_test REFDIR [WAVDIR]     (WAVDIR: ours left, theirs right, a WAV per sound) */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "felucca_tables.h"
#include "cases.h"

#define lx_tr_of lx_tr_fw                         /* the firmware's transients, and theirs whole */
#include "lxr_transients.h"
#undef lx_tr_of
static int tr_full;
static int8_t THEIRS[12][2205];
static inline const int8_t *lx_tr_of(uint32_t i, uint32_t *len)
{
    if (tr_full) {
        *len = 2205;
        return THEIRS[i % 12u];
    }
    return lx_tr_fw(i, len);
}
#define LX_TR_EXTERNAL
#include "lxr_dsp.c"

#define NS (LXC_SECONDS * 44100 / 64 * 64)

static void par_of(const lxc_t *c, lx_par_t *p)
{
    p->kind = c->kind, p->wave = c->wave, p->coarse = c->coarse, p->fine = c->fine, p->mwave = c->mwave;
    p->mcoarse = c->mcoarse, p->mamt = c->mamt, p->m2wave = c->m2wave, p->m2coarse = c->m2coarse, p->m2amt = c->m2amt;
    p->mix = c->mix, p->noisef = c->noisef, p->atk = c->atk, p->dec = c->dec, p->slope = c->slope, p->repeat = c->repeat;
    p->pamt = c->pamt, p->pdec = c->pdec, p->pslope = c->pslope, p->twave = c->twave, p->tvol = c->tvol;
    p->tfreq = c->tfreq, p->ftype = c->ftype, p->fcut = c->fcut, p->fres = c->fres, p->fdrive = c->fdrive;
    p->dist = c->dist, p->decim = c->decim, p->vol = c->vol, p->pwm = c->pwm;
}
static void ours(const lxc_t *c, float *out)
{
    static lx_voice_t v;
    lx_par_t p;
    int16_t b[32];
    uint32_t i, k;
    memset(&v, 0, sizeof v);
    par_of(c, &p);
    lx_rng_state = 0x2545F491u;
    lx_trigger(&v, &p, 60, c->vel);
    for (i = 0; i < NS; i += 32) {
        lx_render(&v, &p, b, 32, 60);
        for (k = 0; k < 32; k++)
            out[i + k] = b[k];
    }
}

/* ---- an FFT (radix 2, in place) for the bands */
static void fft(float *re, float *im, uint32_t n)
{
    uint32_t i, j = 0, k, m;
    for (i = 1; i < n; i++) {
        uint32_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (m = 2; m <= n; m <<= 1) {
        double a = -2 * M_PI / m;
        for (k = 0; k < n; k += m)
            for (j = 0; j < m / 2; j++) {
                float wr = (float)cos(a * j), wi = (float)sin(a * j);
                float xr = re[k + j + m / 2] * wr - im[k + j + m / 2] * wi, xi = re[k + j + m / 2] * wi + im[k + j + m / 2] * wr;
                re[k + j + m / 2] = re[k + j] - xr, im[k + j + m / 2] = im[k + j] - xi;
                re[k + j] += xr, im[k + j] += xi;
            }
    }
}
#define NF 16384
#define NBANDS 27                                 /* third octaves from 40 Hz */
static void bands(const float *x, double *e)
{
    static float re[NF], im[NF];
    uint32_t i, b;
    for (i = 0; i < NF; i++)
        re[i] = x[i] * (float)(0.5 - 0.5 * cos(2 * M_PI * i / NF)), im[i] = 0;
    fft(re, im, NF);
    for (b = 0; b < NBANDS; b++) {
        double lo = 40 * pow(2, (b - 0.5) / 3), hi = 40 * pow(2, (b + 0.5) / 3);
        e[b] = 1e-9;
        for (i = (uint32_t)(lo * NF / 44100) + 1; i <= (uint32_t)(hi * NF / 44100) && i < NF / 2; i++)
            e[b] += (double)re[i] * re[i] + (double)im[i] * im[i];
    }
}
static void wav(const char *path, const float *a, const float *b)
{
    FILE *f = fopen(path, "wb");
    uint32_t h[11] = {0x46464952, 36 + NS * 4, 0x45564157, 0x20746d66, 16, 0x00020001, 44100, 44100 * 4, 0x00100004,
                      0x61746164, NS * 4};
    int i;
    if (!f)
        return;
    fwrite(h, 4, 11, f);
    for (i = 0; i < NS; i++) {
        int16_t s[2] = {(int16_t)a[i], (int16_t)b[i]};
        fwrite(s, 2, 2, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    static float a[NS], b[NS];
    static int16_t r[NS];
    const char *dir = argc > 1 ? argv[1] : "build/lxr_ref";
    char path[512];
    uint32_t i, k, fails = 0;
    FILE *f;
    snprintf(path, sizeof path, "%s/transients.bin", dir);
    if (!(f = fopen(path, "rb")) || fread(THEIRS, 1, sizeof THEIRS, f) != sizeof THEIRS) {
        printf("lxr_test: no %s (sh tests/lxr_ref/build.sh)\n", path);
        return 1;
    }
    fclose(f);
    printf("lxr_test: KARMA's port against the LXR firmware's voices (%u sounds, 1 s each)\n", (unsigned)LXC_N);
    printf("  %-17s %8s %7s %7s  | firmware transients: %8s %7s %7s\n", "sound", "RES dB", "ENV dB", "SPEC dB", "RES dB",
           "ENV dB", "SPEC dB");
    for (i = 0; i < LXC_N; i++) {
        const lxc_t *c = &LXC[i];
        double res[2], env[2], spec[2], ea[NBANDS], eb[NBANDS], top = 0, pk = 0;
        int smooth = c->wave == 0 || c->wave == 4, run;
        if (c->kind == 0 && !c->mix && c->mwave != 0 && c->mwave != 4 && c->mamt)
            smooth = 0;                           /* (a table wave modulating) */
        if (c->kind == 0 && c->mix && c->mwave != 0 && c->mwave != 4)
            smooth = 0;
        if (c->kind >= 2 && ((c->mwave != 0 && c->mwave != 4 && c->mamt) || (c->m2wave != 0 && c->m2wave != 4 && c->m2amt)))
            smooth = 0;
        snprintf(path, sizeof path, "%s/%02u.raw", dir, i);
        if (!(f = fopen(path, "rb")) || fread(r, 2, NS, f) != NS) {
            printf("lxr_test: no %s\n", path);
            return 1;
        }
        fclose(f);
        for (k = 0; k < NS; k++)
            b[k] = r[k];
        for (run = 0; run < 2; run++) {
            double d = 0, s = 0, wmax = 0;
            tr_full = run == 0;
            ours(c, a);
            for (k = 0; k < NS; k++)
                d += (double)(a[k] - b[k]) * (a[k] - b[k]), s += (double)b[k] * b[k];
            res[run] = 10 * log10((d + 1e-9) / (s + 1e-9));
            for (k = 0; k + 441 <= NS; k += 441) {   /* 10 ms windows */
                double ra = 0, rb = 0;
                uint32_t j;
                for (j = 0; j < 441; j++)
                    ra += (double)a[k + j] * a[k + j], rb += (double)b[k + j] * b[k + j];
                if (rb > pk)
                    pk = rb;
            }
            env[run] = 0;
            for (k = 0; k + 441 <= NS; k += 441) {
                double ra = 0, rb = 0, dd;
                uint32_t j;
                for (j = 0; j < 441; j++)
                    ra += (double)a[k + j] * a[k + j], rb += (double)b[k + j] * b[k + j];
                if (rb < pk * 1e-5)
                    continue;
                dd = fabs(10 * log10((ra + 1) / (rb + 1)));
                if (dd > wmax)
                    wmax = dd;
            }
            env[run] = wmax;
            bands(a, ea);
            bands(b, eb);
            top = 0;
            for (k = 0; k < NBANDS; k++)
                top = eb[k] > top ? eb[k] : top;
            spec[run] = 0;
            for (k = 0; k < NBANDS; k++)
                if (eb[k] > top * 1e-4) {
                    double dd = fabs(10 * log10(ea[k] / eb[k]));
                    spec[run] = dd > spec[run] ? dd : spec[run];
                }
            if (run == 0 && getenv("LXR_DUMP") && (uint32_t)atoi(getenv("LXR_DUMP")) == i)   /* (the first samples) */
                for (k = 0; k < 48u; k++)
                    printf("%u: %6.0f %6.0f\n", k, a[k], b[k]);
            if (run == 0 && argc > 2) {
                snprintf(path, sizeof path, "%s/lxr_%02u.wav", argv[2], i);
                wav(path, a, b);
            }
        }
        {
            int ok = env[0] < 1.5 && spec[0] < 3.0 && (!smooth || res[0] < -40.0);
            printf("  %-17s %8.1f %7.2f %7.2f  |                      %8.1f %7.2f %7.2f  %s%s\n", c->name, res[0], env[0],
                   spec[0], res[1], env[1], spec[1], ok ? "ok" : "FAIL", smooth ? "" : " (table waves: ENV, SPEC)");
            fails += !ok;
        }
    }
    printf(fails ? "lxr_test: %u sounds FAIL\n" : "lxr_test: all %u sounds match\n", fails ? fails : (unsigned)LXC_N);
    return fails != 0;
}
