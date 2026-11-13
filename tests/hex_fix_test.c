/* SPDX-License-Identifier: GPL-3.0-only */
/* KARMA 3.1.2, the HEX drums (a report: "a glitch on the hex drum effects"):
 *  - a hit cut short (stolen, choked) fades its reverb, delay and CLOUD sends as its dry sound: they stepped to 0
 *  - a MY HEX kit copied from a HEX kit plays as loud as it (the first EDIT knob made the kit 1.4 .. 2.7 dB louder)
 *  - ATK 127 ends (the envelope's step was 0: a voice never ended, a clap droned)
 *  - WAVE turned back to SIN during a hit plays a sine, not the square table */
#define main hostsim_main
#include <stddef.h>
#include "hostsim.c"
#undef main
#include <math.h>

static int fails;
static void check(int ok, const char *what)
{
    printf("%-78s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        fails++;
}
static int32_t ml[32], mr[32], rv[32], dl[32];
static void block(void)
{
    memset(ml, 0, sizeof ml); memset(mr, 0, sizeof mr); memset(rv, 0, sizeof rv); memset(dl, 0, sizeof dl);
    memset(cloud_bus, 0, sizeof cloud_bus);
    drums_render(ml, mr, rv, dl, 32);
}
static void reset(uint32_t kit)
{
    memset(&drums, 0, sizeof drums);
    drums.set = -2;
    TDRUM->p[P_E0] = (int16_t)kit;
    song.g[G_DRLVL] = 100;
    song.g[G_DRREV] = song.g[G_DRDLY] = 0;
}

/* the largest sample step in the sends that the dry sound does not have, over 4 s of kicks on kicks (own voice) */
static int32_t send_only_step(uint32_t kit)
{
    uint32_t b, i, l;
    int32_t pd = 0, pdl = 0, pc = 0, worst = 0;
    reset(kit);
    for (l = 0; l < DRUM_LANES; l++) {
        drum_send[l][0] = DRUM_SEND_REV;
        drum_send[l][1] = DRUM_SEND_DLY;
        drum_cloud[l] = 127;
    }
    for (b = 0; b < 44100 * 4 / 32; b++) {
        if (b % 43u == 0)                            /* a kick every ~31 ms: each one cuts the last */
            drum_on(36, 110);
        block();
        for (i = 0; i < 32; i++) {
            int32_t d = ml[i] - pd, sd = dl[i] - pdl, sc = cloud_bus[i] - pc;
            int32_t ad = d < 0 ? -d : d, asd = sd < 0 ? -sd : sd, asc = sc < 0 ? -sc : sc;
            /* a send step much larger than the dry one's (the sends are at most the dry's level) */
            if (asd > ad + 4000 && asd - ad > worst) worst = asd - ad;
            if (asc > ad + 4000 && asc - ad > worst) worst = asc - ad;
            pd = ml[i]; pdl = dl[i]; pc = cloud_bus[i];
        }
    }
    for (l = 0; l < DRUM_LANES; l++)
        drum_send[l][0] = drum_send[l][1] = 0, drum_cloud[l] = 0;
    return worst;
}
static double rms_hit(uint32_t kit, uint32_t note, uint32_t blocks)
{
    double s = 0;
    uint32_t b, i;
    reset(kit);
    drum_on(note, 110);
    for (b = 0; b < blocks; b++) {
        block();
        for (i = 0; i < 32; i++)
            s += (double)ml[i] * ml[i];
    }
    return sqrt(s / (blocks * 32.0));
}

int main(void)
{
    uint32_t k, l, b, act;
    char t[96];
    for (k = 0; k < 5; k++) {
        int32_t w = send_only_step(k);
        snprintf(t, sizeof t, "HEX kit %u: no click in the reverb / delay / CLOUD when a hit is cut (worst %d)", k, (int)w);
        check(w == 0, t);
    }
    for (k = 0; k < 5; k++) {
        double a, c;
        dsu_from_factory(0, k);
        a = rms_hit(k, 36, 400);
        c = rms_hit(DRUM_SYN, 36, 400);
        snprintf(t, sizeof t, "MY HEX 1 copied from HEX kit %u: as loud (%+.2f dB)", k, 20 * log10(c / a));
        check(fabs(20 * log10(c / a)) < 0.05, t);
    }
    dsu_from_factory(0, 0);                          /* ATK 127 on every sound of MY HEX 1: every voice ends */
    for (l = 0; l < DS_LANES; l++)
        dsu.k[0].s[l].atk = 127;
    reset(DRUM_SYN);
    drum_on(39, 110);
    drum_on(38, 110);
    drum_on(36, 110);
    for (b = 0; b < 44100 * 120 / 32; b++)
        block();
    for (act = 0, k = 0; k < NDRUM; k++)
        act += drums.v[k].active;
    snprintf(t, sizeof t, "ATK 127 (clap, snare, kick): every voice has ended after 2 minutes (%u on)", act);
    check(act == 0, t);
    {                                                /* WAVE SAW -> SIN while a kick rings: the sine's wave */
        lx_osc_t o;
        memset(&o, 0, sizeof o);
        o.freq = 60.f;
        o.wave = LW_SAW;
        lx_osc_freq(&o, 1.0f, 0);
        o.wave = LW_SINE;
        lx_osc_freq(&o, 1.0f, 0);
        check(o.band != 4u, "WAVE turned back to SIN during a hit: the sine, not the square table");
    }
    {                                                /* the overload guard: the drums shed first, then come back */
        uint32_t n0, n1, n2, h;
        voice_t *k;
        reset(0);
        drum_cap = NDRUM; drum_calm = 0;
        drum_on(36, 110); drum_on(42, 100); drum_on(38, 100); drum_on(46, 100); drum_on(49, 100); drum_on(51, 100);
        for (b = 0; b < 4; b++) block();
        n0 = drum_active();
        snprintf(t, sizeof t, "overload guard: six drum sounds at once (%u)", n0);
        check(n0 == 6, t);
        for (h = 0; h < 10; h++) drum_shed();
        n1 = drum_active();
        for (k = 0, l = 0; l < NDRUM; l++) if (drums.v[l].active && drums.v[l].note == 36u) k = &drums.v[l];
        snprintf(t, sizeof t, "overload: the drums shed down to %u, never under 3, the kick kept (%u)", DRUM_CAP_MIN, n1);
        check(n1 == DRUM_CAP_MIN && k != 0, t);
        drum_on(37, 100); drum_on(39, 100);
        n2 = drum_active();
        snprintf(t, sizeof t, "overload: new hits steal while capped, still %u at once (%u)", (unsigned)drum_cap, n2);
        check(n2 <= drum_cap, t);
        for (h = 0; h < DRUM_CALM_HALVES * 3u + 3u; h++) drum_calm_half(0);
        snprintf(t, sizeof t, "calm for ~6 s: all %u drum voices back (%u)", NDRUM, (unsigned)drum_cap);
        check(drum_cap == NDRUM, t);
        for (b = 0; b < 200; b++) block();
        {   /* a shed voice fades: the steps around the cut no larger than the sound's own just before it */
            int32_t pd = 0, w = 0, base = 0;
            reset(0);
            drum_on(46, 110); drum_on(49, 110); drum_on(51, 110); drum_on(42, 100);
            for (b = 0; b < 40; b++) {
                if (b == 20)
                    drum_shed();
                block();
                for (l = 0; l < 32; l++) {
                    int32_t d = ml[l] - pd, a = d < 0 ? -d : d;
                    if (b >= 12 && b <= 19 && a > base) base = a;
                    if (b >= 20 && b <= 22 && a > w) w = a;
                    pd = ml[l];
                }
            }
            snprintf(t, sizeof t, "a shed voice fades (steps around the cut %d, before it %d)", (int)w, (int)base);
            check(w <= base + base / 8, t);
        }
        drum_cap = NDRUM;
    }
    {                                                /* the CLOUD under overload: fewer grains, then back */
        static int32_t in[CTL], cl[CTL], cr[CTL];
        uint32_t on, h, j;
        int16_t dens = song.g[G_CLDENS];
        song.g[G_CLDENS] = 0;                          /* her steady rate at its fastest: every grain */
        cl_cap = CL_NG;
        for (b = 0; b < 2000; b++) {
            for (j = 0; j < CTL; j++) in[j] = (int32_t)((j * 977u + b * 131u) % 16000u) - 8000, cl[j] = cr[j] = 0;
            cloud_process(in, cl, cr, CTL);
        }
        for (on = 0, j = 0; j < CL_NG; j++) on += cloud_cl.g[j].on;
        snprintf(t, sizeof t, "CLOUD at DENS 0: %u grains at once", on);
        check(on > CL_CAP_MIN + 2u, t);
        for (h = 0; h < 20; h++) {
            cloud_shed();
            for (j = 0; j < CTL; j++) in[j] = 4000, cl[j] = cr[j] = 0;
            cloud_process(in, cl, cr, CTL);
        }
        for (b = 0; b < 3000; b++) {
            for (j = 0; j < CTL; j++) in[j] = (int32_t)((j * 977u + b * 131u) % 16000u) - 8000, cl[j] = cr[j] = 0;
            cloud_process(in, cl, cr, CTL);
        }
        for (on = 0, j = 0; j < CL_NG; j++) on += cloud_cl.g[j].on;
        snprintf(t, sizeof t, "CLOUD under overload: at most %u grains (%u), never under %u", (unsigned)cl_cap, on, CL_CAP_MIN);
        check(on <= cl_cap && cl_cap == CL_CAP_MIN, t);
        for (h = 0; h < 172u * 8u; h++) cloud_calm_half(0);
        snprintf(t, sizeof t, "calm for ~8 s: all %u grains allowed again (%u)", CL_NG, (unsigned)cl_cap);
        check(cl_cap == CL_NG, t);
        song.g[G_CLDENS] = dens;
    }
    dsu_defaults();
    printf(fails ? "HEX fixes: %d FAILED\n" : "HEX fixes: all ok\n", fails);
    return fails != 0;
}
