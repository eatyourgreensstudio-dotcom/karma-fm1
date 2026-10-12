/* SPDX-License-Identifier: GPL-3.0-only */
/* The firmware in the browser: its UI and its audio as the host tests run them (tests/ui_pages_test.c: the
 * real ui*.c, sequencer, voices, engines and FX on a framebuffer, panel and storage doubles), compiled to
 * WebAssembly and run in an AudioWorklet. The exports are those of Felucca's browser build
 * (web/emu/felucca_web.c of hugelton/Felucca, GPL-3.0, itself after charlesvestal/fm1-x0x), so its page and
 * its worklet drive this one: the device's clock advances with the audio the worklet asks for.
 *   - the panel: buttons and encoders by their printed labels, the keys as fm1_in.notes, the LEDs read back
 *     through the FM-1's key matrix;
 *   - the screen: the harness's 240 x 240 RGB565 frame.
 * Not here (the harness has doubles for them): the flash (projects, user presets and settings are not
 * kept), USB, TRS MIDI, the boot and the power-on LED sweep.
 *   web/emu/build.sh  ->  build/emu/{sloop.wasm, worklet.js, index.html} */
#include <stdint.h>
#define FM1_NCOL 11u
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {      /* as hal/fm1_input.h: key id at (column, row bit) */
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
#define UI_PAGES_HARNESS_ONLY 1
#include "../../tests/ui_pages_test.c"

#define EXPORT __attribute__((used, visibility("default")))
#undef __attribute__

/* output ring, stereo float: a frame of the device (16 ms: 22 blocks of audio, then the UI) at a time */
#define RING 4096u
static float ring[RING * 2];
static uint32_t ring_w, ring_r, web_booted;
static float out_l[1024], out_r[1024];

static void web_frame(void)
{
    static int32_t o[CTL * 2];
    uint32_t q, i;
    for (q = 0; q < 22u; q++) {
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            uint32_t k = (ring_w++ % RING) * 2u;
            ring[k] = (float)o[2u * i] / 32768.0f;
            ring[k + 1] = (float)o[2u * i + 1u] / 32768.0f;
        }
        for (i = 1; i < CTL; i += 2u) {              /* the scope, as audio.c fills it (the visualiser) */
            scope_bufr[scope_w & (SCOPE_N - 1u)] = vis_tap[2u * i + 1u];
            scope_buf[scope_w++ & (SCOPE_N - 1u)] = vis_tap[2u * i];
        }
    }
    ui_input();
    ui_leds();
    ui_draw();
    fm1_ms += 16;
}

EXPORT float *web_out_l(void) { return out_l; }
EXPORT float *web_out_r(void) { return out_r; }
/* n frames (<= 1024) into out_l / out_r: the device runs until they exist */
EXPORT void web_render(uint32_t n)
{
    uint32_t k;
    if (n > 1024u)
        n = 1024u;
    while (web_booted && ring_w - ring_r < n)
        web_frame();
    for (k = 0; k < n; k++) {
        uint32_t i = (ring_r++ % RING) * 2u;
        out_l[k] = ring[i];
        out_r[k] = ring[i + 1];
    }
}

/* the flash: none (the page's saved sectors go nowhere; nothing is written) */
static uint8_t nor[0x100000];
EXPORT uint8_t *web_nor(void) { return nor; }
EXPORT uint32_t web_nor_size(void) { return sizeof nor; }
EXPORT void web_nor_erase(void) { memset(nor, 0xFF, sizeof nor); }
EXPORT uint32_t web_flash_writes_count(void) { return 0; }

/* power on: the factory sounds on the three synth tracks, the default kit, empty patterns, TRACKS */
EXPORT void web_boot(void)
{
    uint32_t i;
    if (web_booted)
        return;
    bank_resolve();
    panel = PANEL_DEFAULT;
    layers_init();
    settings.palette = 4;
    palette_set(4);
    host_tracks_init();
    song.g[G_BPM] = 90;                               /* (as SLOOP powers on) */
    for (i = 0; i < NPART; i++) {
        set_engine_of(&trk[i], TRK_DEF[i][0]);
        apply_preset_to(&trk[i], TRK_DEF[i][1]);
        trk[i].engine = trk[i].eng_req;
    }
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
    go_home();
    ui.force = 1;
    web_booted = 1;
}

/* a short pattern on the four tracks, to hear and to look at (the page's DEMO button); PLAY is the page's */
EXPORT void web_demo(void)
{
    static const uint8_t BEAT[16] = {0x11, 0x10, 0x10, 0x10, 0x14, 0x10, 0x10, 0x21, 0x11, 0x10, 0x01, 0x10, 0x1C, 0x10, 0x10, 0x20};
    static const uint8_t BASS[16] = {36, 0, 0, 36, 0, 0, 39, 0, 36, 0, 0, 43, 0, 41, 0, 0};
    static const uint8_t LEAD[16] = {0, 0, 72, 0, 75, 0, 0, 79, 0, 77, 0, 0, 75, 0, 72, 0};
    uint32_t j;
    for (j = 0; j < NTRK; j++) {
        steps_clear(&trk[j]);
        trk[j].p[P_SLEN] = 16;
    }
    for (j = 0; j < 16u; j++) {
        if (BEAT[j] & 1u) dstep_set(&TDRUM->dstep[j], 0, LV_HARD, 0);
        if (BEAT[j] & 4u) dstep_set(&TDRUM->dstep[j], 2, LV_NORM, 0);
        if (BEAT[j] & 8u) dstep_set(&TDRUM->dstep[j], 3, LV_GHOST, 0);
        if (BEAT[j] & 0x10u) dstep_set(&TDRUM->dstep[j], 4, j % 4u ? LV_SOFT : LV_NORM, j == 14u ? 2u : 0u);
        if (BEAT[j] & 0x20u) dstep_set(&TDRUM->dstep[j], 5, LV_NORM, 0);
        if (BASS[j]) { trk[0].step[j].n = 1; trk[0].step[j].note[0] = BASS[j]; trk[0].step[j].time = ST_NOTE; }
        if (LEAD[j]) { trk[2].step[j].n = 1; trk[2].step[j].note[0] = LEAD[j]; trk[2].step[j].time = ST_NOTE; }
    }
    for (j = 0; j < 16u; j += 8u) {
        trk[1].step[j].n = 3; trk[1].step[j].note[0] = 60; trk[1].step[j].note[1] = 63; trk[1].step[j].note[2] = 67;
        trk[1].step[j].time = ST_NOTE;
    }
    for (j = 0; j < NTRK; j++)
        trk[j].seq_active = 1;
    song.g[G_BPM] = 120;
    ui.force = 1;
}

/* input, by the printed labels: buttons bit b = B_FX .. B_OCTUP (panel.c), keys bit k = key k (F3 .. G5),
 * encoders by role (EN_SELECT .. EN_K4), MASTER 0..1023 */
EXPORT void web_buttons(uint32_t labels)
{
    uint32_t m = 0, b, old = fm1_in.buttons;
    for (b = 0; b < NB; b++)
        if ((labels >> b) & 1u)
            m |= 1u << panel.btn[b];
    edges_btn |= m & ~old;
    fm1_in.buttons = m;
}
EXPORT void web_keys(uint32_t mask) { fm1_in.notes = mask & ((1u << 27) - 1u); }
EXPORT void web_enc(uint32_t role, int32_t n)
{
    if (role < NE)
        encs[panel.enc[role] % 7u] += n * panel.dir[role];
}
EXPORT void web_master(uint32_t v)                    /* (main.c: the pot squared, 0 .. ~4096) */
{
    uint32_t k10 = v > 1023u ? 1023u : v;
    song.master_q12 = (k10 * k10) >> 8;
}
/* a channel message from Web MIDI, as a USB-MIDI packet arrives (usb.c): 0 = the ring was full */
EXPORT uint32_t web_midi(uint32_t status, uint32_t d1, uint32_t d2)
{
    if (status < 0x80u || status >= 0xF0u)
        return 1;
    if (mi_w - mi_r >= MQ)
        return 0;
    midi_in_q[mi_w % MQ] = (status >> 4) | status << 8 | (d1 & 127u) << 16 | (d2 & 127u) << 24;
    mi_w++;
    return 1;
}

/* the LEDs: bit b = the button labelled b lit, keys bit k; the glow (and the backlight) as dim */
static uint32_t led_on(const uint8_t *l, uint32_t id)
{
    uint32_t c, r;
    for (c = 0; c < FM1_NCOL; c++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][c] == (int8_t)id)
                return (l[c] >> r) & 1u;
    return 0;
}
static uint32_t led_buttons(const uint8_t *l)
{
    uint32_t b, m = 0;
    for (b = 0; b < NB; b++)
        m |= led_on(l, panel.btn[b]) << b;
    return m;
}
static uint32_t led_keys(const uint8_t *l)
{
    uint32_t k, m = 0;
    for (k = 0; k < 27u; k++)
        m |= led_on(l, 14u + k) << k;
    return m;
}
EXPORT uint32_t web_lit_buttons(void) { return led_buttons(fm1_led); }
EXPORT uint32_t web_lit_keys(void) { return led_keys(fm1_led); }
EXPORT uint32_t web_dim_buttons(void) { return led_buttons(fm1_led_dim) | led_buttons(fm1_led_bg); }
EXPORT uint32_t web_dim_keys(void) { return led_keys(fm1_led_dim) | led_keys(fm1_led_bg); }
EXPORT uint32_t web_dim_level(void) { return 0; }
EXPORT uint32_t web_breath_buttons(void) { return 0; }
EXPORT uint32_t web_breath_keys(void) { return 0; }

EXPORT uint16_t *web_screen(void) { return screen; }
EXPORT uint32_t web_screen_draws(void) { return fm1_ms; }     /* (a frame drawn: sent again) */
EXPORT uint32_t web_now_ms(void) { return fm1_ms; }
EXPORT uint32_t web_playing(void) { return song.playing; }
EXPORT uint32_t web_sample_rate(void) { return FS; }
