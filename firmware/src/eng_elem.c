/* SPDX-License-Identifier: GPL-3.0-only */
/* ELEMENTS: the exciters of Elements (Emilie Gillet, MIT: strike_dsp.c, bow_dsp.c) on PHYS's modal bank, as an
 * engine of their own. The code is eng_phys.c's (its models from PM_ELEM on; they are described there); this
 * file is the engine's face: its MODEL list, its presets, its name.
 *   BOUNC   hit again and again while the key is held (LAST: how long the bounces go on)
 *   PICK    plucked by her plectrum (GAP: the time between the catch and the pluck)
 *   BOW     bowed: sings while the key is held (BOW: the pressure, TONE: the bow's hiss, ACC: how rough); one note
 *   BLOW    blown: breath, and above 2 / 3 of BLOW her reed pipe (TONE: the breath's colour); one note
 * STRC BRIT DAMP POS ACC as PHYS MODAL. (The name is the one Ariel wants on his build: Mutable Instruments'
 * guidelines ask derivatives not to use the module names, so rename it before any public release.) */
enum { EM_BOUNCE, EM_PICK, EM_BOW, EM_BLOW, EM_COUNT };
_Static_assert(EM_COUNT == PM_COUNT - PM_ELEM, "ELEMENTS' models are PHYS's from PM_ELEM on");

static void elem_note_on(track_t *t, voice_t *v) { phys_note_on_e(t, v, 1); }
static void elem_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m) { phys_render_e(t, v, out, n, m, 1); }

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
};

static const engine_t ENG_ELEM = {
    .name = "ELEMENTS",
    .page_title = {"BODY", "EXCT"},
    .edit = {
        {"MODEL", F_ENUM, 0, EM_COUNT - 1, 0, N_PHYS_MODEL + PM_ELEM, 0},
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
