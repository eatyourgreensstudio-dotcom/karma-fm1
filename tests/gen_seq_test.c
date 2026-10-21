/* SPDX-License-Identifier: GPL-3.0-only */
/* SEQ > GEN (firmware/src/gen_seq.c): CHANGE 0 keeps the loop, all the way rolls every step; the notes it writes
 * stay in the track's SCALE and inside OCT .. RANGE; DENS how many play; ON per track from the page; NEW.
 *   cc -O1 -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o build/host/gen_seq_test tests/gen_seq_test.c -lm */
#define UI_PAGES_HARNESS_ONLY 1
#include "ui_pages_test.c"

static int fails;
static void ck(int ok, const char *what) { printf("gen: %-70s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }

static int same(const track_t *t, const step_t *was, uint32_t len)
{
    return !memcmp(t->step, was, sizeof(step_t) * len);
}

int main(void)
{
    static step_t was[NSTEP];
    track_t *t;
    uint32_t i, k, len, inscale = 1, inrange = 1, notes = 0, changed;
    host_tracks_init();
    song.sel = 0;
    t = &trk[0];
    t->p[P_SLEN] = 16;
    t->p[P_ROOT] = 9;                                    /* A minor */
    t->p[P_SCALE] = 2;
    len = trk_len(t);
    song.g[G_GNDENS] = 127; song.g[G_GNRANGE] = 2; song.g[G_GNOCT] = 2;
    gen_new(t);
    for (i = 0; i < len; i++)
        if (t->step[i].time == ST_NOTE) {
            int32_t n = t->step[i].note[0], rel = n - (12 * 3 + 9);
            notes++;
            inscale &= (scale_mask(t) >> (uint32_t)((rel % 12 + 12) % 12)) & 1u;
            inrange &= rel >= 0 && rel < 24;
        }
    ck(notes == len, "NEW, DENS 100 %: every step a note");
    ck(inscale, "... each in the track's SCALE (A minor)");
    ck(inrange, "... inside OCT 2 .. 2 octaves up");
    memcpy(was, t->step, sizeof(step_t) * len);
    gen_on = 1;
    song.g[G_GNCHG] = 0;
    for (k = 0; k < 64u; k++)
        for (i = 0; i < len; i++)
            gen_tick(t, i);
    ck(same(t, was, len), "CHANGE 0: 64 passes, the loop as it was (locked)");
    song.g[G_GNCHG] = 127;
    for (i = 0; i < len; i++)
        gen_tick(t, i);
    for (i = 0, changed = 0; i < len; i++)
        changed += memcmp(&t->step[i], &was[i], sizeof(step_t)) != 0;
    ck(changed >= len / 2u, "CHANGE 100 %: one pass, most steps new");
    song.g[G_GNCHG] = 32;
    memcpy(was, t->step, sizeof(step_t) * len);
    for (i = 0, changed = 0; i < 100u * len; i++) {
        step_t s = t->step[i % len];
        gen_tick(t, i % len);
        changed += memcmp(&s, &t->step[i % len], sizeof s) != 0;
    }
    ck(changed > 50u && changed < 20u * len, "CHANGE 25 %: a few steps a pass change (it drifts)");
    gen_on = 0;
    memcpy(was, t->step, sizeof(step_t) * len);
    song.g[G_GNCHG] = 127;
    for (i = 0; i < len; i++)
        gen_tick(t, i);
    ck(same(t, was, len), "GEN OFF on the track: nothing changes");
    song.g[G_GNDENS] = 0;
    gen_new(t);
    for (i = 0, notes = 0; i < len; i++)
        notes += t->step[i].time == ST_NOTE;
    ck(notes >= 1u, "NEW at DENS 0: a note at least");
    song.g[G_GNDENS] = 64;
    {                                                     /* the page: ON follows the selected track */
        song.sel = 1; frames(2);
        memset(trk[1].step, 0, sizeof(step_t) * trk_len(&trk[1]));
        memcpy(was, trk[1].step, sizeof(step_t) * trk_len(&trk[1]));
        song.g[G_GNON] = 1; frames(2);
        ck(gen_on == 2u, "page: GEN ON on track 2 sets its bit only");
        ck(!same(&trk[1], was, trk_len(&trk[1])), "page: GEN ON writes a new loop at once (no wait for DRIFT)");
        song.sel = 0; frames(2);
        ck(song.g[G_GNON] == 0, "page: back on track 1 it shows OFF");
        song.sel = 1; frames(2);
        ck(song.g[G_GNON] == 1, "page: track 2 again: ON");
        memcpy(was, trk[1].step, sizeof(step_t) * trk_len(&trk[1]));
        song.g[G_GNNEW] = 1; frames(2);
        ck(!same(&trk[1], was, trk_len(&trk[1])) && !song.g[G_GNNEW], "page: NEW rolls track 2's loop, the knob back to --");
        song.sel = NPART; frames(2);
        song.g[G_GNON] = 1; frames(2);
        ck(song.g[G_GNON] == 0 && gen_on == 2u, "the drum track: GEN stays OFF");
    }
    {   /* DRUM GEN */
        track_t *d = TDRUM;
        uint32_t dl = 16, ok, p, x, y, n;
        d->p[P_SLEN] = 16;
        dl = trk_len(d);
        memset(d->dstep, 0, sizeof d->dstep);
        for (i = 0; i < dl; i++)
            d->dstep[i].on[1] = 0x81;                     /* SNARE 2 and COWBELL on every step: never touched */
        song.g[G_DGMODE] = DG_MAPM; song.g[G_DGCHAOS] = 0;
        song.g[G_DGK] = song.g[G_DGS] = song.g[G_DGH] = 127;
        memset(dg_pert, 0, sizeof dg_pert);
        for (ok = 1, y = 0; y < 5u; y++)
            for (x = 0; x < 5u; x++) {                    /* each node of the map, at its corner: exactly Grids' */
                const uint8_t *nd = DG_NODE[DG_MAP[x][y]];
                song.g[G_DGX] = (int16_t)(x < 4u ? x * 32u : 127u);
                song.g[G_DGY] = (int16_t)(y < 4u ? y * 32u : 127u);
                if (x == 4u || y == 4u)
                    continue;                             /* (127 * 2 = 254: not quite the last node) */
                drum_gen_fill(d);
                for (p = 0; p < 3u; p++)
                    for (i = 0; i < dl; i++)
                        ok &= dstep_has(&d->dstep[i], DG_LANE[p]) == (nd[p * 32u + i] > 0u) &&
                              (!dstep_has(&d->dstep[i], DG_LANE[p]) || (dstep_lvl(&d->dstep[i], DG_LANE[p]) == LV_HARD) == (nd[p * 32u + i] > 192u));
            }
        ck(ok, "MAP: at each node, KICK SNARE HATS are Grids' pattern (accents over 192 HARD)");
        song.g[G_DGX] = 40; song.g[G_DGY] = 90;
        drum_gen_fill(d);
        for (ok = 1, i = 0; i < dl; i++)
            ok &= d->dstep[i].on[1] == 0x81;
        ck(ok, "the other lanes (SNARE 2, COWBELL) untouched");
        song.g[G_DGK] = song.g[G_DGS] = song.g[G_DGH] = 0;
        drum_gen_fill(d);
        for (n = 0, i = 0; i < dl; i++)
            for (p = 0; p < 3u; p++)
                n += dstep_has(&d->dstep[i], DG_LANE[p]);
        ck(n == 0u, "MAP: density 0, no hits");
        song.g[G_DGMODE] = DG_EUCL;
        song.g[G_DGK] = 32; song.g[G_DGS] = 16; song.g[G_DGH] = 127; song.g[G_DGROT] = 4;
        drum_gen_fill(d);
        for (ok = 1, i = 0; i < dl; i++) {
            ok &= dstep_has(&d->dstep[i], 0) == (i % 4u == 0u);          /* 4 kicks in 16: on the beats */
            ok &= dstep_has(&d->dstep[i], 2) == (i == 4u || i == 12u);   /* 2 snares, turned 4: the backbeat */
            ok &= dstep_has(&d->dstep[i], 4);                            /* hats on every step */
        }
        ck(ok, "EUCL: 4 kicks on the beats, 2 snares on 5 and 13 (ROT 4), 16 hats");
        song.g[G_DGMODE] = DG_OFF;
        memcpy(was, d->dstep, sizeof(dstep_t) * dl);
        song.g[G_DGK] = 127;
        for (i = 0; i < dl; i++)
            drum_gen_tick(d, i);
        ck(!memcmp(was, d->dstep, sizeof(dstep_t) * dl), "MODE OFF: the lanes stay as they are");
        song.g[G_DGMODE] = DG_MAPM; song.g[G_DGCHAOS] = 127; song.g[G_DGX] = 64; song.g[G_DGY] = 64;
        song.g[G_DGK] = song.g[G_DGS] = song.g[G_DGH] = 50;
        {
            uint32_t pass, diff = 0;
            step_t prev[16];
            drum_gen_fill(d);
            for (pass = 0; pass < 8u; pass++) {
                memcpy(prev, d->dstep, sizeof(dstep_t) * dl);
                for (i = 0; i < dl; i++)
                    drum_gen_tick(d, i);
                diff += memcmp(prev, d->dstep, sizeof(dstep_t) * dl) != 0;
            }
            ck(diff >= 4u, "CHAOS: the passes differ");
        }
        song.g[G_DGMODE] = DG_OFF; frames(2);
    }
    {   /* SEQ > PLAY: the order (DIR, STRIDE), CHANCE steps */
        track_t *u = &trk[0];
        uint32_t n, ok = 1, seen = 0, hits = 0, moves = 0, prev;
        static const uint8_t PEND[10] = {0, 1, 2, 3, 2, 1, 0, 1, 2, 3};
        song.rec = 0;
        pl_dir[0] = PD_FWD; pl_stride[0] = 1;
        for (n = 0; n < 40u; n++) ok &= play_idx(u, n, 16) == n % 16u;
        ck(ok, "PLAY DIR FWD: 0, 1, 2 .. 15, 0 ..");
        pl_dir[0] = PD_REV;
        for (ok = 1, n = 0; n < 40u; n++) ok &= play_idx(u, n, 16) == 15u - n % 16u;
        ck(ok, "PLAY DIR REV: 15, 14 .. 0, 15 ..");
        pl_dir[0] = PD_PEND;
        for (ok = 1, n = 0; n < 10u; n++) ok &= play_idx(u, n, 4) == PEND[n];
        ck(ok, "PLAY DIR PEND (4 steps): 0 1 2 3 2 1 0 1 2 3 (the ends once)");
        pl_dir[0] = PD_FWD; pl_stride[0] = 3;
        for (ok = 1, n = 0; n < 16u; n++) { seen |= 1u << play_idx(u, n, 16); ok &= play_idx(u, n, 16) == n * 3u % 16u; }
        ck(ok && seen == 0xFFFFu, "PLAY STRIDE 3: 0 3 6 9 .. every step of 16 once a round");
        pl_dir[0] = PD_RAND; pl_stride[0] = 1; seen = 0;
        for (n = 0; n < 400u; n++) seen |= 1u << play_idx(u, n, 16);
        ck(seen == 0xFFFFu, "PLAY DIR RAND: every step comes up");
        pl_dir[0] = PD_WALK; pl_pos[0] = 5; prev = 5; ok = 1;
        for (n = 0; n < 400u; n++) {
            uint32_t q = play_idx(u, n, 16), d = (q + 16u - prev) % 16u;
            ok &= d == 0u || d == 1u || d == 15u;
            moves += d != 0u;
            prev = q;
        }
        ck(ok && moves > 150u, "PLAY DIR WALK: a step back, stay or a step on, never a leap");
        song.rec = 1;
        ck(play_idx(u, 7, 16) == 7u, "PLAY: while recording the steps go in order");
        song.rec = 0; pl_dir[0] = PD_FWD;
        step_fill_set(u, 3, FC_CHANCE);
        pl_chance[0] = 32;
        for (n = 0; n < 4000u; n++) hits += step_plays(u, 3);
        ck(hits > 800u && hits < 1200u, "CHANCE step at 25 %: plays about 1 time in 4");
        pl_chance[0] = 0;
        for (hits = 0, n = 0; n < 400u; n++) hits += step_plays(u, 3);
        ck(hits == 0u && step_plays(u, 4), "CHANCE 0: never; a normal step: always");
        step_fill_set(u, 3, FC_NORM); pl_chance[0] = 64;
    }
    printf(fails ? "gen: FAILED\n" : "gen: all ok\n");
    return fails ? 1 : 0;
}
