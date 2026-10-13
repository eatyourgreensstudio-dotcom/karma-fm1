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
/* The strike exciters of Elements (elements/dsp/exciter.cc of github.com/pichenettes/eurorack) in fixed point:
 * when the hits fall and how hard, a sample at a time; the pulse itself, its tone and the body it strikes are the
 * caller's (phys_dsp.c: the modal bank). Needs <stdint.h> only: tests/strike_test.c builds it alone against the
 * float original (tests/strike_ref).
 *
 *   bounce  ProcessParticles: while the key is held, hits one after the other, like something bouncing or
 *           rattling on the body: the gap to the next one drifts at random, more often longer than shorter, and
 *           each bounce is allowed less height, so they die out; her parameter is how long they keep going
 *   pick    ProcessPlectrum: a soft pulse the wrong way as the plectrum catches, then, after the time it bends
 *           the string, the pulse itself; it mutes the body a little while it touches (the damping it returns)
 *
 * Levels and her parameter: Q16 (1.0 = 65536). The random numbers: stmlib's generator, asked in her order, so
 * the same hits fall as in her code. No 64-bit division. */

static inline uint32_t el_word(uint32_t *r)           /* stmlib Random::GetWord */
{
    *r = *r * 1664525u + 1013904223u;
    return *r;
}

/* ------------------------------------------------------------- bounce --- */
typedef struct {
    uint32_t rng;
    int32_t state, range;        /* the gap to the next hit (and its level) of 0.15 s; the height still allowed */
    uint32_t delay;              /* samples to the next hit */
} el_bounce_t;

static void el_bounce_start(el_bounce_t *s)           /* the key goes down */
{
    uint32_t r = el_word(&s->rng) >> 16;
    s->state = 65536 - (int32_t)((((r * r) >> 16) * 39322u) >> 16);   /* 1 - 0.6 r^2 */
    s->delay = 0;
    s->range = 65536;
}
/* a sample with the key down: 0, or a hit's level. param: how long they last (0..1); span: 0.15 s in samples */
static inline int32_t el_bounce_tick(el_bounce_t *s, int32_t param, uint32_t span)
{
    uint32_t r;
    int32_t amount, g, out, df;
    if (s->delay) {
        s->delay--;
        return 0;
    }
    r = el_word(&s->rng) >> 16;
    amount = 68813 + (int32_t)((r * r) >> 17);        /* 1.05 + 0.5 r^2 */
    if (el_word(&s->rng) > 3006477107u) {             /* 3 in 10: a longer gap */
        s->state = (int32_t)(((int64_t)s->state * amount) >> 16);
        if (s->state >= s->range + 16384)
            s->state = s->range + 16384;
    } else if (el_word(&s->rng) < 1288490189u) {      /* then 3 in 10 of the rest: a shorter one */
        s->state = (s->state << 14) / (amount >> 2);
        if (s->state <= 1311)
            s->state = 1311;                          /* 0.02 */
    }
    s->delay = (uint32_t)(((int64_t)s->state * span) >> 16);
    g = 65536 - s->range;
    g = (int32_t)(((int64_t)g * g) >> 16);
    out = (int32_t)(((int64_t)s->state * (65536 - g)) >> 16);
    df = 65536 - param;
    s->range = (int32_t)(((int64_t)s->range * (65536 - (int32_t)(((int64_t)df * df) >> 17))) >> 16);
    return out > 0 ? out : 1;                         /* (a hit, however faint: never the 0 of no hit) */
}

/* --------------------------------------------------------------- pick --- */
typedef struct {
    uint32_t delay;              /* samples until the plectrum lets go */
    int32_t damp;                /* how much it mutes the body now, Q16 */
    int32_t first;               /* the pulse as it catches, for the next sample */
} el_pick_t;

/* the key goes down. param: how far the plectrum bends the string (0..1), sig: how hard it catches (0..1);
 * scale: samples of this rate for one of hers (Q16; hers: 32 kHz) */
static void el_pick_start(el_pick_t *s, int32_t param, int32_t sig, uint32_t scale)
{
    s->first = -(3277 + (int32_t)(((int64_t)sig * 13107) >> 16));    /* -(0.05 + 0.2 sig) */
    s->delay = (uint32_t)((((((int64_t)param * param) >> 16) * 4096 >> 16) + 64) * scale >> 16);
}
/* a sample: 0, or a pulse's level (the catch below 0, the pluck 1) */
static inline int32_t el_pick_tick(el_pick_t *s)
{
    int32_t out = s->first;
    s->first = 0;
    if (s->delay) {
        if (!--s->delay)
            out = 65536;
        s->damp = 65536 - (int32_t)(((int64_t)(65536 - s->damp) * 65339) >> 16);   /* towards 1, x 0.997 */
    } else {
        s->damp = (int32_t)(((int64_t)s->damp * 58982) >> 16);                       /* x 0.9 */
    }
    return out;
}
