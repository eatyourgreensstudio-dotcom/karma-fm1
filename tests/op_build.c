/* SPDX-License-Identifier: GPL-3.0-only */
/* A second take for posting (2026-10-21, Ariel's): built from scratch on his sounds (the simulator's pattern, its
 * steps cleared): a 4/4 kick put in on the drum GRID, a beat at a time; ACID, a key on SEQ > GEN and the generator
 * writes its loop, the step boxes, the knobs; ELEMENTS the same; PLAITS the same and its pages; the mixer to the end.
 * The same harness and output as op_tour.c (tools/op_tour.sh TAKE=op_build).
 *   op_build OUTDIR PATTERN  ->  OUTDIR/tour.{rgb,pcm} */
#define UI_PAGES_HARNESS_ONLY 1
#include "ui_pages_test.c"
#define PROJ_HOST 1
#include "../firmware/src/project.c"

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



static void to_page(const char *title)
{
    uint32_t k;
    for (k = 0; k < NPAGES; k++)
        if (!strcmp(PAGES[k].title, title)) {
            ui.page = (uint8_t)k;
            break;
        }
    ui.home = 0;
    page_entered();
    ui.force = 1;
    vframe();
}
/* the selected track from nothing: SEQ > GEN, a key (the loop starts on it), DRIFT up a little */
static void gen_from_key(uint32_t white, int oct, double bars)
{
    uint32_t k;
    song.octave = (int8_t)oct;
    open_family(FAM_SEQ);
    to_page("GEN");
    part(0.5, 0, 0, 0, 0);
    tap_key(white);
    song.g[G_GNCHG] = 0;
    for (k = 0; k < 10u; k++) {                      /* DRIFT up on screen (50 of 127) */
        song.g[G_GNCHG] = (int16_t)(song.g[G_GNCHG] + 5);
        ui.hot_col = 1; ui.hot_t = 40;
        vframe();
    }
    part(bars - 0.5, 0, 0, 0, 0);
    song.octave = 0;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/video", *pat = argc > 2 ? argv[2] : "tests/op_build_pattern.fm1p";
    static uint8_t raw[8192];
    static project_t q;
    char p[600];
    uint32_t i, b;
    FILE *pf;
    int n;
    bank_resolve();
    panel = PANEL_DEFAULT;
    layers_init();
    settings.palette = 4;
    palette_set(4);
    host_tracks_init();
    pf = fopen(pat, "rb");                          /* Ariel's sounds, as his simulator kept them */
    assert(pf);
    n = (int)fread(raw, 1, sizeof raw, pf);
    fclose(pf);
    if (!proj_import(&q, raw, n)) {
        printf("%s: not an FM-1 project\n", pat);
        return 1;
    }
    proj_apply(&q, 1);
    for (i = 0; i < NTRK; i++) {
        if (i < NPART)
            trk[i].engine = trk[i].eng_req;
        steps_clear(&trk[i]);                       /* from scratch: the sounds, no steps */
        trk[i].p[P_MUTE] = 0;
        trk[i].seq_active = 1;
    }
    song.batt_raw = 620;
    snprintf(p, sizeof p, "%s/tour.rgb", dir); fv = fopen(p, "wb");
    snprintf(p, sizeof p, "%s/tour.pcm", dir); fa = fopen(p, "wb");
    assert(fv && fa);

    song.sel = TRK_DRUM; go_home(); frames(2);
    studio_open(SC_DRUM); drum_page = 0; drum_lane = 0; drum_cursor = 0; ui.force = 1; frames(2);
    transport_req = 1;
    part(0.25, 0, 0, 0, 0);                         /* 1. the GRID, empty, running */
    for (b = 0; b < 4u; b++) {                      /* the kick on 1, 5, 9, 13: a beat at a time */
        tap_key(4u * b);
        part(0.25 + (b == 3u ? 0.0 : 0.0), 0, 0, 0, 0);
    }
    part(1.75, 0, 0, 0, 0);                         /* the 4/4 kick */
    studio_open(SC_DRUM); drum_page = 0; ui.force = 1; vframe();

    song.sel = 0;                                   /* 2. ACID */
    gen_from_key(0, -1, 2);
    to_page("STEP");                                /* its loop, the boxes */
    part(1, 0, 0, 0, 0);
    open_family(FAM_EDIT);
    part(2, 0, 14, 20, 16);                         /* the knobs */

    song.sel = 1;                                   /* 3. ELEMENTS */
    gen_from_key(4, 0, 2);
    to_page("STEP");
    part(1, 0, 0, 0, 0);
    open_family(FAM_EDIT);
    part(2, 0, 16, 18, 12);

    song.sel = 2;                                   /* 4. PLAITS and its pages */
    gen_from_key(2, 1, 2);
    open_family(FAM_EDIT);
    part(2, 0, 16, 18, 16);
    page_on(1);
    part(1, 0, 10, 10, 0);
    open_family(FAM_ENV);
    part(1, 0, 0, 0, 0);
    open_family(FAM_LFO);
    part(1, 0, 0, 0, 0);
    open_family(FAM_FX);
    part(2, 0, 12, 16, 0);

    go_home();                                      /* 5. the mixer, to the end */
    hold(B_GLO);
    part(4, 0, 0, 0, 0);
    let_go(B_GLO);
    fclose(fv); fclose(fa);
    printf("%u frames, %.2f s\n", (unsigned)nframe, nframe * 22.0 * CTL / FS);
    return 0;
}
