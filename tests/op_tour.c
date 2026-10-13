/* SPDX-License-Identifier: GPL-3.0-only */
/* A tour of the screens in one take, for posting: the real UI and audio (the ui_pages_test harness) play the
 * pattern Ariel had in the simulator (op_tour_pattern.h) while the screens are walked and their knobs turned; each frame (240 x 240 RGB) and
 * its audio (16 bit stereo, 44.1 kHz) go to files, ffmpeg makes the video (tools/op_tour.sh).
 *   op_tour OUTDIR   ->  OUTDIR/tour.{rgb,pcm}
 * The take: the mixer with only the kick; track 2 comes in (its scene, its envelope); the bass (scene, envelope);
 * PLAITS (scene, envelope); the drums (the grid, the pads); the FX wizard, the CLOUD; the mixer; HOME. */
#define UI_PAGES_HARNESS_ONLY 1
#include "ui_pages_test.c"
#include "op_tour_pattern.h"

static FILE *fv, *fa;
static uint32_t nframe;
static void vframe(void)                            /* frame() of the harness, the picture and the audio kept */
{
    static int32_t o[CTL * 2];
    static uint8_t rgb[240 * 240 * 3];
    uint32_t q, i;
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t p = swap16(screen[i]);
        rgb[3u * i] = (uint8_t)((p >> 11) * 255 / 31);
        rgb[3u * i + 1u] = (uint8_t)(((p >> 5) & 63) * 255 / 63);
        rgb[3u * i + 2u] = (uint8_t)((p & 31) * 255 / 31);
    }
    fwrite(rgb, sizeof rgb, 1, fv);
    for (q = 0; q < 22u; q++) {
        int16_t s[CTL * 2];
        mix_block(o, CTL);
        for (i = 0; i < CTL * 2u; i++)
            s[i] = (int16_t)(o[i] > 32767 ? 32767 : o[i] < -32768 ? -32768 : o[i]);
        fwrite(s, sizeof s, 1, fa);
        for (i = 1; i < CTL; i += 2u) {
            scope_bufr[scope_w & (SCOPE_N - 1u)] = vis_tap[2u * i + 1u];
            scope_buf[scope_w++ & (SCOPE_N - 1u)] = vis_tap[2u * i];
        }
    }
    ui_input(); ui_leds(); ui_draw(); fm1_ms += 16;
    nframe++;
}

/* the take is counted in bars (of the pattern's tempo): a part runs until the take is that many bars old, so the cuts fall
 * on the bar. Meanwhile KNOB 1..4 turn k[] detents out over its first half and back over its second; the knobs
 * speed up when turned fast, so the way back may not land where it started: the values are put back at the end
 * (the sound ends as it began) */
static double bars_done;
static void part(double bars, int k1, int k2, int k3, int k4)
{
    const int k[4] = {k1, k2, k3, k4};
    static int16_t keep_p[P_COUNT], keep_g[G_COUNT];
    track_t *kt = &trk[song.sel % NTRK];
    uint8_t keep_ds[2] = {drum_send[drum_lane % DRUM_LANES][0], drum_send[drum_lane % DRUM_LANES][1]};
    uint8_t keep_lane = drum_lane, keep_lv[NTRK];
    uint32_t f0 = nframe, f1, n, i, j;
    int32_t done[4] = {0, 0, 0, 0};
    memcpy(keep_p, kt->p, sizeof keep_p);
    memcpy(keep_g, song.g, sizeof keep_g);
    for (j = 0; j < NTRK; j++)
        keep_lv[j] = (uint8_t)trk[j].p[P_LEVEL];
    bars_done += bars;
    f1 = (uint32_t)(bars_done * 240.0 / song.g[G_BPM] * FS / (22.0 * CTL) + 0.5);
    n = f1 > f0 ? f1 - f0 : 1u;
    ui.force = 1;
    for (i = 0; i < n; i++) {
        for (j = 0; j < 4u; j++) {                  /* a triangle: out and back */
            int32_t h = (int32_t)n / 2, at = (int32_t)i < h ? (int32_t)i : (int32_t)n - (int32_t)i;
            int32_t want = h ? k[j] * at / h : 0;
            if (want != done[j]) {
                encs[panel.enc[EN_K1 + j]] += want - done[j];
                done[j] = want;
            }
        }
        vframe();
    }
    for (j = 0; j < 4u; j++)
        if (done[j])
            encs[panel.enc[EN_K1 + j]] -= done[j];
    if (k1 || k2 || k3 || k4) {
        vframe();
        memcpy(kt->p, keep_p, sizeof keep_p);
        memcpy(song.g, keep_g, sizeof keep_g);
        for (j = 0; j < NTRK; j++)
            trk[j].p[P_LEVEL] = keep_lv[j];
        drum_lane = keep_lane;
        drum_send[drum_lane % DRUM_LANES][0] = keep_ds[0];
        drum_send[drum_lane % DRUM_LANES][1] = keep_ds[1];
        cloud_resync = 1;
    }
}
static void tap_key(uint32_t white)                 /* a white key, a moment */
{
    fm1_in.notes = 1u << key_of_white(white);
    vframe(); vframe(); vframe();
    fm1_in.notes = 0;
    vframe();
}
static void hold(uint32_t b) { edges_btn |= BT(b); fm1_in.buttons |= BT(b); vframe(); }
static void let_go(uint32_t b) { fm1_in.buttons &= ~BT(b); vframe(); }
static void page_on(int n) { encs[panel.enc[EN_SELECT]] += n; vframe(); }


int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/video";
    char p[600];
    uint32_t i;
    bank_resolve();
    panel = PANEL_DEFAULT;
    layers_init();
    settings.palette = 4;
    palette_set(4);
    host_tracks_init();
    _Static_assert(P_COUNT == TOUR_NP && G_COUNT == TOUR_NG && sizeof trk[0].step == 640 && sizeof trk[0].lock == 96 &&
                   sizeof trk[0].fill == 16, "the pattern was read from this layout: read it again after a change");
    for (i = 0; i < NTRK; i++) {                    /* Ariel's pattern, as it stood in his simulator (op_tour_pattern.h) */
        if (i < NPART)
            set_engine_of(&trk[i], TOUR_ENG[2u * i]);
        memcpy(trk[i].p, &TOUR_P[i * TOUR_NP], sizeof trk[i].p);
        trk[i].preset = TOUR_ENG[2u * i + 1u];
        trk[i].engine = trk[i].eng_req;
        memcpy(trk[i].step, &TOUR_STEP[i * 640u], 640);
        memcpy(trk[i].micro, &TOUR_MICRO[i * 64u], 64);
        memcpy(trk[i].lock, &TOUR_LOCK[i * 96u], 96);
        memcpy(trk[i].fill, &TOUR_FILL[i * 16u], 16);
        trk[i].seq_active = 1;
    }
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = TOUR_G[i];
    if (trk[2].p[P_LEVEL] < 40)
        trk[2].p[P_LEVEL] = 64;                     /* (PLAITS stood at 8 of 127 in his mixer: up, to be heard) */
    for (i = 0; i < NPART; i++)
        trk[i].p[P_MUTE] = 1;
    song.batt_raw = 620;
    snprintf(p, sizeof p, "%s/tour.rgb", dir); fv = fopen(p, "wb");
    snprintf(p, sizeof p, "%s/tour.pcm", dir); fa = fopen(p, "wb");
    assert(fv && fa);

    song.sel = TRK_DRUM; go_home(); ui.force = 1;
    frames(2);
    fm1_in.buttons |= BT(B_GLO); edges_btn |= BT(B_GLO); frames(3);   /* the mixer, before the first frame */
    transport_req = 1;
    part(2, 0, 0, 0, 0);                            /* the mixer: the kick alone */
    tap_key(1);                                     /* ELEMENTS in */
    part(1, 0, -14, 0, 0);
    let_go(B_GLO);
    song.sel = 1; open_family(FAM_EDIT);
    part(2, 0, 16, 18, 12);                         /* its scene: the vines, the moon, the rings */
    open_family(FAM_ENV);
    part(1, 8, 10, 0, 12);

    hold(B_GLO);
    tap_key(0);                                     /* the bass in */
    part(1, -14, 0, 0, 0);
    let_go(B_GLO);
    song.sel = 0; open_family(FAM_EDIT);
    part(2, 0, 14, 20, 16);                         /* the Buddha: his rays, the moon, the dust */
    open_family(FAM_ENV);
    part(1, 8, 0, 12, 12);

    hold(B_GLO);
    tap_key(2);                                     /* PLAITS in */
    part(1, 0, 0, -14, 0);
    let_go(B_GLO);
    song.sel = 2; open_family(FAM_EDIT);
    part(2, 0, 16, 18, 16);                         /* the saucer: the planet, the stars, the sparks */
    open_family(FAM_ENV);
    part(1, 8, 10, 0, 12);

    song.sel = TRK_DRUM; studio_open(SC_DRUM); drum_page = 0; drum_lane = 0; drum_cursor = 0;
    part(2, 0, 0, 0, 0);                            /* the grid: the drummer on his kick */
    drum_page = 1;
    part(1, 0, 0, 5, 0);                            /* the pads; the kick's own reverb, up and back */

    go_home(); song.sel = 2; open_family(FAM_FX);
    part(1, 0, 14, 18, 20);                         /* the wizard: chorus, delay, reverb */
    page_on(6); page_on(1);                         /* .. on to CLOUD */
    for (i = 0; i < 80u && song.g[G_CLSEND] < 104; i++) {   /* SEND up (and it stays): the grains */
        if (i % 2u == 0u)
            encs[panel.enc[EN_K1]] += 1;
        vframe();
    }
    part(2, 0, 16, 0, 5);                           /* .. POS, PITCH */

    hold(B_GLO);
    part(1, 0, 0, 0, 0);                            /* the band, all of it */
    let_go(B_GLO);
    song.sel = 0; go_home();
    part(2, 0, 0, 0, 0);                            /* HOME */
    fclose(fv); fclose(fa);
    printf("%u frames, %.2f s\n", (unsigned)nframe, nframe * 22.0 * CTL / FS);
    return 0;
}
