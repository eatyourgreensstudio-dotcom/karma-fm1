/* SPDX-License-Identifier: GPL-3.0-only */
/* ELEMENTS: the exciters of Elements (Emilie Gillet, MIT: strike_dsp.c, bow_dsp.c) on PHYS's modal bank, as an
 * engine of their own. The code is eng_phys.c's (its models from PM_ELEM on; they are described there); this
 * file is the engine's face: its MODEL list, its presets, its name.
 *   BOUNC   hit again and again while the key is held (LAST: how long the bounces go on)
 *   PICK    plucked by her plectrum (GAP: the time between the catch and the pluck)
 *   BOW     bowed: sings while the key is held (BOW: the pressure, TONE: the bow's hiss, ACC: how rough); one note
 *   BLOW    blown: breath, and above 2 / 3 of BLOW her reed pipe (TONE: the breath's colour); one note
 *   OMIN    her Ominous Voice, the module's hidden mode (her own C++: firmware/mi/elx.cc): two FM oscillators
 *           through two filters, circling; RATIO FM FDBK DTUNE BRIT SHAPE SPACE; one note
 * STRC BRIT DAMP POS ACC as PHYS MODAL. (The name is the one Ariel wants on his build: Mutable Instruments'
 * guidelines ask derivatives not to use the module names, so rename it before any public release.) */
#include "../mi/elx.h"
enum { EM_BOUNCE, EM_PICK, EM_BOW, EM_BLOW, EM_OMIN, EM_COUNT };
_Static_assert(EM_OMIN == PM_COUNT - PM_ELEM && EM_OMIN == ELEM_OMIN, "ELEMENTS' models are PHYS's from PM_ELEM on, then OMIN");
static const char *const N_ELEM_MODEL[EM_COUNT] = {"BOUNC", "PICK", "BOW", "BLOW", "OMIN"};

/* OMIN: its knobs (E1..E7) */
static const param_desc_t OMIN_D[7] = {
    {"RATIO", F_PCT, 0, 127, 64, 0, 0}, {"FM", F_PCT, 0, 127, 64, 0, 0}, {"FDBK", F_PCT, 0, 127, 40, 0, 0},
    {"DTUNE", F_PCT, 0, 127, 30, 0, 0}, {"BRIT", F_PCT, 0, 127, 64, 0, 0}, {"SHAPE", F_PCT, 0, 127, 40, 0, 0},
    {"SPACE", F_PCT, 0, 127, 50, 0, 0}};
static const param_desc_t *omin_desc(uint32_t k) { return k >= 1u && k <= 7u ? &OMIN_D[k - 1u] : 0; }

/* OMIN: the one voice, at the arena's start (the PHYS slots' place: either takes it over) */
static void *omin_of(track_t *t)
{
    uint32_t p = (uint32_t)(t - trk);
    void *a;
    if (p >= NPART || elx_size() > eng_arena_size(t))
        return 0;
    static uint32_t gen_seen[NPART];
    a = eng_arena_of(t, ENGI_ELEM);
    if (!a)
        return 0;
    if (gen_seen[p] != eng_arena_gen[p]) {               /* (the arena was cleared: set up again) */
        gen_seen[p] = eng_arena_gen[p];
        elx_ready[p] = 0;
    }
    if (!elx_ready[p]) {
        elx_init(a);
        elx_ready[p] = 1;
        phys_lay[p] = 0;                                 /* (the PHYS slots, back: every one starts over) */
    }
    return a;
}
static void elem_note_on(track_t *t, voice_t *v)
{
    if (elem_omin(t))
        (void)omin_of(t);
    else
        phys_note_on_e(t, v, 1);
}
static void elem_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    elx_params_t q;
    uint32_t done = 0, i;
    void *o;
    if (!elem_omin(t)) {
        phys_render_e(t, v, out, n, m, 1);
        return;
    }
    if (!(o = omin_of(t)))
        return;
    q.note = (float)m->pitch16 * (1.0f / 16.0f);
    q.strength = (float)v->vel * (1.0f / 127.0f);
    q.ratio = p[P_E1] * (1.0f / 127.0f);
    q.fm = p[P_E2] * (1.0f / 127.0f);
    q.feedback = p[P_E3] * (1.0f / 127.0f);
    q.detune = p[P_E4] * (1.0f / 127.0f);
    q.brightness = (float)clamp((p[P_E5] << 8) + m->cutoff, 0, 127 << 8) * (1.0f / (127 << 8));   /* (ENV, LFO > FLT) */
    q.shape = (float)clamp((p[P_E6] << 8) + m->shape - (64 << 8), 0, 127 << 8) * (1.0f / (127 << 8));   /* (LFO > SHP) */
    q.space = p[P_E7] * (1.0f / 127.0f);
    while (done < n) {
        float b[ELX_MAX_BLOCK];
        uint32_t k = n - done < ELX_MAX_BLOCK ? n - done : ELX_MAX_BLOCK;
        elx_render(o, &q, v->gate, b, k);
        for (i = 0; i < k; i++) {
            float f = b[i] * 32767.0f;
            int32_t x = f > 32767.0f ? 32767 : f < -32767.0f ? -32767 : (int32_t)f;
            out[done + i] += mulq15(mulq15(x, amp_at(m, done + i)), VOICE_FS) << 1;
        }
        done += k;
    }
}

static const preset_t ELEM_PRESETS[] = {
    /* name, {MODEL, STRC, BRIT, DAMP, POS, ACC, .., ..}, {A D S R}, fenv, mono */
    /* BOUNC: {MODEL, STRC, BRIT, DAMP, POS, ACC, LAST, EXC}; PICK: {.., GAP, EXC} (Elements' strikes) */
    {"BOUNCE BAR", {EM_BOUNCE, 116, 60, 70, 24, 100, 70, 20}, {0, 100, 127, 60}, 0, 0, FX(0, 0, 14, 34)},
    {"MARBLES", {EM_BOUNCE, 98, 100, 50, 10, 110, 40, 40}, {0, 100, 127, 40}, 0, 0, FX(0, 0, 20, 30)},
    {"RATTLE BELL", {EM_BOUNCE, 70, 90, 110, 12, 90, 110, 6}, {0, 100, 127, 96}, 0, 0, FX(0, 10, 24, 56)},
    {"PICKED BAR", {EM_PICK, 116, 70, 80, 24, 100, 50, 20}, {0, 100, 127, 56}, 0, 0, FX(0, 0, 16, 32)},
    {"PICK CHIME", {EM_PICK, 70, 104, 112, 10, 100, 90, 10}, {0, 100, 127, 96}, 0, 0, FX(0, 12, 26, 60)},
    /* BOW: {MODEL, STRC, BRIT, DAMP, POS, ACC, BOW, TONE}; BLOW: {.., BLOW, TONE} (Elements' bow and blow; one note) */
    {"BOW STRING", {EM_BOW, 34, 60, 100, 12, 80, 100, 80}, {30, 90, 127, 70}, 0, 0, FX(0, 14, 10, 50)},
    {"BOW GLASS", {EM_BOW, 98, 80, 118, 8, 60, 110, 90}, {50, 90, 127, 100}, 0, 0, FX(0, 24, 20, 72)},
    {"BOW BASS", {EM_BOW, 34, 45, 96, 16, 110, 100, 70}, {10, 90, 127, 50}, 0, 1, FX(0, 0, 0, 20), XP(P_TRANS + 1, -12)},
    {"PIPE", {EM_BLOW, 34, 60, 100, 12, 80, 127, 80}, {20, 90, 127, 60}, 0, 0, FX(0, 10, 16, 50)},
    {"BREATHY", {EM_BLOW, 34, 70, 90, 12, 80, 50, 90}, {30, 90, 127, 70}, 0, 0, FX(0, 16, 20, 56)},
    {"BLOWN BELL", {EM_BLOW, 70, 100, 120, 12, 100, 110, 110}, {10, 90, 127, 100}, 0, 0, FX(0, 20, 24, 66)},
    /* OMIN: {MODEL, RATIO, FM, FDBK, DTUNE, BRIT, SHAPE, SPACE} (her Ominous Voice; one note) */
    {"OMINOUS", {EM_OMIN, 64, 70, 50, 30, 60, 40, 60}, {60, 90, 127, 90}, 0, 1, FX(0, 20, 20, 60)},
    {"DARK DRONE", {EM_OMIN, 30, 90, 80, 50, 40, 20, 90}, {90, 90, 127, 110}, 0, 1, FX(0, 30, 24, 70), XP(P_TRANS + 1, -12)},
    {"OMEN BELL", {EM_OMIN, 100, 60, 20, 10, 90, 70, 30}, {0, 90, 60, 90}, 0, 1, FX(0, 10, 26, 56)},
};

static const engine_t ENG_ELEM = {
    .name = "ELEMENTS",
    .page_title = {"BODY", "EXCT"},
    .edit = {
        {"MODEL", F_ENUM, 0, EM_COUNT - 1, 0, N_ELEM_MODEL, 0},
        {"STRC", F_PCT, 0, 127, 64, 0, 0},
        {"BRIT", F_PCT, 0, 127, 80, 0, 0},
        {"DAMP", F_PCT, 0, 127, 80, 0, 0},
        {"POS", F_PCT, 0, 127, 20, 0, 0},
        {"ACC", F_PCT, 0, 127, 90, 0, 0},
        {"BOW", F_PCT, 0, 127, 100, 0, 0},
        {"EXC", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = ELEM_PRESETS,
    .npresets = NELEM(ELEM_PRESETS),
    .note_on = elem_note_on,
    .render = elem_render,
    .fil_page = -1,
    .color = 0xFD8F,
    .macro = {P_E1, P_E2, P_E3, P_E4},
    .poly = PHYS_POLY,
    .desc = elem_desc,
};
