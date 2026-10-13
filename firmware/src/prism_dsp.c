/* SPDX-License-Identifier: MIT
 * Copyright 2016 Emilie Gillet (emilie.o.gillet@gmail.com)
 * Ported to fixed-point C, 2026, for the PRISM engine (eng_prism.c).
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
/* Models of Plaits (plaits/dsp of github.com/pichenettes/eurorack) in fixed point, for a CPU without an FPU.
 * Needs <stdint.h> and prism_tables.h only, so the host test (tests/prism_test.c) builds it alone and compares
 * each model with the float original (tests/prism_ref).
 *
 *   fold    WaveshapingEngine: a band-limited slope (a triangle whose peak MORPH moves) through one of five
 *           waveshapers (HARMONICS morphs through them) and a wavefolder (TIMBRE its gain), both tamed as the
 *           note rises. The main output (her AUX, the folder's other table against a sine, is not made).
 *
 * Number formats: phases 32 bit (a cycle = 2^32), increments cycles per sample Q32; the slope Q16 (1.0 =
 * 65536); the parameters 0..32767 (Q15); the output Q15.
 * Departures from the float code: the frequency and the pulse width are held over a block (hers glide over it;
 * a block is 32 samples here); the shape and the folder's gain glide as hers do. */

/* ------------------------------------------------- fold (WaveshapingEngine) --- */
typedef struct {
    uint32_t phase;              /* (hers starts at 0.5) */
    int32_t next;                /* the part of the next sample the last discontinuity left, Q16 */
    int32_t shape, gain;         /* the values the last block ended at (Q15 of 0..4, Q15) */
    uint8_t high, on;
} pr_fold_t;

static void pr_fold_init(pr_fold_t *s)
{
    s->phase = 0x80000000u;
    s->next = 0;
    s->shape = 0;
    s->gain = 0;
    s->high = 1;
    s->on = 0;
}

/* stmlib NextIntegratedBlepSample: 0.1875 - t/2 + 1.5 (t/2)^2 - (t/2)^4, t and the result Q16 */
static inline int32_t pr_iblep_next(uint32_t t)
{
    uint32_t t1 = t >> 1, t2 = (t1 * t1) >> 16, t4 = (t2 * t2) >> 16;
    return (int32_t)(12288u + ((3u * t2) >> 1)) - (int32_t)t1 - (int32_t)t4;
}

/* Tame: how much of a modulation a note of frequency f0 (Q16 cycles) keeps when it puts `harmonics` (Q8)
 * partials up: 1 below 0.5 / order, 0 at Nyquist, cubed; Q15 */
static int32_t pr_tame(uint32_t f0, uint32_t harmonics_q8, uint32_t order)
{
    int32_t f = (int32_t)((f0 * harmonics_q8) >> 8), max_f = (int32_t)(32768u / order), a;
    a = 32768 - (int32_t)(((int64_t)(f - max_f) << 15) / (32768 - max_f));
    a = a < 0 ? 0 : a > 32767 ? 32767 : a;
    return (((a * a) >> 15) * a) >> 15;
}

/* n samples (Q15) into out. inc: the note; harmonics, timbre, morph: 0..32767 */
static void pr_fold_render(pr_fold_t *s, uint32_t inc, int32_t harmonics, int32_t timbre, int32_t morph, int16_t *out,
                           uint32_t n)
{
    uint32_t f0 = inc >> 16, i, pw, su, sd, disc, phase = s->phase;
    int32_t dm = morph - 16384, dh = harmonics - 16384, slope, amount, att, att2, shape1, gain1, shape, gain, dshape, dgain;
    int32_t next = s->next, high = s->high;
    /* her limits of the oscillator: f0 0.000001 .. 0.25, the pulse width 2 f0 .. 1 - 2 f0 */
    if (f0 > 16384u)
        f0 = 16384u;
    if (f0 < 1u)
        f0 = 1u;
    inc = inc > 0x40000000u ? 0x40000000u : inc < 65536u ? 65536u : inc;
    pw = (uint32_t)(32768 + ((morph * 29491) >> 15));                 /* morph * 0.45 + 0.5, Q16 */
    if (pw < 2u * f0)
        pw = 2u * f0;
    if (pw > 65536u - 2u * f0)
        pw = 65536u - 2u * f0;
    su = 0xFFFFFFFFu / pw;                                            /* 1 / pw, Q16 */
    sd = 0xFFFFFFFFu / (65536u - pw);                                 /* 1 / (1 - pw), Q16 (up to 20) */
    disc = (((su + sd) >> 4) * f0) >> 12;                             /* (slope up + down) f0, Q16 */
    /* how rich the spectrum is: less of the waveshaper and of the folder as the note rises */
    slope = 768 + (((dm < 0 ? -dm : dm) * 1280) >> 15);               /* 3 + |morph - 0.5| 5, Q8 */
    amount = (dh < 0 ? -dh : dh) * 2;                                 /* |harmonics - 0.5| 2, Q15 */
    att = pr_tame(f0, (uint32_t)slope, 16);
    att2 = pr_tame(f0, (uint32_t)((slope * (768 + ((((amount * att) >> 15) * 1280) >> 15))) >> 8), 12);
    shape1 = (16384 + ((dh * att) >> 15)) * 4;                        /* (0.5 + ..) 4: Q15 of 0..4 */
    shape1 = shape1 > 131071 ? 131071 : shape1 < 0 ? 0 : shape1;      /* (hers: 3.9999) */
    gain1 = 983 + ((((15073 * timbre) >> 15) * att2) >> 15);          /* 0.03 + 0.46 timbre tamed, Q15 */
    if (!s->on) {                                                     /* (the first block: no glide from nothing) */
        s->shape = shape1;
        s->gain = gain1;
        s->on = 1;
    }
    shape = s->shape;
    gain = s->gain;
    dshape = (shape1 - shape) / (int32_t)n;
    dgain = (gain1 - gain) / (int32_t)n;
    for (i = 0; i < n; i++) {
        int32_t this_s = next, x, si, sf, wi, wf, a, b, mix, idx, ii, ff, xm1, x0, x1, x2, c, v, w, aa, bn, y;
        uint32_t p16, wrapped;
        const int16_t *t1, *t2;
        next = 0;
        phase += inc;
        p16 = phase >> 16;
        wrapped = phase < inc;                                        /* (her phase passes 1 before it wraps) */
        if (wrapped ? high : high ^ (p16 < pw)) {                     /* past the peak: the slope turns down */
            uint32_t t = ((phase - (pw << 16)) / (inc >> 16));
            t = t > 65535u ? 65535u : t;
            this_s -= (pr_iblep_next(65536u - t) * (int32_t)(disc >> 4)) >> 12;
            next -= (pr_iblep_next(t) * (int32_t)(disc >> 4)) >> 12;
            high = !wrapped && p16 < pw;
        }
        if (wrapped) {                                                /* a new cycle: it turns up */
            uint32_t t = phase / (inc >> 16);
            t = t > 65535u ? 65535u : t;
            this_s += (pr_iblep_next(65536u - t) * (int32_t)(disc >> 4)) >> 12;
            next += (pr_iblep_next(t) * (int32_t)(disc >> 4)) >> 12;
            high = 1;
        }
        next += high ? (int32_t)((p16 * (su >> 4)) >> 12) : 65536 - (int32_t)(((p16 - pw) * (sd >> 4)) >> 12);
        x = 2 * this_s - 65536;                                       /* the slope, -1..1 Q16 */
        /* the waveshaper: two of the five tables, read at 127 x + 128 */
        shape += dshape;
        gain += dgain;
        si = shape >> 15;
        si = si < 0 ? 0 : si > 3 ? 3 : si;
        sf = shape - (si << 15);
        t1 = PR_WS[si];
        t2 = PR_WS[si + 1];
        idx = 127 * x + (128 << 16);                                  /* Q16 */
        wi = (idx >> 16) & 255;
        wf = (idx & 0xFFFF) >> 1;                                     /* Q15 */
        a = t1[wi] + (((t1[wi + 1] - t1[wi]) * wf) >> 15);
        b = t2[wi] + (((t2[wi + 1] - t2[wi]) * wf) >> 15);
        mix = a + (((b - a) * sf) >> 15);                             /* Q15 */
        /* the folder: its table (512 points, one before, two after) read at (mix gain + 0.5) 512, Hermite */
        idx = (((mix * gain) >> 15) + 16384) * 512;                   /* Q15 of 0..512 */
        ii = idx >> 15;
        ii = ii < 0 ? 0 : ii > 512 ? 512 : ii;
        ff = idx & 32767;
        xm1 = PR_FOLD[ii];
        x0 = PR_FOLD[ii + 1];
        x1 = PR_FOLD[ii + 2];
        x2 = PR_FOLD[ii + 3];
        c = (x1 - xm1) >> 1;
        v = x0 - x1;
        w = c + v;
        aa = w + v + ((x2 - x0) >> 1);
        bn = w + aa;
        y = ((((((aa * ff) >> 15) - bn) * ff) >> 15) + c);
        y = ((y * ff) >> 15) + x0;
        out[i] = (int16_t)(y > 32767 ? 32767 : y < -32767 ? -32767 : y);
    }
    s->phase = phase;
    s->next = next;
    s->high = (uint8_t)high;
    s->shape = shape1;
    s->gain = gain1;
}
