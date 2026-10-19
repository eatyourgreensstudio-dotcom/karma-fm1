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
        song.g[G_GNON] = 1; frames(2);
        ck(gen_on == 2u, "page: GEN ON on track 2 sets its bit only");
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
    printf(fails ? "gen: FAILED\n" : "gen: all ok\n");
    return fails ? 1 : 0;
}
