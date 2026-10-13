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
 *   swarm   SwarmEngine: eight sawtooth oscillators around the note, each on a grain of its own: a new random pitch
 *           offset at each grain, as wide as HARMONICS; TIMBRE the grains' rate; MORPH their size, from short
 *           windowed grains (a cloud) to long overlapping ones, where the pitch slides from one offset to the
 *           next (a swarm of glissandi). The sawtooth output (her AUX: the same with sines, is not made); the
 *           grains run freely (her trigger input, that starts bursts, is unpatched).
 *
 *   wave    WavetableEngine: 192 single-cycle waves in three banks of 8 x 8 (and a fourth, the same waves in
 *           another order), read between eight neighbours: TIMBRE along a bank's rows, MORPH along its columns,
 *           HARMONICS through the banks (there and back: 1 2 3 4 4 3 2 1; in the second half the positions snap
 *           to the waves). The waves are stored integrated and differentiated after the read, which keeps the
 *           interpolation from aliasing. The main output (her AUX, the same in 5 bits, is not made).
 *
 * No 64-bit division anywhere (the target's library has none: the link fails; the host does not notice), 64-bit
 * products only where a block or a sample needs one.
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
    if (f >= 32768)                                   /* (at Nyquist and above: nothing; and the sum below fits) */
        return 0;
    a = 32768 - ((f - max_f) << 15) / (32768 - max_f);   /* (32 bits: the CPU's library has no 64-bit divide) */
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

/* ------------------------------------------------------- ratios, random --- */
/* 2^(semitones / 12), Q16, for semitones Q8 in -96 .. +127 (linear between semitones: within 0.3 cent) */
static uint32_t pr_ratio(int32_t semis_q8)
{
    static const uint32_t ST[13] = {65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772, 123715, 131072};
    int32_t x = semis_q8 + (96 << 8), oct, st;
    uint32_t r;
    x = x < 0 ? 0 : x > (223 << 8) ? (223 << 8) : x;
    oct = (x >> 8) / 12;
    st = (x >> 8) - oct * 12;
    r = ST[st] + (((ST[st + 1] - ST[st]) * (uint32_t)(x & 255)) >> 8);
    return oct >= 8 ? r << (oct - 8) : r >> (8 - oct);
}
static uint32_t pr_rng = 0x21;                      /* stmlib Random: the same generator */
static inline uint32_t pr_rand16(void)              /* 0 .. 65535 */
{
    pr_rng = pr_rng * 1664525u + 1013904223u;
    return pr_rng >> 16;
}

/* ------------------------------------------------------ swarm (SwarmEngine) --- */
#define PR_SWARM_N 8
typedef struct {
    int32_t from, interval;      /* GrainEnvelope: the pitch offset the grain starts at and the way to the next, Q16 */
    uint32_t gphase;             /* its phase, Q16 (1.0 = 65536) */
    int32_t fm;                  /* its rate against the others', Q12 (0.5 .. 2) */
    int32_t amp, coef;           /* its amplitude and the smoothing after a change of regime, Q15 */
    uint32_t phase, inc;         /* AdditiveSawOscillator: phase, the increment the last block ended at */
    int32_t next, gain;          /* .. the next sample's share (Q16), the gain the last block ended at (Q15) */
    uint8_t big;                 /* the grains were long (size ratio >= 1) at the last block */
} pr_swarm_voice_t;
typedef struct { pr_swarm_voice_t v[PR_SWARM_N]; } pr_swarm_t;

static void pr_swarm_init(pr_swarm_t *s)
{
    uint32_t i;
    for (i = 0; i < PR_SWARM_N; i++) {
        pr_swarm_voice_t *v = &s->v[i];
        v->from = 0;
        v->interval = 65536;
        v->gphase = 65536;
        v->fm = 0;
        v->amp = 16384;
        v->coef = 0;
        v->phase = 0;
        v->inc = 0;                                  /* (hers glides from 0.01; here: from the note) */
        v->next = 0;
        v->gain = 0;
        v->big = 0;
    }
}

/* n samples (Q15) into out, n <= 32. inc: the note; harmonics, timbre, morph: 0..32767 */
static void pr_swarm_render(pr_swarm_t *s, uint32_t inc, int32_t harmonics, int32_t timbre, int32_t morph, int16_t *out, uint32_t n)
{
    /* rank (rank + 0.01) of the eight voices, Q16; rank = (i - 3) / 3 (hers: the middle of 0..7 taken as 3) */
    static const int32_t RR[PR_SWARM_N] = {64881, 28690, 7063, 0, 7500, 29564, 66191, 117383};
    int32_t acc[32], spread, size_ratio;
    uint32_t i, k, rate;
    spread = (((harmonics * harmonics) >> 15) * harmonics) >> 15;                 /* h^3, Q15 */
    /* the grains' rate, their phase a block: her NoteToFrequency(timbre 120) 0.025 n, Q16
     * (NoteToFrequency(0) 0.025 = 4.2695e-6 = 18337 / 2^32) */
    rate = (uint32_t)(((uint64_t)18337u * n * pr_ratio((timbre * 120) >> 7)) >> 32);
    size_ratio = (int32_t)(pr_ratio(((32767 - morph) * 84) >> 7) >> 2);            /* 0.25 2^((1 - morph) 7), Q16 */
    for (k = 0; k < n; k++)
        acc[k] = 0;
    for (i = 0; i < PR_SWARM_N; i++) {
        pr_swarm_voice_t *v = &s->v[i];
        int32_t target = 32767, expo, es, lin, gain1, gain, dgain, next = v->next, dinc;
        uint32_t f, phase = v->phase, vinc;
        /* GrainEnvelope::Step: a new grain when the phase passes 1 */
        v->gphase += (rate * (uint32_t)v->fm) >> 12;
        if (v->gphase >= 65536u) {
            v->gphase &= 65535u;
            v->from += v->interval;
            v->interval = (int32_t)pr_rand16() - v->from;
            v->fm = 2048 + (int32_t)((pr_rand16() * 6144u) >> 16);                /* 0.5 + 1.5 random */
        }
        /* its amplitude: long grains under a raised cosine; smoothed, more just after the regime changed */
        if (size_ratio >= 65536) {
            int32_t ph = (int32_t)(((int64_t)((int32_t)v->gphase - 32768) * size_ratio) >> 16), a;
            ph = ph < -65536 ? -65536 : ph > 65536 ? 65536 : ph;
            a = ph < 0 ? -ph : ph;                                                /* 0 .. 1, Q16 */
            target = PR_HANN[a >> 8] + (((PR_HANN[(a >> 8) + (a < 65536)] - PR_HANN[a >> 8]) * (a & 255)) >> 8);
        }
        if ((size_ratio >= 65536) != v->big)
            v->coef = 16384;
        v->coef = (v->coef * 31130) >> 15;                                        /* x 0.95 */
        v->big = size_ratio >= 65536;
        v->amp += ((target - v->amp) * (16384 - v->coef)) >> 15;
        /* its pitch: short grains slide from one offset to the next, long ones hold theirs */
        expo = size_ratio < 65536 ? 2 * (v->from + (int32_t)(((int64_t)v->interval * (int32_t)v->gphase) >> 16)) - 65536 : v->from;
        es = (int32_t)(((int64_t)expo * spread) >> 15);                           /* Q16 */
        f = (uint32_t)(((uint64_t)inc * pr_ratio((es * 16 * ((int32_t)i - 3)) >> 8)) >> 16);   /* 48 semitones x rank */
        lin = (int32_t)(((int64_t)RR[i] * spread) >> 17);                         /* rank (rank + 0.01) spread / 4, Q16 */
        f += (uint32_t)(((uint64_t)f * (uint32_t)lin) >> 16);
        f = f > 0x40000000u ? 0x40000000u : f < 65536u ? 65536u : f;
        gain1 = v->amp >> 3;                                                      /* / 8 voices */
        if (!v->inc)
            v->inc = f;
        /* AdditiveSawOscillator: a band-limited sawtooth, its frequency and gain gliding over the block */
        vinc = v->inc;
        dinc = ((int32_t)(f >> 4) - (int32_t)(vinc >> 4)) / (int32_t)n * 16;
        gain = v->gain;
        dgain = (gain1 - gain) / (int32_t)n;
        for (k = 0; k < n; k++) {
            int32_t this_s = next;
            next = 0;
            vinc += (uint32_t)dinc;
            gain += dgain;
            phase += vinc;
            if (phase < vinc) {                                                   /* a new cycle: the step, smoothed */
                uint32_t t = phase / (vinc >> 16);
                t = t > 65535u ? 65535u : t;
                this_s -= (int32_t)((t * t) >> 17);                               /* ThisBlepSample: t^2 / 2 */
                next += (int32_t)(((65536u - t) * (65536u - t)) >> 17);           /* - NextBlepSample: (1 - t)^2 / 2 */
            }
            next += (int32_t)(phase >> 16);
            acc[k] += ((2 * this_s - 65536) * gain) >> 15;
        }
        v->phase = phase;
        v->next = next;
        v->inc = f;
        v->gain = gain1;
    }
    for (k = 0; k < n; k++) {
        int32_t y = acc[k] >> 1;
        out[k] = (int16_t)(y > 32767 ? 32767 : y < -32767 ? -32767 : y);
    }
}

/* ---------------------------------------------------- wave (WavetableEngine) --- */
#define PR_WAVE_LEN 128                              /* samples a wave (+ 4: one before, three after) */
typedef struct {
    uint32_t phase;
    int32_t pre[3];              /* x y z after the block's smoothing, Q16 of 0..7 */
    int32_t prev[3];             /* where the last block's glide ended */
    int32_t lp[3];               /* the per-sample smoothing */
    int32_t dprev, dlp;          /* the differentiator: the last mix, its low-passed difference (Q8 of the waves' units) */
} pr_wave_t;

static void pr_wave_init(pr_wave_t *s)
{
    uint32_t i;
    s->phase = 0;
    for (i = 0; i < 3u; i++)
        s->pre[i] = s->prev[i] = s->lp[i] = -1;      /* (set at the first block: no sweep from the corner) */
    s->dprev = s->dlp = 0;
}

/* her wave_map_: wave (x, y) of bank z (0..3); the fourth bank: the waves in the order w 101 mod 192 */
static inline const int16_t *pr_wave_at(int32_t x, int32_t y, int32_t z)
{
    int32_t w = x + y * 8 + z * 64;
    if (z == 3)
        w = (w * 101) % 192;
    return PR_WAVES + w * (PR_WAVE_LEN + 4);
}
static inline int32_t pr_wave_read(const int16_t *t, int32_t pi, int32_t pf)   /* InterpolateWaveHermite; pf Q15 */
{
    int32_t xm1 = t[pi], x0 = t[pi + 1], x1 = t[pi + 2], x2 = t[pi + 3];
    int32_t c = (x1 - xm1) >> 1, v = x0 - x1, w = c + v, a = w + v + ((x2 - x0) >> 1), bn = w + a;
    return ((((((a * pf) >> 15) - bn) * pf) >> 15) + c) * pf / 32768 + x0;
}

/* n samples (Q15) into out, n <= 32. inc: the note; harmonics, timbre, morph: 0..32767 */
static void pr_wave_render(pr_wave_t *s, uint32_t inc, int32_t harmonics, int32_t timbre, int32_t morph, int16_t *out, uint32_t n)
{
    int32_t target[3], cur[3], d[3], quant, lpc, cutoff, f0, k, i;
    uint32_t phase = s->phase;
    int64_t gain;
    inc = inc > 0x40000000u ? 0x40000000u : inc < 65536u ? 65536u : inc;
    f0 = (int32_t)(inc >> 16);                                                    /* Q16 */
    target[0] = (int32_t)(((int64_t)timbre * 458746) >> 15);                       /* x 6.9999, Q16 */
    target[1] = (int32_t)(((int64_t)morph * 458746) >> 15);
    target[2] = (int32_t)(((int64_t)harmonics * 458746) >> 15);
    if (s->pre[0] < 0)
        for (k = 0; k < 3; k++)
            s->pre[k] = s->prev[k] = s->lp[k] = target[k];
    s->pre[0] += ((target[0] - s->pre[0]) * 13) >> 6;                             /* x 0.2 */
    s->pre[1] += ((target[1] - s->pre[1]) * 13) >> 6;
    s->pre[2] += ((target[2] - s->pre[2]) * 13) >> 8;                             /* x 0.05 */
    quant = s->pre[2] - (3 << 16);                                                /* past the third bank: snap to the waves */
    quant = quant < 0 ? 0 : quant > 65536 ? 65536 : quant;
    lpc = (int32_t)((2 * (int64_t)f0 * ((4 << 16) - 3 * quant)) >> 17);           /* 2 f0 (4 - 3 q), Q15 */
    lpc = lpc < 328 ? 328 : lpc > 3277 ? 3277 : lpc;                              /* 0.01 .. 0.1 */
    for (k = 0; k < 3; k++) {
        int32_t ip = s->pre[k] >> 16, fr = s->pre[k] & 65535, cl;
        cl = (fr - 32768) * 16;                                                   /* Clamp(fr, 16) */
        cl = (cl < -32768 ? -32768 : cl > 32768 ? 32768 : cl) + 32768;
        fr += (int32_t)(((int64_t)quant * (cl - fr)) >> 16);
        cur[k] = s->prev[k];
        s->prev[k] = (ip << 16) + fr;
        d[k] = (s->prev[k] - cur[k]) / (int32_t)n;
    }
    cutoff = f0 * 64;                                                             /* min(128 f0, 1), Q15 */
    cutoff = cutoff > 32767 ? 32767 : cutoff;
    /* (0.95 - f0) / (f0 131072) for waves of 16 bits and Q15 out: (0.95 - f0) / (4 f0), Q16 */
    gain = ((62259 - f0) << 14) / f0;                                             /* (fits 32 bits: no 64-bit divide) */
    for (i = 0; i < (int32_t)n; i++) {
        int32_t x, y, z, xi, yi, zi, xf, yf, zf, z0, z1, pi, pf, a, b, c0, c1, m0, m1, mix, y15;
        for (k = 0; k < 3; k++) {
            cur[k] += d[k];
            s->lp[k] += (int32_t)(((int64_t)(cur[k] - s->lp[k]) * lpc) >> 15);
        }
        x = s->lp[0]; y = s->lp[1]; z = s->lp[2];
        x = x < 0 ? 0 : x > 458751 ? 458751 : x;
        y = y < 0 ? 0 : y > 458751 ? 458751 : y;
        z = z < 0 ? 0 : z > 458751 ? 458751 : z;
        xi = x >> 16; xf = (x & 65535) >> 1;
        yi = y >> 16; yf = (y & 65535) >> 1;
        zi = z >> 16; zf = (z & 65535) >> 1;
        phase += inc;
        pi = (int32_t)(phase >> 25);
        pf = (int32_t)((phase >> 10) & 32767);
        z0 = zi >= 4 ? 7 - zi : zi;
        z1 = zi + 1 >= 4 ? 7 - (zi + 1) : zi + 1;
        a = pr_wave_read(pr_wave_at(xi, yi, z0), pi, pf);
        b = pr_wave_read(pr_wave_at(xi + 1, yi, z0), pi, pf);
        c0 = a + (((b - a) * xf) >> 15);
        a = pr_wave_read(pr_wave_at(xi, yi + 1, z0), pi, pf);
        b = pr_wave_read(pr_wave_at(xi + 1, yi + 1, z0), pi, pf);
        c1 = a + (((b - a) * xf) >> 15);
        m0 = c0 + (((c1 - c0) * yf) >> 15);
        a = pr_wave_read(pr_wave_at(xi, yi, z1), pi, pf);
        b = pr_wave_read(pr_wave_at(xi + 1, yi, z1), pi, pf);
        c0 = a + (((b - a) * xf) >> 15);
        a = pr_wave_read(pr_wave_at(xi, yi + 1, z1), pi, pf);
        b = pr_wave_read(pr_wave_at(xi + 1, yi + 1, z1), pi, pf);
        c1 = a + (((b - a) * xf) >> 15);
        m1 = c0 + (((c1 - c0) * yf) >> 15);
        mix = (m0 << 8) + ((m1 - m0) * zf >> 7);                                  /* Q8 */
        s->dlp += (int32_t)(((int64_t)((mix - s->dprev) - s->dlp) * cutoff) >> 15);
        s->dprev = mix;
        y15 = (int32_t)(((int64_t)s->dlp * gain) >> 24);
        out[i] = (int16_t)(y15 > 32767 ? 32767 : y15 < -32767 ? -32767 : y15);
    }
    s->phase = phase;
}
