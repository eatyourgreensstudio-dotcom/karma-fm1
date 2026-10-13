/* SPDX-License-Identifier: GPL-3.0-only */
/* A tour of the screens in one take, for posting: the real UI and audio (the ui_pages_test harness) play the
 * simulator's demo pattern while the screens are walked and their knobs turned; each frame (240 x 240 RGB) and
 * its audio (16 bit stereo, 44.1 kHz) go to files, ffmpeg makes the video (tools/op_tour.sh).
 *   op_tour OUTDIR   ->  OUTDIR/tour.{rgb,pcm}
 * The take: the mixer with only the kick; ELEMENTS comes in (its scene, its envelope); the bass (scene, envelope);
 * PLAITS (scene, envelope); the whole kit (the grid, the pads); the FX wizard, the CLOUD; the mixer; HOME. */
#define UI_PAGES_HARNESS_ONLY 1
#include "ui_pages_test.c"

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

/* the take is counted in bars (120 BPM: 2 s): a part runs until the take is that many bars old, so the cuts fall
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
    f1 = (uint32_t)(bars_done * 2.0 * FS / (22.0 * CTL) + 0.5);
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
    static const uint8_t BEAT[16] = {0x11, 0x10, 0x10, 0x10, 0x14, 0x10, 0x10, 0x21, 0x11, 0x10, 0x01, 0x10, 0x1C, 0x10, 0x10, 0x20};
    static const uint8_t BASS[16] = {36, 0, 0, 36, 0, 0, 39, 0, 36, 0, 0, 43, 0, 41, 0, 0};
    static const uint8_t LEAD[16] = {0, 0, 72, 0, 75, 0, 0, 79, 0, 77, 0, 0, 75, 0, 72, 0};
    static const uint8_t BARS[16] = {60, 0, 0, 67, 0, 0, 72, 0, 63, 0, 0, 67, 0, 0, 70, 0};   /* ELEMENTS: the demo's chord, played out */
    static dstep_t kit[NSTEP];
    const char *dir = argc > 1 ? argv[1] : "build/video";
    char p[600];
    uint32_t i, j;
    bank_resolve();
    panel = PANEL_DEFAULT;
    layers_init();
    settings.palette = 4;
    palette_set(4);
    host_tracks_init();
    for (i = 0; i < NPART; i++) { set_engine_of(&trk[i], TRK_DEF[i][0]); apply_preset_to(&trk[i], TRK_DEF[i][1]); trk[i].engine = trk[i].eng_req; }
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
    for (j = 0; j < NTRK; j++) {
        steps_clear(&trk[j]);
        trk[j].p[P_SLEN] = 16;
        trk[j].seq_active = 1;
    }
    for (j = 0; j < 16u; j++) {                     /* the simulator's demo pattern (web/emu/sloop_web.c web_demo) */
        if (BEAT[j] & 1u) dstep_set(&TDRUM->dstep[j], 0, LV_HARD, 0);
        if (BEAT[j] & 4u) dstep_set(&TDRUM->dstep[j], 2, LV_NORM, 0);
        if (BEAT[j] & 8u) dstep_set(&TDRUM->dstep[j], 3, LV_GHOST, 0);
        if (BEAT[j] & 0x10u) dstep_set(&TDRUM->dstep[j], 4, j % 4u ? LV_SOFT : LV_NORM, j == 14u ? 2u : 0u);
        if (BEAT[j] & 0x20u) dstep_set(&TDRUM->dstep[j], 5, LV_NORM, 0);
        if (BASS[j]) { trk[0].step[j].n = 1; trk[0].step[j].note[0] = BASS[j]; trk[0].step[j].time = ST_NOTE; }
        if (BARS[j]) { trk[1].step[j].n = 1; trk[1].step[j].note[0] = BARS[j]; trk[1].step[j].time = ST_NOTE; }
        if (LEAD[j]) { trk[2].step[j].n = 1; trk[2].step[j].note[0] = LEAD[j]; trk[2].step[j].time = ST_NOTE; }
    }
    memcpy(kit, TDRUM->dstep, sizeof kit);          /* the whole kit, for later: first the kick alone */
    for (j = 0; j < 16u; j++) {
        memset(&TDRUM->dstep[j], 0, sizeof(dstep_t));
        if (BEAT[j] & 1u) dstep_set(&TDRUM->dstep[j], 0, LV_HARD, 0);
    }
    for (i = 0; i < NPART; i++)
        trk[i].p[P_MUTE] = 1;
    song.g[G_BPM] = 120;
    song.g[G_DRREV] = 0;
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

    memcpy(TDRUM->dstep, kit, sizeof kit);          /* the whole kit */
    song.sel = TRK_DRUM; studio_open(SC_DRUM); drum_page = 0; drum_lane = 0; drum_cursor = 0;
    part(2, 4, 0, 0, 0);                            /* the grid (KNOB 1: through the sounds) */
    drum_page = 1;
    part(1, 0, 0, 6, 0);                            /* the pads; the snare's reverb */

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
