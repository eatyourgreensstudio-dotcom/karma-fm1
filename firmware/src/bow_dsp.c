/* SPDX-License-Identifier: MIT
 * Copyright 2014 Emilie Gillet (emilie.o.gillet@gmail.com)
 * Ported to fixed-point C, 2026, for the PHYS engine (eng_phys.c).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
/* Bow and blow of Elements (elements/dsp/resonator.cc, exciter.cc, tube.cc, fx/diffuser.h of
 * github.com/pichenettes/eurorack) in fixed point, the pieces the PHYS engine (phys_dsp.c, eng_phys.c) puts on its
 * modal bank. Needs <stdint.h> and el_word of strike_dsp.c only: tests/bow_test.c builds it alone against the float
 * originals (tests/bow_ref).
 *
 *   bow     Resonator::BowTable and her "banded waveguides": each of the body's first modes also runs round a loop
 *           (a delay one period long and a narrow band-pass), and the bow's friction pushes on all of them at once
 *           by what they add up to: it grips while the body moves with it and lets go when it does not, so the
 *           body sings by itself for as long as the bow presses
 *   flow    ProcessFlow: the bow's noise: a level that flips at random, with white noise leaking in
 *   tube    Tube: a pipe with a reed: the breath's noise goes round a delay one period long
 *   diffuse Diffuser: four all-pass loops that smear the breath before it reaches the body
 *
 * Signals: Q22 in the waveguides (1.0 = 4194304), Q20 in the tube and the diffuser, levels Q16. The delay memory
 * is 16 bit. No signed 64-bit division. */

/* ---------------------------------------------------------------- bow --- */
#define EL_BOW_N 8                                    /* her kMaxBowedModes */
#define EL_BOW_MEM 3328                               /* 1024 + 2 x 512 + 5 x 256 (hers: 8 x 1024 floats) */
static const uint16_t EL_BOW_AT[EL_BOW_N + 1] = {0, 1024, 1536, 2048, 2304, 2560, 2816, 3072, EL_BOW_MEM};

typedef struct {
    int32_t s[EL_BOW_N][2];                           /* the band-passes, Q22 */
    int32_t push;                                     /* the friction's push (her bow_signal_), Q22 */
    uint32_t wp;
    int16_t line[EL_BOW_MEM];                         /* Q16: +-0.5 (hers settle near 0.15) */
} el_bow_t;
typedef struct {                                      /* a waveguide's block: the band-pass as phys_dsp.c's px_svf_t */
    int32_t g, rpg, gh;                               /* (Q28, Q28, Q30), */
    int32_t r;                                        /* 1 / q, Q28 */
    int32_t w;                                        /* its share of the sound (her amplitude x 8), Q24 */
    uint32_t delay;                                   /* samples */
} el_bowg_t;

/* the delay of waveguide i for a period (samples): halved until it fits, as hers (she has 1024 for each) */
static inline uint32_t el_bow_delay(uint32_t i, uint32_t period)
{
    uint32_t size = (uint32_t)(EL_BOW_AT[i + 1] - EL_BOW_AT[i]);
    while (period >= size)
        period >>= 1;
    return period;
}

/* BowTable: the push of the bow (pressing at velocity, Q16) on a body moving at x (Q22) */
static inline int32_t el_bow_table(int32_t x, int32_t velocity)
{
    int32_t b, f;
    x = (int32_t)(((int64_t)velocity * 34079) >> 12) - x;   /* 0.13 velocity - x */
    b = (x < 0 ? -x : x) >> 6;                        /* Q16 */
    if (b > 43690)                                    /* (beyond 2 / 3: the floor below anyway) */
        return (int32_t)(((int64_t)x * 2621) >> 20);
    b = b * 6 + 49152;                                /* |6 x| + 0.75 */
    b = (int32_t)(((int64_t)b * b) >> 16);
    b = (int32_t)(((int64_t)b * b) >> 16);            /* ^4, at most 256 */
    f = (int32_t)(0x10000000u / (uint32_t)((b >> 6) + 1));   /* 0.25 / it, Q20 */
    if (f < 2621)
        f = 2621;                                     /* 0.0025 */
    if (f > 256901)
        f = 256901;                                   /* 0.245 */
    return (int32_t)(((int64_t)x * f) >> 20);
}

/* n samples of the nw waveguides: in[] what else excites the body (Q22), v0 -> v1 the bow's pressure over the
 * block (Q16); what they sound is added to out[] (Q20) */
static void el_bow_run(el_bow_t *E, const el_bowg_t *G, uint32_t nw, const int32_t *in, int32_t v0, int32_t v1,
                       int32_t *out, uint32_t n)
{
#if defined(EL_BOW_FIXED)
    uint32_t i, k, wp = E->wp;
    int32_t push = E->push, dv = n ? (v1 - v0) / (int32_t)n : 0;
    for (i = 0; i < n; i++) {
        int32_t x = in[i] + push, sum = 0;
        int64_t acc = 0;
        for (k = 0; k < nw; k++) {
            const el_bowg_t *c = &G[k];
            uint32_t base = EL_BOW_AT[k], mask = (uint32_t)(EL_BOW_AT[k + 1] - EL_BOW_AT[k]) - 1u;
            int32_t s = (E->line[base + ((wp + c->delay) & mask)] * 16220) >> 8;   /* 0.99 of it, Q16 -> Q22 */
            int32_t s1 = E->s[k][0], s2 = E->s[k][1], t, v;
            sum += s;
            t = (int32_t)(((int64_t)c->gh * (x + s - (int32_t)(((int64_t)c->rpg * s1) >> 28) - s2)) >> 30);
            v = s1 + t;
            E->s[k][0] = v + t;
            t = (int32_t)(((int64_t)c->g * v) >> 28);
            E->s[k][1] = s2 + t + t;
            s = (int32_t)(((int64_t)v * c->r) >> 28);     /* the band-pass at its peak: 1 */
            acc += (int64_t)s * c->w;
            s = (s + 32) >> 6;
            E->line[base + (wp & mask)] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
        }
        wp--;
        v0 += dv;
        push = el_bow_table(sum, v0);
        out[i] += (int32_t)(acc >> 26);
    }
    for (k = 0; k < nw; k++) {                        /* (a wild state cannot overflow the sums above) */
        E->s[k][0] = E->s[k][0] > (1 << 28) ? (1 << 28) : E->s[k][0] < -(1 << 28) ? -(1 << 28) : E->s[k][0];
        E->s[k][1] = E->s[k][1] > (1 << 28) ? (1 << 28) : E->s[k][1] < -(1 << 28) ? -(1 << 28) : E->s[k][1];
    }
    E->wp = wp;
    E->push = push;
#else
    /* in float (2026-10-28: the FM-1's FPU; 64-bit integer multiplies made BOW STRING overload a busy section): the
     * band-passes' states in Q22 units as floats for the block, back to integers after it; the lines stay 16 bits */
    uint32_t i, k, wp = E->wp;
    int32_t push = E->push, dv = n ? (v1 - v0) / (int32_t)n : 0;
    float fg[EL_BOW_N], frpg[EL_BOW_N], fgh[EL_BOW_N], fr[EL_BOW_N], fw[EL_BOW_N], s1[EL_BOW_N], s2[EL_BOW_N];
    for (k = 0; k < nw; k++) {
        fg[k] = (float)G[k].g * (1.0f / 268435456.0f);
        frpg[k] = (float)G[k].rpg * (1.0f / 268435456.0f);
        fgh[k] = (float)G[k].gh * (1.0f / 1073741824.0f);
        fr[k] = (float)G[k].r * (1.0f / 268435456.0f);
        fw[k] = (float)G[k].w * (1.0f / 67108864.0f);   /* (Q24, the sum >> 26) */
        s1[k] = (float)E->s[k][0];
        s2[k] = (float)E->s[k][1];
    }
    for (i = 0; i < n; i++) {
        int32_t sum = 0;
        float x = (float)(in[i] + push), acc = 0.0f;
        for (k = 0; k < nw; k++) {
            uint32_t base = EL_BOW_AT[k], mask = (uint32_t)(EL_BOW_AT[k + 1] - EL_BOW_AT[k]) - 1u;
            int32_t si = (E->line[base + ((wp + G[k].delay) & mask)] * 16220) >> 8, o;   /* 0.99 of it, Q16 -> Q22 */
            float t, v, y;
            sum += si;
            t = fgh[k] * (x + (float)si - frpg[k] * s1[k] - s2[k]);
            v = s1[k] + t;
            s1[k] = v + t;
            s2[k] += 2.0f * fg[k] * v;
            y = v * fr[k];                            /* the band-pass at its peak: 1 */
            acc += y * fw[k];
            y *= 1.0f / 64.0f;
            o = (int32_t)(y > 32767.0f ? 32767.0f : y < -32768.0f ? -32768.0f : y + (y < 0.0f ? -0.5f : 0.5f));
            E->line[base + (wp & mask)] = (int16_t)o;
        }
        wp--;
        v0 += dv;
        push = el_bow_table(sum, v0);
        out[i] += (int32_t)acc;
    }
    for (k = 0; k < nw; k++) {                        /* (a wild state cannot overflow the sums above) */
        float a1 = s1[k] > 268435456.0f ? 268435456.0f : s1[k] < -268435456.0f ? -268435456.0f : s1[k];
        float a2 = s2[k] > 268435456.0f ? 268435456.0f : s2[k] < -268435456.0f ? -268435456.0f : s2[k];
        E->s[k][0] = (int32_t)(a1 < 0.0f ? a1 - 0.5f : a1 + 0.5f);
        E->s[k][1] = (int32_t)(a2 < 0.0f ? a2 - 0.5f : a2 + 0.5f);
    }
    E->wp = wp;
    E->push = push;
#endif
}

/* --------------------------------------------------------------- flow --- */
typedef struct { uint32_t rng; int32_t state; } el_flow_t;   /* state: +-0.5, Q16; 0 = start */
/* a sample, +-1 at most, Q16. scale: her parameter ^4 (Q16); thr: how often it flips, of 2^32 a sample (hers:
 * 0.0001 + scale / 8, at 32 kHz) */
static inline int32_t el_flow_tick(el_flow_t *s, int32_t scale, uint32_t thr)
{
    uint32_t r = el_word(&s->rng);
    if (!s->state)
        s->state = 32768;
    if (r < thr)
        s->state = -s->state;
    return s->state + (int32_t)(((int64_t)((int32_t)(r >> 16) - 32768 - s->state) * scale) >> 16);
}

/* --------------------------------------------------------------- tube --- */
#define EL_TUBE_N 2048                                /* her kTubeDelaySize */
typedef struct {
    int32_t zero, pole;                               /* Q20 */
    uint32_t wp;
    int16_t line[EL_TUBE_N];                          /* Q13 (she keeps half of +-5) */
} el_tube_t;
typedef struct {
    uint32_t di;                                      /* the delay: samples */
    int32_t df;                                       /* .. and the rest, Q16 */
    int32_t damp;                                     /* 3.6 - 1.8 damping, Q16 */
    int32_t lpf;                                      /* Q16 */
} el_tube_blk_t;

/* Tube::Process, the block's part. f: the note (cycles a sample, Q32, > 0); damping, timbre: Q16 0..1 */
static void el_tube_block(el_tube_blk_t *B, uint32_t f, int32_t damping, int32_t timbre)
{
    uint64_t d = ((uint64_t)1 << 48) / f, l;          /* the period, Q16 */
    while (d >= ((uint64_t)EL_TUBE_N << 16))
        d >>= 1;
    B->di = (uint32_t)(d >> 16);
    B->df = (int32_t)(d & 0xFFFFu);
    B->damp = 235930 - (int32_t)(((int64_t)damping * 117965) >> 16);
    l = ((uint64_t)(f >> 8) * (uint32_t)(65536 + (int32_t)(((int64_t)timbre * timbre) >> 8))) >> 24;   /* f (1 + 256 t^2) */
    B->lpf = l > 65208u ? 65208 : (int32_t)l;         /* 0.995 */
}

/* n samples: io[] the breath (Q20) on the way in; the pipe's sound x gain x env (Q16 each) added to it */
static void el_tube_run(el_tube_t *T, const el_tube_blk_t *B, int32_t env, int32_t gain, int32_t *io, uint32_t n)
{
    uint32_t i, d = T->wp;
    int32_t zero = T->zero, pole = T->pole, ge;
    if (env > 65536)
        env = 65536;
    ge = (int32_t)(((int64_t)gain * env) >> 16);
    for (i = 0; i < n; i++) {
        int32_t breath = (int32_t)(((int64_t)io[i] * B->damp) >> 16) + 838861;        /* + 0.8 */
        int32_t a = T->line[(d + B->di) & (EL_TUBE_N - 1u)], b = T->line[(d + B->di + 1u) & (EL_TUBE_N - 1u)];
        int32_t in = (a << 7) + (((b - a) * (B->df >> 2)) >> 7);
        int32_t pd = -(int32_t)(((int64_t)((int32_t)(((int64_t)in * env) >> 16) + zero) * 62259) >> 16) - breath;   /* -0.95 (..) - breath */
        int32_t reed = 838861 - pd / 5, out;
        zero = in;
        out = (int32_t)(((int64_t)pd * reed) >> 20) + breath;
        if (out > 5 << 20)
            out = 5 << 20;
        if (out < -(5 << 20))
            out = -(5 << 20);
        T->line[d] = (int16_t)(out >> 8);
        d = (d - 1u) & (EL_TUBE_N - 1u);
        pole += (int32_t)(((int64_t)B->lpf * (out - pole)) >> 16);
        io[i] += (int32_t)(((int64_t)ge * pole) >> 16);
    }
    T->wp = d;
    T->zero = zero;
    T->pole = pole;
}

/* ------------------------------------------------------------ diffuse --- */
#ifndef EL_DIFF_D1                                    /* the four loops, samples: hers (125, 179, 268, 443 at 32 kHz) */
#define EL_DIFF_D1 172                                /* at 44.1 kHz */
#define EL_DIFF_D2 247
#define EL_DIFF_D3 369
#define EL_DIFF_D4 610
#endif
#define EL_DIFF_MEM (EL_DIFF_D1 + EL_DIFF_D2 + EL_DIFF_D3 + EL_DIFF_D4)
typedef struct {
    uint16_t p[4];
    int16_t line[EL_DIFF_MEM];                        /* Q13 */
} el_diff_t;

static void el_diff_run(el_diff_t *D, int32_t *io, uint32_t n)   /* Q20, in place */
{
    static const uint16_t LEN[4] = {EL_DIFF_D1, EL_DIFF_D2, EL_DIFF_D3, EL_DIFF_D4};
    uint32_t i, k;
    for (i = 0; i < n; i++) {
        int32_t acc = io[i];
        int16_t *l = D->line;
        for (k = 0; k < 4u; k++) {
            int32_t r = l[D->p[k]] << 7, w;
            acc += (r >> 1) + (r >> 3);               /* + 0.625 of the loop's end */
            w = (acc + 64) >> 7;
            w = w > 32767 ? 32767 : w < -32768 ? -32768 : w;
            l[D->p[k]] = (int16_t)w;
            if (++D->p[k] >= LEN[k])
                D->p[k] = 0;
            acc = r - (((w << 7) >> 1) + ((w << 7) >> 3));
            l += LEN[k];
        }
        io[i] = acc;
    }
}
