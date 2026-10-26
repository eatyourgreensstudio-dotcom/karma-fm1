/* SPDX-License-Identifier: GPL-3.0-only
 * Portions Copyright (C) 2026 Juan Ignacio Antonucci (Juan Jiant): the harmony's idea (one key and one plan of chords
 * that every part reads, each part in its role) and the pitch helpers, after gen.c of JIANT FM (GPL-3.0-only). */
/* SEQ > GEN (2026-10-26: composed, not thrown). One KEY (SEQ > HARMONY: KEY and MODE are track 1's ROOT and SCL,
 * set for the three synth tracks and ACID GEN together, so they are kept in the project) and a PROGRESSION of chords,
 * one every BARS bars, that every GEN track follows, each in its role:
 *   track 1  BASS: one rhythm of DENS hits reused every bar, each hit a tone of the bar's chord (the root mostly, its
 *            octave, seventh, fifth, third, second: an acid line's weights), the chord's root where a chord begins;
 *            ACC and SLIDE their flags. DRIFT picks a hit's tone again, in every bar at once (the line keeps its
 *            shape and follows the chords), now and then a hit moves.
 *   track 2  LINE: long notes, chord tones (third, fifth, seventh, root), each the nearest to the one before
 *            (voice leading) but not the same note again, a new one each bar; DENS: half way too; a short sound (ATK and
 *            REL short: a struck ELEMENTS) struck again on the beats, the tone or another of the chord. DRIFT: a
 *            chord's tone again as it comes round.
 *   track 3  LEAD: a one-bar motif (a rhythm cell, DENS how busy; steps, a third, a leap turned back) developed
 *            over the loop (AAAA', AABA', ABAB'), each bar's motif moved to its chord, chord tones on the beats,
 *            the phrase ending on the key's tonic. DRIFT: the last bar varied again (now and then a new motif).
 * OCT moves every register by octaves (3: bass from C2, line G3, lead Bb3); RANGE how wide the lead goes. The loop is
 * the progression's (the track's LEN set to it, at most 64 steps). A key on the GEN or HARMONY page: the KEY's root,
 * GEN on, a new part. GEN is on per track (gen_on: not kept; what it wrote is the track's pattern and is kept). */
static volatile uint8_t gen_on;                      /* bit n: GEN changes synth track n's loop */

#define GK_NMODE 5
#define GK_NPROG 6
static const uint8_t GK_SC[GK_NMODE][7] = {
    {0, 2, 3, 5, 7, 8, 10},                          /* MIN */
    {0, 2, 3, 5, 7, 9, 10},                          /* DOR */
    {0, 1, 3, 5, 7, 8, 10},                          /* PHRY */
    {0, 2, 4, 5, 7, 9, 11},                          /* MAJ */
    {0, 2, 4, 5, 7, 9, 10},                          /* MIX */
};
static const uint8_t GK_SCL[GK_NMODE] = {2, 3, 8, 1, 4};   /* the tracks' SCL (params.c N_SCALE) */
static const uint8_t GK_AG[GK_NMODE] = {0, 4, 1, 5, 5};    /* ACID GEN's SCALE */
/* DRONE i, SWAY i-bVII, DEEP i-iv, EPIC i-bVI-bVII, POP i-bVI-III-bVII, MOOD i-iv-bVI-v (degrees of the mode) */
static const uint8_t GK_PROG[GK_NPROG][4] = {{0}, {0, 6}, {0, 3}, {0, 5, 6}, {0, 5, 2, 6}, {0, 3, 5, 4}};
static const uint8_t GK_PN[GK_NPROG] = {1, 2, 2, 3, 4, 4};
static struct {
    uint8_t root, mode, n, cst, len;                 /* the key; chords; steps a chord; the loop */
    uint8_t deg[4];
} gk = {0, 0, 1, 16, 16, {0}};

static uint32_t gk_mode_of(int32_t scl)              /* a track's SCL as a mode of seven (CHR, the rest: MIN) */
{
    static const uint8_t M[16] = {0, 3, 0, 1, 4, 0, 3, 0, 2, 3, 2, 1, 0, 0, 0, 0};
    return M[(uint32_t)scl & 15u];
}
static int32_t gk_note(int32_t d)                    /* the mode's degree d (any: 7 an octave up) from the key's root */
{
    int32_t o = d >= 0 ? d / 7 : -((6 - d) / 7);
    return gk.root + 12 * o + GK_SC[gk.mode][d - 7 * o];
}
static int32_t gk_deg(int32_t note)                  /* the degree nearest a note */
{
    int32_t d = (note - gk.root) / 12 * 7 - 7, best = d, bd = 999, k;
    for (k = 0; k < 24; k++) {
        int32_t e = gk_note(d + k) - note;
        e = e < 0 ? -e : e;
        if (e < bd)
            bd = e, best = d + k;
    }
    return best;
}
static int32_t gk_near(int32_t target, int32_t d)    /* degree d's pitch class nearest a note */
{
    int32_t n = gk_note(d);
    while (n < target - 6)
        n += 12;
    while (n > target + 5)
        n -= 12;
    return n;
}
static int32_t gk_fold(int32_t n, int32_t lo, int32_t hi)   /* into [lo, hi] by octaves */
{
    while (n < lo)
        n += 12;
    while (n > hi && n - 12 >= lo)
        n -= 12;
    return n;
}
static void gk_load(void)                            /* the plan, from the key and HARMONY's knobs */
{
    const int16_t *g = song.g;
    uint32_t p = (uint32_t)clamp(g[G_GKPROG], 0, GK_NPROG - 1), k, st = 16u << clamp(g[G_GKBARS], 0, 2);
    gk.root = (uint8_t)((uint32_t)trk[0].p[P_ROOT] % 12u);
    gk.mode = (uint8_t)gk_mode_of(trk[0].p[P_SCALE]);
    gk.n = GK_PN[p];
    for (k = 0; k < gk.n; k++) {
        uint32_t d = GK_PROG[p][k];
        if (gk_note((int32_t)d + 4) - gk_note((int32_t)d) != 7)   /* (a diminished chord of the mode: its V, or i) */
            d = gk_note(8) - gk_note(4) == 7 && d != 4u ? 4u : 0u;
        gk.deg[k] = (uint8_t)d;
    }
    while (st > 16u && st * gk.n > NSTEP)
        st /= 2u;
    gk.cst = (uint8_t)st;
    gk.len = (uint8_t)(st * gk.n);
}
static uint32_t gk_chord(uint32_t step) { return gk.deg[(step % gk.len) / gk.cst]; }
static int gk_is_tone(int32_t d, uint32_t c)         /* degree d a tone of the triad on c */
{
    int32_t r = ((d - (int32_t)c) % 7 + 7) % 7;
    return r == 0 || r == 2 || r == 4;
}
static uint32_t gk_pick(const uint8_t *w, uint32_t n)   /* an index by its weight */
{
    uint32_t i, sum = 0, r;
    for (i = 0; i < n; i++)
        sum += w[i];
    r = sum ? rng() % sum : 0u;
    for (i = 0; i < n; i++) {
        if (r < w[i])
            return i;
        r -= w[i];
    }
    return 0;
}
static int32_t gen_oct(void) { return 12 * (clamp(song.g[G_GNOCT], 1, 5) - 3); }

/* what each part keeps between its writes (not kept: a new part when the FM-1 starts) */
static struct {
    uint16_t mask;                                   /* BASS: its rhythm in the bar; LEAD: its cell */
    uint8_t pick[16];                                /* BASS: the tone of each hit (GB_*) */
    int8_t mdeg[8];                                  /* LEAD: the motif's degrees from its first note */
    uint8_t mpos[8], mn, form;                       /* ... where in the bar, how many; AAAA' AABA' ABAB' */
} gpart[NPART];

/* ---- BASS ---- */
enum { GB_R, GB_OCT, GB_7, GB_5, GB_3, GB_2, GB_N };
static void gen_bass_pick(uint32_t s)
{
    uint8_t w[GB_N] = {55, 15, 8, 10, 7, 5};
    w[GB_OCT] = (uint8_t)(5 + 8 * clamp(song.g[G_GNRANGE], 1, 3));
    gpart[0].pick[s] = (uint8_t)gk_pick(w, GB_N);
}
static void gen_bass_step(track_t *t, uint32_t i)
{
    static const uint8_t TONE[GB_N] = {0, 7, 6, 4, 2, 1};   /* the pick's degree over the chord's root (GB_OCT: 7) */
    step_t *s = &t->step[i];
    uint32_t sl = i % 16u, c = gk_chord(i), first = 0, k;
    uint16_t m = gpart[0].mask;
    int32_t lo = 36 + gen_oct(), root = gk_fold(gk_note((int32_t)c), lo, lo + 11), n;
    const int16_t *g = song.g;
    memset(s, 0, sizeof *s);
    if (!((m >> sl) & 1u)) {
        s->time = ST_REST;
        s->note[0] = (uint8_t)root;
        return;
    }
    for (k = 0; k < 16u && !((m >> k) & 1u); k++)
        ;
    first = i % gk.cst < 16u && sl == k;             /* the chord's first hit: its root */
    k = first ? GB_R : gpart[0].pick[sl];
    n = k == GB_OCT ? root + 12 : gk_fold(gk_note((int32_t)(c + TONE[k])), root, root + 11);
    s->time = ST_NOTE;
    s->n = 1;
    s->note[0] = (uint8_t)clamp(n, 0, 127);
    if (rng() % 127u < (uint32_t)g[G_GNACC])
        s->flags |= SF_ACCENT;
    if (((m >> ((sl + 1u) & 15u)) & 1u) && rng() % 127u < (uint32_t)g[G_GNSLD])
        s->flags |= SF_SLIDE;                        /* (into the next hit: legato) */
}
static void gen_bass(track_t *t)
{
    static const uint8_t W[16] = {0, 20, 50, 30, 45, 25, 60, 30, 55, 25, 60, 30, 45, 25, 60, 35};
    uint8_t w[16];
    uint32_t i, hits = 3u + (uint32_t)clamp(song.g[G_GNDENS], 0, 127) * 10u / 127u;
    memcpy(w, W, sizeof w);
    gpart[0].mask = 1u;                              /* (the one: always) */
    for (i = 1; i < hits; i++) {
        uint32_t k = gk_pick(w, 16);
        gpart[0].mask |= (uint16_t)(1u << k);
        w[k] = 0;
    }
    for (i = 0; i < 16u; i++)
        gen_bass_pick(i);
    for (i = 0; i < gk.len; i++)
        gen_bass_step(t, i);
}

/* ---- LINE ---- */
static int32_t gen_line_tone(uint32_t c, int32_t prev, int32_t lo, int32_t hi, int32_t not)
{
    static const uint8_t TD[4] = {2, 4, 6, 0};       /* third, fifth, seventh, root */
    uint8_t w[4] = {35, 25, 15, 25};
    int32_t cand[2], k;
    if (song.g[G_GNDENS] < 60)
        w[2] = 0;                                    /* (the seventh: a richer line only) */
    for (k = 0; k < 2; k++) {
        uint32_t j = gk_pick(w, 4);
        cand[k] = gk_fold(gk_near(prev, (int32_t)(c + TD[j])), lo, hi);
        if (cand[k] == not)
            cand[k] = gk_fold(gk_near(prev, (int32_t)(c + TD[(j + 1u) & 3u])), lo, hi);
    }
    k = (cand[1] > prev ? cand[1] - prev : prev - cand[1]) < (cand[0] > prev ? cand[0] - prev : prev - cand[0]);
    return cand[k];
}
static void gen_line_span(track_t *t, uint32_t a, int32_t prev, int32_t not)
{
    uint32_t i, c = gk_chord(a), dens = (uint32_t)song.g[G_GNDENS];
    /* struck: ELEMENTS' BOUNCE and PICK (a bar, a chime: it rings out), or any short sound; the bowed, blown, OMIN and
     * slow sounds hold (2026-10-27: PICKED BAR's long release had it held, a strike each half bar) */
    uint32_t pulse = t->engine == ENGI_ELEM ? (t->p[P_E0] == EM_BOUNCE || t->p[P_E0] == EM_PICK)
                                            : (t->p[P_ATK] < 12 && t->p[P_REL] < 40);
    int32_t lo = 55 + gen_oct(), hi = lo + 19, n = gen_line_tone(c, prev, lo, hi, not);
    for (i = 0; i < gk.cst && a + i < NSTEP; i++) {
        step_t *s = &t->step[a + i];
        int move = i && (i % 16u == 0u || (i % 16u == 8u && rng() % 127u < dens));   /* (2026-10-26: a new tone each
                                                     * bar, DENS: half way too; one note a chord stayed for bars) */
        memset(s, 0, sizeof *s);
        s->time = ST_TIE;
        if (pulse && i && ((i % 4u == 0u && rng() % 127u < 110u) || (i % 4u == 2u && rng() % 127u < dens * 2u / 3u)))
            move = 2;                                /* (struck: again on every beat, DENS: the eighths between) */
        if (!i || move) {
            if (move == 1 || (move == 2 && (rng() & 1u)))
                n = gen_line_tone(c, n, lo, hi, n);  /* (another tone of the chord) */
            s->time = ST_NOTE;
            s->n = 1;
            s->note[0] = (uint8_t)clamp(n, 0, 127);
        }
    }
}
static int32_t gen_held(const track_t *t, uint32_t i, int32_t dflt)   /* the note sounding before step i */
{
    uint32_t k;
    for (k = i; k-- > 0;)
        if (t->step[k].time == ST_NOTE && t->step[k].n)
            return t->step[k].note[0];
    return dflt;
}
static void gen_line(track_t *t)
{
    uint32_t a;
    for (a = 0; a < gk.len; a += gk.cst)
        gen_line_span(t, a, gen_held(t, a, 64 + gen_oct()), a ? gen_held(t, a, -1) : -1);   /* (not the same note again) */
}

/* ---- LEAD ---- */
static const uint16_t GL_CELL[8] = {             /* one-bar rhythm cells, sparse to busy */
    0x0481, 0x1101, 0x0949, 0x4511, 0x1495, 0x5145, 0x5525, 0x554D,
};
static void gen_lead_motif(void)
{
    uint32_t i, n = 0, cell = (uint32_t)clamp(song.g[G_GNDENS], 0, 127) * 6u / 127u + (rng() & 1u);
    int32_t d = 0, back = 0, lim = 2 + 2 * clamp(song.g[G_GNRANGE], 1, 3);
    uint16_t m = GL_CELL[cell & 7u];
    gpart[2].mask = m;
    for (i = 0; i < 16u && n < 8u; i++)
        if ((m >> i) & 1u) {
            if (n) {
                uint32_t r = rng() % 100u;
                int32_t up = (rng() & 1u) ? 1 : -1, mv = r < 8u ? 0 : r < 72u ? 1 : r < 94u ? 2 : 4;
                if (back) {
                    mv = 1;
                    up = -back;                      /* (a leap: a step back) */
                }
                back = mv == 4 ? up : 0;
                d += up * mv;
                if (d > lim || d < -lim)
                    d -= 2 * up * mv;
            }
            gpart[2].mdeg[n] = (int8_t)d;
            gpart[2].mpos[n++] = (uint8_t)i;
        }
    gpart[2].mn = (uint8_t)n;
    gpart[2].form = (uint8_t)(rng() % 3u);
}
/* bar b of the lead: the motif (or its variation), moved to the bar's chord; returns its first note */
static int32_t gen_lead_bar(track_t *t, uint32_t b, int32_t prev, uint32_t var)
{
    uint32_t i, a = b * 16u, c = gk_chord(a), last = (b + 1u) * 16u >= gk.len, n = gpart[2].mn;
    int32_t lo = 58 + gen_oct(), hi = lo + 21, anchor, first = -1;
    int8_t md[8];
    uint8_t mp[8];
    memcpy(md, gpart[2].mdeg, sizeof md);
    memcpy(mp, gpart[2].mpos, sizeof mp);
    if (var == 1u) {                                 /* B: the motif turned back to front */
        for (i = 0; i < n; i++)
            md[i] = gpart[2].mdeg[n - 1u - i];
    } else if (var == 2u) {                          /* B: up a third */
        for (i = 0; i < n; i++)
            md[i] = (int8_t)(md[i] + 2);
    } else if (var == 3u && n > 2u) {                /* A': shorter, room to breathe */
        n--;
    }
    {                                                /* the anchor: the chord's tone nearest the last bar's */
        static const uint8_t TD[3] = {0, 2, 4};
        int32_t best = 0, bd = 999, k;
        for (k = 0; k < 3; k++) {
            int32_t x = gk_near(prev, (int32_t)(c + TD[k])), e = x > prev ? x - prev : prev - x;
            if (e < bd)
                bd = e, best = x;
        }
        anchor = gk_deg(gk_fold(best, lo, hi - 7));
    }
    for (i = 0; i < 16u; i++) {
        step_t *s = &t->step[a + i];
        memset(s, 0, sizeof *s);
        s->time = ST_REST;
        s->note[0] = (uint8_t)clamp(gk_note(anchor), 0, 127);
    }
    for (i = 0; i < n; i++) {
        step_t *s = &t->step[a + mp[i]];
        int32_t d = anchor + md[i], note;
        if (mp[i] % 4u == 0u && !gk_is_tone(d, c))
            d += gk_is_tone(d - 1, c) ? -1 : 1;     /* (on the beat: a chord tone) */
        if (last && i + 1u == n)
            d = gk_deg(gk_near(gk_note(d), 0));      /* (the phrase's end: the key's tonic) */
        note = gk_fold(gk_note(d), lo, hi);
        if (first < 0)
            first = note;
        s->time = ST_NOTE;
        s->n = 1;
        s->note[0] = (uint8_t)clamp(note, 0, 127);
        if (!i && rng() % 127u < (uint32_t)song.g[G_GNACC] * 2u)
            s->flags |= SF_ACCENT;
    }
    return first < 0 ? prev : first;
}
static uint32_t gen_lead_var(uint32_t b)             /* the form: which bars vary (0 A, 1 2 B, 3 A') */
{
    uint32_t nb = gk.len / 16u, f = gpart[2].form;
    if (b + 1u == nb && nb > 1u)
        return 3u;
    if (f == 1u && b == 2u)
        return 1u + (rng() & 1u);                    /* AABA' */
    if (f == 2u && (b & 1u))
        return 2u;                                   /* ABAB' */
    return 0u;
}
static void gen_lead(track_t *t)
{
    uint32_t b;
    int32_t prev = 67 + gen_oct();
    for (b = 0; b * 16u < gk.len; b++)
        prev = gen_lead_bar(t, b, prev, gen_lead_var(b));
}

/* ---- the parts ---- */
static void gen_new(track_t *t)                      /* NEW, ON, a key: the track's part again, on the plan */
{
    uint32_t ti = (uint32_t)(t - trk);
    if (ti >= NPART)
        return;
    gk_load();
    if (ti == 0u) {
        gen_bass(t);
    } else if (ti == 1u) {
        gen_line(t);
    } else {
        gen_lead_motif();
        gen_lead(t);
    }
    t->p[P_SLEN] = gk.len;
    t->seq_active = 1;
}
static void gen_key_set(uint32_t root, uint32_t mode)   /* one key: the synth tracks' ROOT and SCL, ACID GEN's */
{
    uint32_t i;
    for (i = 0; i < NPART; i++) {
        trk[i].p[P_ROOT] = (int16_t)(root % 12u);
        trk[i].p[P_SCALE] = GK_SCL[mode % GK_NMODE];
    }
    song.g[G_AGROOT] = (int16_t)(root % 12u);
    song.g[G_AGSCALE] = GK_AG[mode % GK_NMODE];
}
static void gen_again(void)                          /* the plan changed: every GEN track's part on it */
{
    uint32_t i;
    for (i = 0; i < NPART; i++)
        if ((gen_on >> i) & 1u)
            gen_new(&trk[i]);
}
/* a key on the GEN page: the KEY's root (every GEN track follows), the track's part */
static void gen_new_from(track_t *t, uint32_t note)
{
    gk_load();
    if (note % 12u != gk.root) {
        gen_key_set(note, gk.mode);
        gen_again();
    }
    gen_new(t);
}
/* seq_tick: step idx of a synth track about to play. DRIFT (CHANGE) squared: fine near locked */
static void gen_tick(track_t *t, uint32_t idx)
{
    uint32_t c = (uint32_t)song.g[G_GNCHG], ti = (uint32_t)(t - trk), k;
    if (ti >= NPART || !((gen_on >> ti) & 1u) || !c || !gk.len)
        return;
    if (ti == 0u) {                                  /* BASS: a hit's tone again, in every bar */
        uint32_t sl = idx % 16u;
        if (rng() % 16129u < c * c) {
            if (sl && rng() % 4u == 0u)
                gpart[0].mask ^= (uint16_t)(1u << sl);   /* (now and then the hit moves) */
            gen_bass_pick(sl);
            for (k = sl; k < gk.len; k += 16u)
                gen_bass_step(t, k);
        }
    } else if (ti == 1u) {                           /* LINE: the chord's tone again */
        if (idx % gk.cst == 0u && idx < gk.len && rng() % 16129u < c * c * 4u) {
            int32_t was = t->step[idx].note[0];
            gen_line_span(t, idx, gen_held(t, idx, was), was);
        }
    } else if (idx == 0u && rng() % 16129u < c * c * 8u) {   /* LEAD: its last bar varied again */
        uint32_t nb = gk.len / 16u;
        if (rng() % 4u == 0u) {
            gen_lead_motif();                        /* (now and then a new motif) */
            gen_lead(t);
        } else if (nb) {
            gen_lead_bar(t, nb - 1u, gen_held(t, (nb - 1u) * 16u, 67), 1u + rng() % 3u);
        }
    }
}

/* ----------------------------------------------------------- DRUM GEN --- */
/* SEQ > DRUM GEN: the drum track's KICK, SNARE and HAT lanes written by a generator as the pattern plays.
 * MAP: Grids' drum maps (Emilie Gillet, GPL-3.0-or-later, grids_maps.h; ReadDrumMap and EvaluateDrums of
 * grids/pattern_generator.cc in C): MAP X / Y move over 25 patterns, blending the four around; each lane's
 * density (KICK, SNARE, HATS) the levels that play; CHAOS, at each pass, raises each lane's levels by a
 * random amount (Grids' randomness); a level over 192 an accent (HARD). EUCL: each lane's hits spread evenly
 * over the pattern (as many as its density says), SNARE and HATS turned by ROT, CHAOS some taken out or
 * added. MODE OFF: the lanes stay as written. The other lanes are never touched. */
#include "grids_maps.h"
enum { DG_OFF, DG_MAPM, DG_EUCL };
static const uint8_t DG_LANE[3] = {0, 2, 4};        /* KICK, SNARE, HAT (drums.c LANE_NAME) */
static uint8_t dg_pert[3];

static uint8_t dg_mix(uint8_t a, uint8_t b, uint8_t bal) { return (uint8_t)((a * (255u - bal) + b * bal) >> 8); }
static uint8_t dg_level(uint32_t step, uint32_t part, uint8_t x, uint8_t y)   /* Grids' ReadDrumMap */
{
    uint32_t i = x >> 6, j = y >> 6, off = part * 32u + step % 32u;
    uint8_t bx = (uint8_t)(x << 2), by = (uint8_t)(y << 2);
    uint8_t a = DG_NODE[DG_MAP[i][j]][off], b = DG_NODE[DG_MAP[i + 1u][j]][off];
    uint8_t c = DG_NODE[DG_MAP[i][j + 1u]][off], d = DG_NODE[DG_MAP[i + 1u][j + 1u]][off];
    return dg_mix(dg_mix(a, b, bx), dg_mix(c, d, bx), by);
}
static void dg_roll_chaos(void)                      /* a new pass: each lane's random lift (Grids: randomness >> 2) */
{
    uint32_t i, r = (uint32_t)song.g[G_DGCHAOS] * 2u;
    for (i = 0; i < 3u; i++)
        dg_pert[i] = (uint8_t)(((rng() & 255u) * (r >> 2)) >> 8);
}
/* lane `part` at step idx of len: 0 rest, 1 hit, 2 accented hit */
static uint32_t dg_hit(uint32_t idx, uint32_t part, uint32_t len)
{
    const int16_t *g = song.g;
    uint32_t dens = (uint32_t)g[G_DGK + part];
    if (g[G_DGMODE] == DG_MAPM) {
        uint32_t lvl = dg_level(idx, part, (uint8_t)(g[G_DGX] * 2), (uint8_t)(g[G_DGY] * 2)) + dg_pert[part];
        if (lvl > 255u)
            lvl = 255u;
        return lvl > 255u - dens * 255u / 127u ? (lvl > 192u ? 2u : 1u) : 0u;   /* (Grids: > ~density) */
    } else {
        uint32_t k = dens * len / 127u, pos = (idx + len * 4u - (part ? (uint32_t)g[G_DGROT] * part : 0u)) % len, hit;
        hit = k && (pos * k) % len < k;
        if (g[G_DGCHAOS] && rng() % 400u < (uint32_t)g[G_DGCHAOS])
            hit = !hit;                              /* CHAOS: now and then one more, one less */
        return hit ? (pos == 0u ? 2u : 1u) : 0u;
    }
}
static void dg_write(track_t *t, uint32_t idx, uint32_t len)
{
    uint32_t p;
    for (p = 0; p < 3u; p++) {
        uint32_t h = dg_hit(idx, p, len), k = DG_LANE[p];
        dstep_t *st = &t->dstep[idx];
        st->lvl[k / 4u] = (uint8_t)((st->lvl[k / 4u] & ~(3u << (2u * (k % 4u)))) | ((h == 2u ? LV_HARD : LV_NORM) << (2u * (k % 4u))));
        st->rat[k / 4u] &= (uint8_t)~(3u << (2u * (k % 4u)));
        if (h)
            st->on[k / 8u] |= (uint8_t)(1u << (k % 8u));
        else
            st->on[k / 8u] &= (uint8_t)~(1u << (k % 8u));
    }
}
/* the whole pattern at once (a knob turned: seen and heard now) */
static void drum_gen_fill(track_t *t)
{
    uint32_t i, len = trk_len(t);
    for (i = 0; i < len; i++)
        dg_write(t, i, len);
    t->seq_active = 1;
}
/* seq_tick: the drum track's step idx about to play */
static void drum_gen_tick(track_t *t, uint32_t idx)
{
    if (song.g[G_DGMODE] == DG_OFF)
        return;
    if (!idx)
        dg_roll_chaos();
    dg_write(t, idx, trk_len(t));
}
