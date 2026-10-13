/* SPDX-License-Identifier: GPL-3.0-only */
/* PRISM: models of Emilie Gillet's Plaits (github.com/pichenettes/eurorack, MIT), ported to fixed point in
 * prism_dsp.c (there: what each model is, and how close to hers: tests/prism_test.c). As on the module, three
 * controls shape every model: HARM, TIMB, MRPH. The envelope and the LFO are this firmware's: ENV is how far the
 * ADSR opens TIMB; ENV > FLT and LFO > FLT move TIMB, LFO > SHP moves MRPH. SUB adds a sine an octave down.
 *   FOLD   a triangle whose peak MRPH moves, through a waveshaper (HARM) and a wavefolder (TIMB) */
#include "prism_tables.h"
#include "prism_dsp.c"

static const char *const N_PRISM_MODEL[] = {"FOLD"};

static void prism_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[0] = 0x80000000u;      /* pr_fold_init */
    v->ph[2] = 0;                /* SUB */
    v->s[0] = 0;                 /* next */
    v->s[3] = 1;                 /* high */
    v->s[4] = 0;                 /* on: the shape and the gain start where they are asked */
}

static void prism_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    int32_t timb = (p[P_E2] << 8) + m->cutoff + mulq15(m->envq15, p[P_E4] * 256);
    int32_t mrph = (p[P_E3] << 8) + m->shape - (64 << 8), sub = p[P_E5] * 200;
    uint32_t ph2 = v->ph[2], done = 0;
    pr_fold_t s;
    timb = clamp(timb, 0, 127 << 8) * 32767 / (127 << 8);
    mrph = clamp(mrph, 0, 127 << 8) * 32767 / (127 << 8);
    s.phase = v->ph[0];
    s.next = v->s[0];
    s.shape = v->s[1];
    s.gain = v->s[2];
    s.high = (uint8_t)v->s[3];
    s.on = (uint8_t)v->s[4];
    while (done < n) {                                   /* (the mix runs in blocks of CTL) */
        int16_t b[CTL];
        uint32_t k = n - done < CTL ? n - done : CTL, i;
        pr_fold_render(&s, m->inc, p[P_E1] * 258, timb, mrph, b, k);
        for (i = 0; i < k; i++) {
            int32_t x = b[i];
            if (sub) {
                ph2 += m->inc >> 1;
                x += mulq15(osc_sine(ph2), sub);
            }
            out[done + i] += mulq15(mulq15(x, amp_at(m, done + i)), VOICE_FS) << 1;
        }
        done += k;
    }
    v->ph[0] = s.phase;
    v->ph[2] = ph2;
    v->s[0] = s.next;
    v->s[1] = s.shape;
    v->s[2] = s.gain;
    v->s[3] = s.high;
    v->s[4] = s.on;
}

static const preset_t PRISM_PRESETS[] = {
    /* name, {MODEL, HARM, TIMB, MRPH, ENV, SUB, -, -}, {A D S R}, fenv, mono */
    {"FOLD LEAD", {0, 64, 46, 64, 60, 0, 0, 0}, {0, 70, 90, 34}, 0, 1, FX(0, 8, 18, 14), XP(P_GLIDE + 1, 24)},
    {"FOLD BASS", {0, 64, 30, 40, 84, 60, 0, 0}, {0, 58, 40, 20}, 0, 1, FX(0, 0, 0, 0), XP(P_TRANS + 1, -24)},
    {"WEST PLUCK", {0, 20, 20, 64, 110, 0, 0, 0}, {0, 52, 0, 40}, 0, 0, FX(0, 0, 24, 22)},
    {"GLASS FOLD", {0, 110, 58, 90, 30, 0, 0, 0}, {40, 90, 110, 80}, 0, 0, FX(0, 40, 18, 46)},
    {"BUCHLA BELL", {0, 127, 84, 20, 70, 0, 0, 0}, {0, 84, 0, 70}, 0, 0, FX(0, 10, 26, 40)},
    {"FOLD DRONE", {0, 0, 70, 110, 0, 40, 0, 0}, {70, 90, 127, 90}, 0, 0, FX(0, 30, 20, 50), XP(P_LRATE + 1, 20, P_LD_FLT + 1, 30)},
};

static const engine_t ENG_PRISM = {
    "PRISM", {"TONE", "MOD"},
    {
        {"MODEL", F_ENUM, 0, 0, 0, N_PRISM_MODEL, 0},
        {"HARM", F_PCT, 0, 127, 64, 0, 0},
        {"TIMB", F_PCT, 0, 127, 40, 0, 0},
        {"MRPH", F_PCT, 0, 127, 64, 0, 0},
        {"ENV", F_PCT, 0, 127, 64, 0, 0},
        {"SUB", F_PCT, 0, 127, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},
    },
    PRISM_PRESETS, sizeof(PRISM_PRESETS) / sizeof(PRISM_PRESETS[0]), 0, prism_note_on, prism_render,
    0xA2FF, {P_E1, P_E2, P_E3, P_REL},
};
