/* SPDX-License-Identifier: GPL-3.0-only */
/* The real UI and audio (the ui_pages_test harness) run from a pipe, for tools/op_emu.py: a page in the
 * browser shows the screen, plays the sound and sends the panel. One command a line on stdin:
 *   B id d    button id (panel.c B_*) down (1) / up (0)
 *   K k d     key k (0..26, F3 up) down / up
 *   E e n     encoder e (panel.c EN_*) turned n detents
 *   F n       run n frames (16 ms each); answers on stdout with the screen (240 x 240 RGB565, little
 *             endian) and then the n frames of audio (16 bit stereo, 44.1 kHz, 22 * CTL samples a frame)
 * The start: the factory sounds, a short demo pattern on the four tracks, stopped, on the TRACKS screen. */
#define UI_PAGES_HARNESS_ONLY 1
#include "ui_pages_test.c"

#define EMU_SPF (22u * CTL)                         /* samples a frame */

static void emu_frame(void)                         /* frame() of the harness, the audio kept */
{
    static int32_t o[CTL * 2];
    static int16_t pcm[EMU_SPF * 2];
    uint32_t q, i, n = 0;
    for (q = 0; q < 22u; q++) {
        mix_block(o, CTL);
        for (i = 0; i < CTL * 2u; i++)
            pcm[n++] = (int16_t)(o[i] > 32767 ? 32767 : o[i] < -32768 ? -32768 : o[i]);
        for (i = 1; i < CTL; i += 2u) {
            scope_bufr[scope_w & (SCOPE_N - 1u)] = vis_tap[2u * i + 1u];
            scope_buf[scope_w++ & (SCOPE_N - 1u)] = vis_tap[2u * i];
        }
    }
    ui_input();
    ui_leds();
    ui_draw();
    fm1_ms += 16;
    fwrite(pcm, sizeof pcm, 1, stdout);
}

static void emu_demo(void)                          /* something to hear and to look at */
{
    static const uint8_t BEAT[16] = {0x11, 0x10, 0x10, 0x10, 0x14, 0x10, 0x10, 0x21, 0x11, 0x10, 0x01, 0x10, 0x1C, 0x10, 0x10, 0x20};
    static const uint8_t BASS[16] = {36, 0, 0, 36, 0, 0, 39, 0, 36, 0, 0, 43, 0, 41, 0, 0};
    static const uint8_t LEAD[16] = {0, 0, 72, 0, 75, 0, 0, 79, 0, 77, 0, 0, 75, 0, 72, 0};
    uint32_t j;
    for (j = 0; j < 16u; j++) {
        memset(&TDRUM->dstep[j], 0, sizeof(dstep_t));
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
}

int main(void)
{
    char line[64];
    uint32_t i;
    static uint8_t out[240 * 240 * 2];
    bank_resolve();
    panel = PANEL_DEFAULT;
    layers_init();
    settings.palette = 4;
    palette_set(4);
    host_tracks_init();
    for (i = 0; i < NPART; i++) {
        set_engine_of(&trk[i], TRK_DEF[i][0]);
        apply_preset_to(&trk[i], TRK_DEF[i][1]);
        trk[i].engine = trk[i].eng_req;
    }
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
    emu_demo();
    go_home();
    ui.force = 1;
    while (fgets(line, sizeof line, stdin)) {
        int a = 0, b = 0;
        char c = 0;
        if (sscanf(line, " %c %d %d", &c, &a, &b) < 2)
            continue;
        if (c == 'B' && a >= 0 && a < (int)NB) {
            if (b) { edges_btn |= BT((uint32_t)a); fm1_in.buttons |= BT((uint32_t)a); }
            else fm1_in.buttons &= ~BT((uint32_t)a);
        } else if (c == 'K' && a >= 0 && a < 27) {
            if (b) fm1_in.notes |= 1u << a;
            else fm1_in.notes &= ~(1u << a);
        } else if (c == 'E' && a >= 0 && a < (int)NE) {
            encs[panel.enc[a]] += b * panel.dir[a];
        } else if (c == 'F') {
            int n = a < 1 ? 1 : a > 8 ? 8 : a;
            for (i = 0; i < 240u * 240u; i++) {        /* (the screen first: its size is fixed) */
                uint16_t p = swap16(screen[i]);
                out[2u * i] = (uint8_t)p;
                out[2u * i + 1u] = (uint8_t)(p >> 8);
            }
            fwrite(out, sizeof out, 1, stdout);
            while (n--)
                emu_frame();
            fflush(stdout);
        }
    }
    return 0;
}
