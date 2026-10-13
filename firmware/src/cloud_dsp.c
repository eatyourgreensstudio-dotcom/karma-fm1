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
/* The granular player of Clouds (clouds/dsp/granular_sample_player.h and grain.h of github.com/pichenettes/eurorack)
 * in fixed point: grains of a recording, each a short window of it read at its own speed, started at random or
 * at a steady rate, panned at random, overlapped and levelled. The recording itself is the caller's: a ring of
 * 16-bit samples that it keeps writing (fx.c: the punch-in ring, the mix of the last 0.74 s). Needs <stdint.h> only:
 * tests/cloud_test.c builds it alone against the float original (tests/cloud_ref).
 *
 * As hers: when the grains start (the same random numbers asked in the same order), where in the recording, their
 * window (from a click-edged slab to a smooth bell), the levelling by the number of grains. Not hers: 12 grains at
 * most (hers: 40 to 64), the recording read with a straight line between samples (hers: Hermite's curve).
 * Levels and positions: Q16 (1.0 = 65536). No signed 64-bit division. */
#ifndef CL_NG
#define CL_NG 12                                      /* grains at once */
#endif
#ifndef CL_RING_T                                     /* the recording's samples: 16 bits, or what the caller keeps (fx.c: */
#define CL_RING_T int16_t                             /* 8 bits, mu-law, as her low-quality modes) */
#define CL_RD(ring, i) ((ring)[i])
#endif
#ifndef CL_READ_OFS
#define CL_READ_OFS 0                                 /* (the test: 1, where her Hermite reader takes its sample) */
#endif

static const int16_t CL_SIN[65] = {0, 804, 1608, 2410, 3212, 4011, 4808, 5602, 6393, 7179, 7962, 8739, 9512, 10278, 11039, 11793, 12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530, 18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594, 23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790, 27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956, 30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971, 32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757, 32767};              /* a quarter of a sine: the pan */
static const int16_t CL_WIN[65] = {0, 20, 79, 177, 315, 491, 705, 958, 1247, 1573, 1935, 2331, 2761, 3224, 3719, 4244, 4799, 5381, 5990, 6624, 7281, 7961, 8660, 9379, 10114, 10864, 11628, 12403, 13187, 13980, 14778, 15580, 16383, 17187, 17989, 18787, 19580, 20364, 21139, 21903, 22653, 23388, 24107, 24806, 25486, 26143, 26777, 27386, 27968, 28523, 29048, 29543, 30006, 30436, 30832, 31194, 31520, 31809, 32062, 32276, 32452, 32590, 32688, 32747, 32767};              /* her window's rise: (1 - cos) / 2 */
static const uint32_t CL_SEMI[12] = {65536u, 69433u, 73562u, 77936u, 82570u, 87480u, 92682u, 98193u, 104032u, 110218u, 116772u, 123715u};           /* 2^(i / 12), Q16 */

static inline int32_t cl_tab(const int16_t *t, int32_t x)   /* t at x (Q15, 0..1), between its 65 points */
{
    int32_t i = x >> 9, f = x & 511;
    if (i >= 64)
        return t[64];
    return t[i] + (((t[i + 1] - t[i]) * f) >> 9);
}
/* a pitch in semitones (-48..48) as a speed, Q16 */
static uint32_t cl_ratio(int32_t semis)
{
    int32_t o = (semis + 48) / 12 - 4, k = (semis + 48) % 12;
    return o >= 0 ? CL_SEMI[k] << o : CL_SEMI[k] >> -o;
}
static uint32_t cl_isqrt(uint32_t v)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > v)
        b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return r;
}

typedef struct {
    uint8_t on, pre;                                  /* pre: samples of the block before it starts */
    int16_t gl, gr;                                   /* its place between left and right, Q15 */
    uint32_t first, ph, inc;                          /* where it reads: the start, the way from it and the speed (Q16) */
    uint32_t eph, einc;                               /* its window: 0 .. 2 (Q30) */
    int32_t slope, smooth;                            /* the window's shape: a slab's edge (Q12), or how much of the bell (Q15) */
} cl_grain_t;
typedef struct {
    cl_grain_t g[CL_NG];
    uint32_t rng;
    int32_t hint;                                     /* the grains' size of late, samples Q16 */
    int32_t num;                                      /* how many play, smoothed, Q16 */
    int32_t norm;                                     /* the levelling, Q24 */
    int32_t phasor;                                   /* samples since the last grain (the steady rate) */
} cl_t;
typedef struct {                                      /* her Parameters, Q16 0..1 unless said */
    int32_t position;                                 /* how far back in the recording */
    int32_t size;                                     /* a grain, samples (hers: 1024 .. 16384 at 32 kHz) */
    uint32_t ratio;                                   /* the speed it is read at (cl_ratio) */
    int32_t overlap;                                  /* how many grains at once (cubed: 0 none, 1 all of them) */
    int32_t window;                                   /* 0 a slab with clicks .. 0.5 a triangle .. 1 a bell */
    int32_t spread;                                   /* left / right at random */
    uint8_t steady;                                   /* grains at a steady rate (her deterministic seed), else at random */
    uint8_t trigger;                                  /* one grain now */
    uint8_t down;                                     /* the recording runs this many times slower than the output (0: 1) */
} cl_par_t;

static inline uint32_t cl_word(uint32_t *r)           /* stmlib Random::GetWord */
{
    *r = *r * 1664525u + 1013904223u;
    return *r;
}
static void cl_init(cl_t *C, uint32_t seed)
{
    uint32_t i;
    for (i = 0; i < sizeof *C; i++)
        ((uint8_t *)C)[i] = 0;
    C->rng = seed;
    C->hint = 1024 << 16;
    C->norm = 1 << 24;
}

/* GranularSamplePlayer::Play for n (<= 32) samples. ring: the recording, mask + 1 samples (a power of two), head:
 * where it will be written next (this block is in it already). The grains are added to l[] and r[] (16-bit levels) */
static void cl_play(cl_t *C, const cl_par_t *P, const CL_RING_T *ring, uint32_t mask, uint32_t head,
                    int32_t *l, int32_t *r, uint32_t n)
{
    uint8_t avail[CL_NG];
    uint32_t na = 0, i, t, thr, dn = P->down ? P->down : 1u;
    uint64_t tg = (((uint64_t)(uint32_t)P->overlap * (uint32_t)P->overlap) >> 8) * (uint32_t)P->overlap >> 12;   /* ^3, Q28 */
    uint64_t target28 = CL_NG * tg;                   /* how many grains it aims at, Q28 */
    int32_t target = (int32_t)(target28 >> 12), trig = P->trigger, gn, wg, active;   /* .. and Q16 */
    if (n > 32u)
        n = 32u;
    if (P->steady) {
        thr = 0;
    } else {                                          /* a grain with this chance a sample: target / size */
        uint64_t p = (target28 << 20) / (uint32_t)(C->hint > 65536 ? C->hint : 65536);
        thr = p > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)p;
        C->phasor = -1000;
    }
    for (i = 0; i < CL_NG; i++)
        if (!C->g[i].on)
            avail[na++] = (uint8_t)i;
    for (t = 0; t < n; t++) {
        uint32_t w = cl_word(&C->rng);
        int seed;
        C->phasor++;
        seed = (w < thr && target > C->num) || trig ||
               (target28 && C->phasor >= 0 && (uint64_t)(uint32_t)C->phasor * target28 >= ((uint64_t)(uint32_t)C->hint << 12));
        if (na && seed) {                             /* ScheduleGrain + Grain::Start */
            cl_grain_t *g = &C->g[avail[--na]];
            int32_t pan = 32768 + (int32_t)(((int64_t)P->spread * ((int32_t)(cl_word(&C->rng) >> 16) - 32768)) >> 16);
            int32_t gs8 = P->size << 8, gs, ws = P->window;                   /* the size: Q8 (hers: a float) */
            int64_t eaten, av;
            if (P->ratio > 65536u) {                  /* reading faster than it is written: not past the newest */
                int32_t cap = (int32_t)((((uint64_t)(mask + 1u)) << 22) / P->ratio * dn);
                if (gs8 > cap)
                    gs8 = cap;
            }
            gs = gs8 >> 8;
            eaten = (int64_t)((((uint64_t)(uint32_t)gs8 * P->ratio) >> 8) / dn);   /* Q16, of the recording's samples */
            av = ((int64_t)(mask + 1u) << 16) - eaten - (int64_t)(((uint64_t)(uint32_t)gs8 << 8) / dn);
            g->first = (head - (n - t) / dn - (uint32_t)((((av * P->position) >> 16) + eaten) >> 16)) & mask;
            g->pre = (uint8_t)t;
            g->ph = 0;
            g->inc = P->ratio / dn;
            g->eph = 0;
            g->einc = 0x80000000u / (uint32_t)((gs & ~1) > 2 ? gs & ~1 : 2);
            if (ws >= 32768) {
                g->smooth = (ws - 32768) >> 0;                                /* (ws - 0.5) x 2, Q15 */
                g->slope = 0;
            } else {
                g->smooth = 0;
                g->slope = (int32_t)((2048u << 16) / (uint32_t)(ws + 655));   /* 0.5 / (ws + 0.01), Q12 */
            }
            pan = pan < 0 ? 0 : pan > 65536 ? 65536 : pan;
            g->gl = (int16_t)cl_tab(CL_SIN, pan >> 1);
            g->gr = (int16_t)cl_tab(CL_SIN, (65536 - pan) >> 1);
            g->on = 1;
            C->hint += ((gs8 << 8) - C->hint) / 10;
            C->phasor = 0;
            trig = 0;
        }
    }
    for (i = 0; i < CL_NG; i++) {                     /* Grain::OverlapAdd */
        cl_grain_t *g = &C->g[i];
        uint32_t ph, eph;
        if (!g->on)
            continue;
        t = g->pre < n ? g->pre : n;
        g->pre = (uint8_t)(g->pre - t);
        ph = g->ph;
        eph = g->eph;
        for (; t < n; t++) {
            int32_t e = (int32_t)((eph < 0x40000000u ? eph : 0x80000000u - eph) >> 15), s, a, b;
            uint32_t k;
            if (g->smooth)
                e += (g->smooth * (cl_tab(CL_WIN, e) - e)) >> 15;
            else if (g->slope) {
                e = e >= (32768 << 12) / g->slope ? 32768 : (e * g->slope) >> 12;
            }
            eph += g->einc;
            if (eph >= 0x80000000u) {
                g->on = 0;
                break;
            }
            k = g->first + (ph >> 16) + CL_READ_OFS;
            a = CL_RD(ring, k & mask);
            b = CL_RD(ring, (k + 1u) & mask);
            s = a + (((b - a) * (int32_t)((ph & 65535u) >> 1)) >> 15);
            s = (s * e) >> 15;
            l[t] += (s * g->gl) >> 15;
            r[t] += (s * g->gr) >> 15;
            ph += g->inc;
        }
        g->ph = ph;
        g->eph = eph;
    }
    /* the levelling: 1 / sqrt(grains - 1), more with the smooth windows that overlap */
    active = (int32_t)(CL_NG - na) << 16;
    C->num += active > C->num ? (int32_t)(((int64_t)(active - C->num) * 58982 + 65535) >> 16)   /* (it gets there: hers does) */
                              : (int32_t)(((int64_t)(active - C->num) * 13107) >> 16);
    gn = C->num > 2 * 65536 ? (int32_t)(0xFFFFFFFFu / cl_isqrt((uint32_t)(C->num - 65536))) : 1 << 24;   /* Q24 */
    wg = 2 * P->window;
    wg = wg > 65536 ? 65536 : wg;                     /* (window gain - 1) */
    gn = (int32_t)(((int64_t)gn * (65536 + (int32_t)(((int64_t)wg * P->overlap) >> 16))) >> 16);
    for (t = 0; t < n; t++) {
        C->norm += (int32_t)(((int64_t)(gn - C->norm) * 655) >> 16);
        l[t] = (int32_t)(((int64_t)l[t] * C->norm) >> 24);
        r[t] = (int32_t)(((int64_t)r[t] * C->norm) >> 24);
    }
}
