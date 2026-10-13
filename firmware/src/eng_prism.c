/* SPDX-License-Identifier: GPL-3.0-only */
/* PRISM: models of Emilie Gillet's Plaits (github.com/pichenettes/eurorack, MIT), ported to fixed point in
 * prism_dsp.c (there: what each model is, and how close to hers: tests/prism_test.c). As on the module, three
 * controls shape every model: HARM, TIMB, MRPH. The envelope and the LFO are this firmware's: ENV is how far the
 * ADSR opens TIMB; ENV > FLT and LFO > FLT move TIMB, LFO > SHP moves MRPH. SUB adds a sine an octave down.
 *   FOLD   a triangle whose peak MRPH moves, through a waveshaper (HARM) and a wavefolder (TIMB)
 *   SWARM  eight sawtooths on grains of their own: HARM how far they spread, TIMB the grains' rate, MRPH their
 *          size (short: a cloud; long: glissandi)
 *   WAVE   192 waves in banks of 8 x 8: TIMB and MRPH move through a bank, HARM through the banks
 *   HARM   24 harmonics: TIMB the centre of the spectrum, MRPH how narrow it is, HARM how many bumps it has
 *   CHORD  a chord from one key: HARM picks it (octave, fifth, sus4, m, m7, m9, m11, 6/9, M9, M7, M), TIMB its
 *          inversion, MRPH the sound (a string machine, then organ and string waves)
 * The state of all but FOLD is in the part's engine arena (engines.c eng_arena_of), a slot a voice; they play four
 * voices a part at most, CHORD (five oscillators a key) two (voice.c: prism_poly). */
#include "prism_tables.h"
#include "prism_dsp.c"

static const char *const N_PRISM_MODEL[] = {"FOLD", "SWARM", "WAVE", "HARM", "CHORD"};
enum { PRM_FOLD, PRM_SWARM, PRM_WAVE, PRM_HARM, PRM_CHORD, PRM_COUNT };
typedef union {
    pr_swarm_t swarm;
    pr_wave_t wave;
    pr_harm_t harm;
    pr_chord_t chord;
} prism_slot_t;
static void *eng_arena_of(const track_t *t, uint32_t eng);
static uint32_t prism_model(const track_t *t) { return (uint32_t)clamp(t->p[P_E0], 0, PRM_COUNT - 1); }
static uint32_t prism_poly(const track_t *t)             /* (0: NVOICE) */
{
    uint32_t m = prism_model(t);
    return m == PRM_FOLD ? 0u : m == PRM_CHORD ? 2u : 4u;
}
static prism_slot_t *prism_slot(track_t *t, voice_t *v)   /* the voice's slot (0 on the drum track: none) */
{
    prism_slot_t *a = (prism_slot_t *)eng_arena_of(t, ENGI_PRISM);
    return a ? &a[(uint32_t)(v - t->v) % NVOICE] : 0;
}
static void prism_slot_init(track_t *t, voice_t *v)      /* the slot set for the model the track plays now */
{
    prism_slot_t *sl = prism_slot(t, v);
    uint32_t model = prism_model(t);
    v->s[5] = (int32_t)model + 1;
    if (sl && model == PRM_SWARM)
        pr_swarm_init(&sl->swarm);
    if (sl && model == PRM_WAVE)
        pr_wave_init(&sl->wave);
    if (sl && model == PRM_HARM)
        pr_harm_init(&sl->harm);
    if (sl && model == PRM_CHORD)
        pr_chord_init(&sl->chord);
}

static void prism_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[0] = 0x80000000u;      /* pr_fold_init */
    v->ph[2] = 0;                /* SUB */
    v->s[0] = 0;                 /* next */
    v->s[3] = 1;                 /* high */
    v->s[4] = 0;                 /* on: the shape and the gain start where they are asked */
    prism_slot_init(t, v);
}

static void prism_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    int32_t timb = (p[P_E2] << 8) + m->cutoff + mulq15(m->envq15, p[P_E4] * 256);
    int32_t mrph = (p[P_E3] << 8) + m->shape - (64 << 8), sub = p[P_E5] * 200;
    uint32_t ph2 = v->ph[2], done = 0, model = prism_model(t);
    prism_slot_t *sl = model == PRM_FOLD ? 0 : prism_slot(t, v);
    pr_fold_t s;
    if (v->s[5] != (int32_t)model + 1)                   /* (MODEL turned under a held note: its slot is not set) */
        prism_slot_init(t, v);
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
        if (sl && model == PRM_SWARM)
            pr_swarm_render(&sl->swarm, m->inc, p[P_E1] * 258, timb, mrph, b, k);
        else if (sl && model == PRM_WAVE)
            pr_wave_render(&sl->wave, m->inc, p[P_E1] * 258, timb, mrph, b, k);
        else if (sl && model == PRM_HARM)
            pr_harm_render(&sl->harm, m->inc, p[P_E1] * 258, timb, mrph, b, k);
        else if (sl && model == PRM_CHORD)           /* the knob's eleventh: the middle of that chord's range of hers */
            pr_chord_render(&sl->chord, m->inc, (2 * (p[P_E1] * 11 / 128) + 1) * 1460, timb, mrph, b, k, 0);
        else
            pr_fold_render(&s, m->inc, p[P_E1] * 258, timb, mrph, b, k);
        for (i = 0; i < k; i++) {
            int32_t x = b[i];
            if (sub) {
                ph2 += m->inc >> 1;
                x += mulq15(osc_sine(ph2), sub);
            }
            out[done + i] += mulq15(mulq15(x, amp_at(m, done + i)), VOICE_FS) << (model == PRM_SWARM ? 2 : 1);   /* (the swarm: quieter) */
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
    /* SWARM (ENV at 0: the grains' rate is the knob's) */
    {"SUPER SWARM", {1, 36, 56, 100, 0, 0, 0, 0}, {4, 80, 115, 50}, 0, 0, FX(0, 20, 14, 30)},
    {"SWARM PAD", {1, 52, 44, 90, 0, 0, 0, 0}, {70, 90, 120, 90}, 0, 0, FX(0, 30, 20, 55)},
    {"GLISS CLOUD", {1, 84, 60, 70, 0, 0, 0, 0}, {40, 90, 120, 80}, 0, 0, FX(0, 0, 30, 50)},
    {"GRAIN RAIN", {1, 70, 96, 20, 0, 0, 0, 0}, {10, 90, 120, 70}, 0, 0, FX(0, 0, 34, 46)},
    {"SWARM BASS", {1, 24, 50, 110, 0, 50, 0, 0}, {0, 70, 100, 30}, 0, 1, FX(0, 0, 0, 6), XP(P_TRANS + 1, -24)},
    /* WAVE */
    {"TABLE SWEEP", {2, 20, 40, 64, 30, 0, 0, 0}, {50, 90, 118, 80}, 0, 0, FX(0, 30, 18, 46), XP(P_LRATE + 1, 24, P_LD_FLT + 1, 40)},
    {"TABLE BASS", {2, 0, 30, 20, 70, 40, 0, 0}, {0, 62, 60, 22}, 0, 1, FX(0, 0, 0, 4), XP(P_TRANS + 1, -24)},
    {"DIGI KEYS", {2, 50, 60, 30, 60, 0, 0, 0}, {0, 78, 30, 50}, 0, 0, FX(0, 20, 16, 30)},
    {"VOX TABLE", {2, 90, 50, 80, 20, 0, 0, 0}, {60, 90, 118, 84}, 0, 0, FX(0, 40, 16, 52)},
    {"TABLE LEAD", {2, 64, 80, 50, 40, 0, 0, 0}, {0, 70, 100, 30}, 0, 1, FX(0, 10, 22, 20), XP(P_GLIDE + 1, 28)},
    /* HARM */
    {"GLASS ORGAN", {3, 30, 30, 70, 20, 0, 0, 0}, {4, 80, 120, 40}, 0, 0, FX(0, 30, 10, 36)},
    {"OVERTONES", {3, 0, 20, 96, 100, 0, 0, 0}, {30, 96, 60, 80}, 0, 0, FX(0, 0, 26, 50)},
    {"SPECTRAL PD", {3, 70, 50, 60, 0, 0, 0, 0}, {70, 90, 120, 90}, 0, 0, FX(0, 36, 20, 56), XP(P_LRATE + 1, 18, P_LD_FLT + 1, 36)},
    {"ADD BELL", {3, 96, 70, 80, 60, 0, 0, 0}, {0, 88, 0, 76}, 0, 0, FX(0, 0, 24, 44)},
    /* CHORD (HARM: 6 OCT, 17 5TH, 28 SUS4, 40 M, 51 M7, 62 M9, 74 M11, 85 6/9, 96 MAJ9, 108 MAJ7, 119 MAJ; ENV at 0:
     * TIMB is the inversion) */
    {"STRING MCH", {4, 108, 30, 30, 0, 0, 0, 0}, {40, 90, 118, 70}, 0, 0, FX(0, 50, 10, 40)},
    {"SOLINA MIN", {4, 51, 40, 20, 0, 0, 0, 0}, {50, 90, 118, 76}, 0, 0, FX(0, 60, 14, 46)},
    {"ORGAN CHRD", {4, 119, 20, 90, 0, 0, 0, 0}, {2, 80, 120, 30}, 0, 0, FX(0, 20, 0, 24)},
    {"HOUSE STAB", {4, 62, 45, 10, 0, 0, 0, 0}, {0, 62, 0, 40}, 0, 0, FX(0, 10, 26, 26)},
    {"RAVE 5TH", {4, 17, 30, 0, 0, 0, 0, 0}, {0, 66, 30, 30}, 0, 0, FX(20, 0, 22, 20)},
    {"DREAM 69", {4, 85, 60, 110, 0, 0, 0, 0}, {70, 90, 120, 90}, 0, 0, FX(0, 30, 24, 56), XP(P_LRATE + 1, 14, P_LD_FLT + 1, 24)},
};

/* CHORD: its three controls by what they do there */
static const param_desc_t *prism_desc(const track_t *t, uint32_t k)
{
    static const char *const CH[] = {"OCT", "5TH", "SUS4", "MIN", "MIN7", "MIN9", "MIN11", "6/9", "MAJ9", "MAJ7", "MAJ", 0};
    static const param_desc_t D[3] = {{"CHORD", F_INT, 0, 127, 64, CH, 0}, {"INVRT", F_PCT, 0, 127, 40, 0, 0},
                                      {"SOUND", F_PCT, 0, 127, 64, 0, 0}};
    return prism_model(t) == PRM_CHORD && k >= 1u && k <= 3u ? &D[k - 1u] : 0;
}

static const engine_t ENG_PRISM = {
    "PLAITS", {"TONE", "MOD"},      /* (the name Ariel wants on his build; the files and the code say PRISM) */
    {
        {"MODEL", F_ENUM, 0, PRM_COUNT - 1, 0, N_PRISM_MODEL, 0},
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
    .desc = prism_desc,
};
