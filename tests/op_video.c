/* SPDX-License-Identifier: GPL-3.0-only */
/* Clips of the screens with their sound, for posting: the real UI and audio (the ui_pages_test harness) play a
 * pattern and each frame (240 x 240 RGB) and its audio (16 bit stereo, 44.1 kHz) go to files; ffmpeg makes the
 * videos (tools/op_video.sh).   op_video OUTDIR   ->  OUTDIR/{home,drums,bass}.{rgb,pcm}, 4 bars each,
 * and OUTDIR/all.{rgb,pcm}: the three screens in one take, 4 bars each, the music running on */
#define UI_PAGES_HARNESS_ONLY 1
#include "ui_pages_test.c"

static FILE *fv, *fa;
static void vframe(void)                            /* frame() of the harness, the picture and the audio kept */
{
    static int32_t o[CTL * 2];
    static uint8_t rgb[240 * 240 * 3];
    uint32_t q, i;
    for (i = 0; i < 240u * 240u; i++) {             /* (the screen as it stands while this frame's audio plays) */
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
}
static void clip(const char *dir, const char *name, uint32_t nframes)
{
    char p[600];
    uint32_t i;
    snprintf(p, sizeof p, "%s/%s.rgb", dir, name); fv = fopen(p, "wb");
    snprintf(p, sizeof p, "%s/%s.pcm", dir, name); fa = fopen(p, "wb");
    assert(fv && fa);
    ui.force = 1;
    frames(2);                                      /* the screen drawn, stopped */
    transport_req = 1;
    for (i = 0; i < nframes; i++)
        vframe();
    fclose(fv); fclose(fa);
    transport_req = 2;
    frames(200);                                    /* stopped, the tails out */
}

int main(int argc, char **argv)
{
    /* Ariel's pattern (read off the simulator, 2026-10-12): 122 BPM; track 1 808 SLIDE, 21 steps: B1 x5, rest,
     * B1 B1, B1 for 4, rest, A1 for 4, B1 x3, rest; drums 80S: kick on the beats, hat on the 16ths but the 3rd
     * of each beat, where the open hat is */
    static const int8_t BASS[21] = {35, 35, 35, 35, 35, 0, 35, 35, 35, -1, -1, -1, 0, 33, -1, -1, -1, 35, 35, 35, 0};
    const char *dir = argc > 1 ? argv[1] : "build/video";
    uint32_t i, k, nf = (uint32_t)(4.0 * 4.0 * 60.0 / 122.0 * FS / (22.0 * CTL) + 0.5);
    bank_resolve();
    panel = PANEL_DEFAULT;
    layers_init();
    settings.palette = 4;
    palette_set(4);
    host_tracks_init();
    for (i = 0; i < NPART; i++) { set_engine_of(&trk[i], TRK_DEF[i][0]); apply_preset_to(&trk[i], TRK_DEF[i][1]); trk[i].engine = trk[i].eng_req; }
    for (k = 0; k < ENGINES[0]->npresets; k++)
        if (!strcmp(ENGINES[0]->presets[k].name, "808 SLIDE"))
            apply_preset_to(&trk[0], k);
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
    for (k = 0; k < DRUM_KITS; k++)
        if (!strcmp(DRUM_KIT_NAMES[k], "80S"))
            TDRUM->p[P_E0] = (int16_t)k;
    song.g[G_BPM] = 122;
    song.g[G_DRREV] = 0;
    trk[0].p[P_SLEN] = 21;
    for (i = 0; i < 21u; i++) {
        step_t *st = &trk[0].step[i];
        if (BASS[i] > 0) { st->n = 1; st->note[0] = (uint8_t)BASS[i]; st->time = ST_NOTE; }
        else st->time = BASS[i] < 0 ? ST_TIE : ST_REST;
    }
    for (i = 0; i < 16u; i++) {
        if (i % 4u == 0u) dstep_set(&TDRUM->dstep[i], 0, LV_NORM, 0);
        dstep_set(&TDRUM->dstep[i], i % 4u == 2u ? 5 : 4, LV_NORM, 0);
    }
    for (i = 0; i < NTRK; i++)
        trk[i].seq_active = 1;
    song.batt_raw = 620;                             /* (a full battery in the header) */
    printf("kit %s, sound %s, %u frames a clip\n", DRUM_KIT_NAMES[drum_kit()], ENGINES[0]->presets[trk[0].preset].name, (unsigned)nf);
    song.sel = 0; go_home();
    clip(dir, "home", nf);
    song.sel = TRK_DRUM; studio_open(SC_DRUM); drum_page = 0; drum_lane = 0; drum_cursor = 0;
    clip(dir, "drums", nf);
    go_home(); song.sel = 0; open_family(FAM_EDIT);
    clip(dir, "bass", nf);
    {   /* one take: HOME, the drums, the bass, 4 bars each without a stop */
        char p[600];
        snprintf(p, sizeof p, "%s/all.rgb", dir); fv = fopen(p, "wb");
        snprintf(p, sizeof p, "%s/all.pcm", dir); fa = fopen(p, "wb");
        assert(fv && fa);
        go_home(); song.sel = 0; ui.force = 1;
        frames(2);
        transport_req = 1;
        for (i = 0; i < nf; i++) vframe();
        song.sel = TRK_DRUM; studio_open(SC_DRUM); drum_page = 0; ui.force = 1;
        for (i = 0; i < nf; i++) vframe();
        go_home(); song.sel = 0; open_family(FAM_EDIT); ui.force = 1;
        for (i = 0; i < nf; i++) vframe();
        fclose(fv); fclose(fa);
    }
    return 0;
}
