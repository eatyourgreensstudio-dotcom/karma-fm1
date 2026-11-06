/* SPDX-License-Identifier: GPL-3.0-only */
/* KARMA browse test (2026-11-06, Ariel: "moving on a sequence looking for new sounds, sometimes PLAITS or
 * ELEMENTS start the digital distortion; stop, wait, play: clean again"). A pattern plays on one part while
 * its presets are browsed as the UI does it (apply_preset_to: sound_change, panic, the new values), never
 * stopping. Each preset's sound after browsing is then compared with the same preset from a clean start
 * (stop, 1 s, play): its level, its harshness (the share of energy in the sample-to-sample difference),
 * the host time per block. A preset louder, harsher or heavier after browsing than fresh = the bug.
 *   browse_test [ENGINE_INDEX [ROUNDS]]   (default: ELEMENTS then PLAITS, 2 rounds) */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <math.h>

static uint32_t sd = 777u;
static uint32_t rn(uint32_t n) { sd = sd * 1664525u + 1013904223u; return (sd >> 8) % (n ? n : 1u); }

typedef struct { double rms, hf, ns_mean; uint32_t peak, ns_max, clip, busy_max; } met_t;

static void ui_preset(track_t *t, uint32_t e, uint32_t pi)    /* as ui.c apply_preset_to, while playing */
{
    sound_change(t);
    panic_req |= (uint8_t)(1u << (uint32_t)(t - trk));
    host_preset_req(t, e, pi);
}

static void run(uint32_t blocks, met_t *m)
{
    static int32_t o[2 * CTL];
    uint32_t b, i;
    double e = 0, d = 0, ns = 0;
    int32_t prev = 0;
    memset(m, 0, sizeof *m);
    for (b = 0; b < blocks; b++) {
        uint64_t t0 = now_ns(), t1;
        uint32_t bz;
        mix_block(o, CTL);
        t1 = now_ns();
        ns += (double)(t1 - t0);
        if ((uint32_t)(t1 - t0) > m->ns_max)
            m->ns_max = (uint32_t)(t1 - t0);
        bz = busy_now();
        if (bz > m->busy_max)
            m->busy_max = bz;
        for (i = 0; i < CTL; i++) {
            int32_t x = o[2 * i] + o[2 * i + 1], a = x < 0 ? -x : x;
            e += (double)x * x;
            d += (double)(x - prev) * (x - prev);
            prev = x;
            if ((uint32_t)a > m->peak)
                m->peak = (uint32_t)a;
            if (a > (60000 << 8))                  /* near the output's limit */
                m->clip++;
        }
    }
    m->rms = sqrt(e / (blocks * (double)CTL));
    m->hf = e > 0 ? d / e : 0;
    m->ns_mean = ns / blocks;
}

static void pattern(track_t *t)
{
    static const uint8_t N[16] = {48, 0, 0, 55, 0, 0, 51, 0, 60, 0, 0, 58, 0, 0, 63, 0};
    uint32_t i;
    for (i = 0; i < 16u; i++) {
        uint8_t ch[2] = {N[i], (uint8_t)(N[i] + 7u)};
        put_step(t, i, N[i] ? (i % 8u == 0u ? 2u : 1u) : 0u, ch, N[i] ? ST_NOTE : ST_REST, 0);
    }
}

static int test_engine(uint32_t e, uint32_t rounds)
{
    const uint32_t np = ENGINES[e]->npresets, part = e == ENGI_ELEM ? 1u : 2u;
    track_t *t = &trk[part];
    static met_t br[64], fr[64];
    uint32_t r, k, i, bad = 0;
    const uint32_t sec = FS / CTL;
    host_tracks_init();
    song.g[G_BPM] = 120;
    for (i = 0; i < NTRK; i++)
        steps_clear(&trk[i]);
    for (i = 0; i < NPART; i++)
        if (i != part)
            trk[i].p[P_LEVEL] = 0;
    song.g[G_DRLVL] = 0;
    host_preset(t, e, 0);
    pattern(t);
    transport_req = 1;
    run(sec, &br[0]);
    printf("%s: %u presets, browsed %u rounds while playing, then each from a clean start\n", ENGINES[e]->name, np, rounds);
    for (r = 0; r < rounds; r++)                     /* browse: never stopping */
        for (k = 0; k < np; k++) {
            uint32_t pi = r == 0 ? k : rn(np);
            met_t m;
            run(rn(sec / 2u), &m);                   /* somewhere in the bar */
            ui_preset(t, e, pi);
            run(sec, &m);                            /* settle */
            run(2u * sec, &m);
            if (r == rounds - 1u || m.peak > br[pi].peak) /* keep the worst seen per preset */
                br[pi] = m.peak >= br[pi].peak ? m : br[pi];
        }
    for (k = 0; k < np; k++) {                       /* fresh: stop, the preset, 1 s, play */
        met_t m;
        transport_req = 2;
        run(sec, &m);
        host_preset(t, e, k);
        run(sec, &m);
        transport_req = 1;
        run(sec, &m);
        run(2u * sec, &fr[k]);
    }
    for (k = 0; k < np; k++) {
        double lv = fr[k].rms > 0 ? br[k].rms / fr[k].rms : 0, hf = fr[k].hf > 0 ? br[k].hf / fr[k].hf : 0,
               ns = fr[k].ns_mean > 0 ? br[k].ns_mean / fr[k].ns_mean : 0;
        int flag = lv > 1.4 || hf > 1.6 || br[k].clip > fr[k].clip + 50u || ns > 1.5 || br[k].busy_max > fr[k].busy_max + 2u;
        bad += flag;
        printf("  %2u %-12s level %5.2fx  harsh %5.2fx  cost %5.2fx  clip %5u/%-5u voices %u/%u %s\n", k,
               ENGINES[e]->presets[k].name, lv, hf, ns, br[k].clip, fr[k].clip, br[k].busy_max, fr[k].busy_max,
               flag ? "<-- differs" : "");
    }
    return (int)bad;
}

int main(int argc, char **argv)
{
    uint32_t rounds = argc > 2 ? (uint32_t)atoi(argv[2]) : 2u;
    int bad = 0;
    if (argc > 1)
        bad += test_engine((uint32_t)atoi(argv[1]), rounds);
    else {
        bad += test_engine(ENGI_ELEM, rounds);
        bad += test_engine(ENGI_PRISM, rounds);
    }
    printf("%d preset(s) differ after browsing\n", bad);
    return 0;
}
