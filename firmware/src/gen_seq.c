/* SPDX-License-Identifier: GPL-3.0-only */
/* SEQ > GEN: a synth track's loop that changes itself as it plays, after the idea of Music Thing Modular's
 * Turing Machine (Tom Whitwell): the pattern loops, and CHANGE is how likely a step is rolled again as it comes
 * round (0: locked, the loop as it is; all the way: new every time). Own code. A rolled step is often a
 * neighbour of the note it held (a degree or two of the track's SCALE up or down), else any note of the scale
 * over RANGE octaves from OCT (and ROOT); DENS how many steps play, ACC and SLIDE their flags. NEW: a whole
 * new loop at once. GEN is on per track (gen_on: not kept, off at power on; what it wrote is the track's
 * pattern and is kept with it). The settings are the FM-1's, shared by the tracks. */
static volatile uint8_t gen_on;                      /* bit n: GEN changes synth track n's loop */

static uint32_t gen_scale(const track_t *t, uint8_t *deg)   /* the scale's pitch classes from ROOT (CHR: MIN) */
{
    uint32_t m = t->p[P_SCALE] ? scale_mask(t) : SCALE_MASK[2], i, n = 0;
    for (i = 0; i < 12u; i++)
        if ((m >> i) & 1u)
            deg[n++] = (uint8_t)i;
    if (!n)
        deg[n++] = 0;
    return n;
}
/* step i rolled again: a rest (DENS), else a note of the scale, near the old one half the time */
static void gen_roll(track_t *t, uint32_t i, int near)
{
    const int16_t *g = song.g;
    step_t *s = &t->step[i];
    uint8_t deg[12];
    uint32_t n = gen_scale(t, deg), top = n * (uint32_t)clamp(g[G_GNRANGE], 1, 3), pos = top, k;
    int32_t base = 12 * (g[G_GNOCT] + 1) + t->p[P_ROOT], rel = (int32_t)s->note[0] - base;
    if (near && s->time == ST_NOTE && s->n && rel >= 0) {   /* where the old note sits on the scale's ladder */
        for (k = 0; k < n && deg[k] < (uint32_t)rel % 12u; k++)
            ;
        pos = (uint32_t)rel / 12u * n + (k < n ? k : n - 1u);
    }
    memset(s, 0, sizeof *s);
    if (rng() % 127u >= (uint32_t)g[G_GNDENS]) {
        s->time = ST_REST;
        s->note[0] = (uint8_t)clamp(base, 0, 127);
        return;
    }
    if (pos < top && (rng() & 1u)) {                 /* a step or two up or down */
        int32_t p = (int32_t)pos + ((rng() & 2u) ? 1 : -1) * (int32_t)(1u + (rng() & 1u));
        pos = (uint32_t)clamp(p, 0, (int32_t)top - 1);
    } else {
        pos = rng() % top;
    }
    s->time = ST_NOTE;
    s->n = 1;
    s->note[0] = (uint8_t)clamp(base + (int32_t)(pos / n) * 12 + deg[pos % n], 0, 127);
    if (rng() % 127u < (uint32_t)g[G_GNACC])
        s->flags |= SF_ACCENT;
    if (rng() % 127u < (uint32_t)g[G_GNSLD])
        s->flags |= SF_SLIDE;
}
/* NEW: the whole loop rolled (one note at least) */
static void gen_new(track_t *t)
{
    uint32_t i, len = trk_len(t), any = 0;
    for (i = 0; i < len; i++) {
        gen_roll(t, i, 0);
        any |= t->step[i].time == ST_NOTE;
    }
    if (!any) {
        t->step[0].time = ST_NOTE;
        t->step[0].n = 1;
    }
    t->seq_active = 1;
}
/* seq_tick: step idx of a synth track about to play. CHANGE squared: fine near locked */
static void gen_tick(track_t *t, uint32_t idx)
{
    uint32_t c = (uint32_t)song.g[G_GNCHG], ti = (uint32_t)(t - trk);
    if (ti >= NPART || !((gen_on >> ti) & 1u) || !c)
        return;
    if (rng() % 16129u < c * c)
        gen_roll(t, idx, 1);
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
