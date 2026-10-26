/* SPDX-License-Identifier: GPL-3.0-only */
/* The third take for posting (2026-10-26, Ariel's direction), on his pattern (tests/op_v3_pattern.fm1p: G mixolydian,
 * 124 BPM, JUNO bass, ELEMENTS, PLAITS lead, a 4/4 kick): HOME with ELEMENTS alone; ELEMENTS' EDIT; the kick's EDIT,
 * the drummer as the kick comes in; the mixer, track 1 comes in; its EDIT; its GEN page; HOME; the mixer, PLAITS
 * fading in; PLAITS' EDIT; FX; LFO; HOME; the KARMA boot screen as the sound fades. Same harness and output as op_tour.c
 * (TAKE=op_v3 sh tools/op_tour.sh). */
#include <stdint.h>
#define FM1_NCOL 11u
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {      /* as hal/fm1_input.h (the LEDs, as web/emu/sloop_web.c reads them) */
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
#define UI_PAGES_HARNESS_ONLY 1
#include "ui_pages_test.c"
#define PROJ_HOST 1
#include "../firmware/src/project.c"

static FILE *fv, *fa, *fs;
/* (the simulator-page video, tools/sim_video.py) each frame: the LEDs as the page draws them (lit / glow, buttons in the
 * page's order, keys), the buttons held, the keys down, what the pointer does (a button clicked, a knob turned) */
static int32_t ptr_btn = -1, ptr_knob = -1, ptr_t;   /* a click on button (page order) / a knob turned (0..7), frames */
static uint32_t led_on(const uint8_t *l, uint32_t id)
{
    uint32_t c, r;
    for (c = 0; c < FM1_NCOL; c++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][c] == (int8_t)id)
                return (l[c] >> r) & 1u;
    return 0;
}
static uint32_t led_b(const uint8_t *l) { uint32_t b, m = 0; for (b = 0; b < NB; b++) m |= led_on(l, panel.btn[b]) << b; return m; }
static uint32_t led_k(const uint8_t *l) { uint32_t k, m = 0; for (k = 0; k < 27u; k++) m |= led_on(l, 14u + k) << k; return m; }
static void st_write(void)
{
    uint32_t v[8], b, held = 0;
    if (!fs)
        return;
    for (b = 0; b < NB; b++)
        held |= ((fm1_in.buttons >> panel.btn[b]) & 1u) << b;
    v[0] = led_b(fm1_led); v[1] = led_k(fm1_led);
    v[2] = led_b(fm1_led_dim) | led_b(fm1_led_bg); v[3] = led_k(fm1_led_dim) | led_k(fm1_led_bg);
    v[4] = held; v[5] = fm1_in.notes;
    v[6] = (uint32_t)(ptr_t > 0 ? ptr_btn : -1); v[7] = (uint32_t)(ptr_t > 0 ? ptr_knob : -1);
    if (ptr_t > 0)
        ptr_t--;
    fwrite(v, sizeof v, 1, fs);
}
static void click_btn(int32_t b) { ptr_btn = b; ptr_knob = -1; ptr_t = 14; }      /* (page order: FX 0 .. OCT+ 13) */
static void turn_knob(int32_t k, int32_t frames) { ptr_knob = k; ptr_btn = -1; ptr_t = frames; }   /* 0 MASTER 1 SELECT
                                                         * 2 PRESETS 3 ALGORITHM 4..7 KNOB 1..4 */
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
    st_write();
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

static void plaits_fade_in(double bars)              /* PLAITS' LEVEL from 0 to its own over `bars` (the mixer shows it rise) */
{
    int16_t to = trk[2].p[P_LEVEL];
    uint32_t f0 = nframe, n, i;
    bars_done += bars;
    n = (uint32_t)(bars_done * 240.0 / song.g[G_BPM] * FS / (22.0 * CTL) + 0.5) - f0;
    trk[2].p[P_MUTE] = 0;
    for (i = 0; i < n; i++) {
        trk[2].p[P_LEVEL] = (int16_t)(to * (int32_t)i / (int32_t)n);
        vframe();
    }
    trk[2].p[P_LEVEL] = to;
}
static void splash_frames(uint32_t n)                /* the boot screen held, the music still playing (it fades in the mix) */
{
    static int32_t o[CTL * 2];
    static uint8_t rgb[240 * 240 * 3];
    uint32_t f, q, i;
    sloop_splash();
    for (f = 0; f < n; f++) {
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
        }
        fm1_ms += 16;
        nframe++;
        st_write();
    }
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/video", *pat = argc > 2 ? argv[2] : "tests/op_v3_pattern.fm1p";
    static uint8_t raw[8192];
    static project_t q;
    char p[600];
    uint32_t i, k;
    FILE *pf;
    int n;
    bank_resolve();
    panel = PANEL_DEFAULT;
    layers_init();
    settings.palette = 4;
    palette_set(4);
    host_tracks_init();
    pf = fopen(pat, "rb");                          /* Ariel's pattern, as his simulator kept it */
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
        trk[i].seq_active = 1;
        trk[i].p[P_MUTE] = i != 1u;                 /* ELEMENTS alone first */
    }
    song.batt_raw = 620;
    snprintf(p, sizeof p, "%s/tour.rgb", dir); fv = fopen(p, "wb");
    snprintf(p, sizeof p, "%s/tour.pcm", dir); fa = fopen(p, "wb");
    snprintf(p, sizeof p, "%s/tour.state", dir); fs = fopen(p, "wb");
    assert(fv && fa);

    song.sel = 1; go_home(); frames(2);
    transport_req = 1;
    part(2, 0, 0, 0, 0);                            /* 1. HOME: ELEMENTS alone */
    click_btn(4); vframe();
    open_family(FAM_EDIT);
    turn_knob(5, 200);
    part(2, 0, 10, 12, 8);                          /* 2. ELEMENTS' EDIT */
    turn_knob(3, 14); vframe();                     /* (ALGORITHM: the track) */
    song.sel = TRK_DRUM; studio_open(SC_DRUM); drum_page = 0; drum_lane = 0; drum_cursor = 0; ui.force = 1;
    vframe();
    TDRUM->p[P_MUTE] = 0;                           /* 3. the drummer, the kick comes in */
    part(2, 0, 0, 0, 0);
    go_home();                                      /* 4. the mixer: track 1 comes in */
    hold(B_GLO);
    vframe(); vframe();
    tap_key(0);
    part(2, 0, 0, 0, 0);
    let_go(B_GLO);
    turn_knob(3, 10); vframe();
    click_btn(4); vframe();
    song.sel = 0; open_family(FAM_EDIT);
    turn_knob(6, 200);
    part(2, 0, 14, 18, 12);                         /* 5. track 1's EDIT */
    click_btn(9); vframe();
    open_family(FAM_SEQ);
    for (k = 0; k < NPAGES; k++)
        if (!strcmp(PAGES[k].title, "GEN"))
            ui.page = (uint8_t)k;
    ui.home = 0; page_entered(); ui.force = 1; vframe();
    part(2, 0, 0, 0, 0);                            /* 6. its GEN page */
    click_btn(6); vframe();
    go_home();
    part(2, 0, 0, 0, 0);                            /* 7. HOME */
    song.sel = 2; go_home();
    hold(B_GLO);
    vframe(); vframe();
    plaits_fade_in(4);                              /* 8. the mixer: PLAITS fading in */
    let_go(B_GLO);
    click_btn(4); vframe();
    open_family(FAM_EDIT);
    turn_knob(5, 200);
    part(2, 0, 10, 12, 8);                          /* 9. PLAITS' EDIT */
    click_btn(0); vframe();
    open_family(FAM_FX);
    turn_knob(6, 200);
    part(2, 0, 12, 16, 0);                          /* 10. FX */
    click_btn(3); vframe();
    open_family(FAM_LFO);
    part(2, 0, 0, 0, 0);                            /* 11. LFO */
    click_btn(6); vframe();
    go_home();
    part(2, 0, 0, 0, 0);                            /* 12. HOME */
    splash_frames(150);                             /* 13. the boot screen, ~2.4 s, as it fades out */
    fclose(fv); fclose(fa);
    printf("%u frames, %.2f s\n", (unsigned)nframe, nframe * 22.0 * CTL / FS);
    return 0;
}
