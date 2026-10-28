/* SPDX-License-Identifier: GPL-3.0-only */
/* Shared scene application for firmware and host audio integration tests. */
static uint32_t arrangement_ready(void)
{
    uint32_t i, ready = 0;
    for (i = 0; i < ARR_SCENES; i++)
        if (proj_ok(&proj_slot[i])) ready |= 1u << i;
    return ready;
}

/* the bars a section's loop takes (its longest pattern: ceil(LEN x step / bar), at least 1): the quick
 * chain (seq.c chain_*) plays each of its sections this long */
static uint32_t section_bars(uint32_t s)
{
    const project_t *p = &proj_slot[s & 3u];
    uint32_t k, bars = 1;
    if (!proj_ok(p))
        return 1;
    for (k = 0; k < NTRK; k++) {
        uint32_t len = (uint32_t)clamp(p->t[k].p[P_SLEN], 1, NSTEP), u = div_units((uint32_t)p->t[k].p[P_SDIV] % NDIV_STEP);
        uint32_t b = (len * u + 4u * BEAT_U - 1u) / (4u * BEAT_U);   /* (64 x 8 beats fits 32 bits) */
        if (b > bars)
            bars = b;
    }
    return bars > 64u ? 64u : bars;
}
static uint32_t arr_scene_bars(uint32_t scene) { return section_bars(scene & 3u); }   /* (arranger.h: a LOOP entry) */

/* Called only at an audio-block boundary, after validation of all slots at
 * start. The song keeps one tempo and global FX across every section (only the drum
 * level and reverb come from it). */
/* edits stick (2026-10-28, Ariel: erased hits came back each time the chain came round): the section playing (live_sec)
 * takes what was changed while it played, as a SAVE + 5..8 would: in the main loop a moment after the panel was last
 * touched, mid-bar (project.c section_keep_ui; at the section change it cost 0.6 ms on the downbeat, measured on the
 * FM-1), and here when the song stops. sec_kept tells the main loop to write it to flash when quiet */
static volatile uint8_t sec_kept;
static void section_keep(void)
{
    int32_t s = live_sec;
    uint32_t was;
    if (s < 0 || s > 3 || !proj_ok(&proj_slot[s]))
        return;
    was = proj_slot[s].sum;
    proj_capture(&proj_slot[s]);
    if (proj_slot[s].sum != was)
        sec_kept |= (uint8_t)(1u << s);
}
static uint32_t prof_keep_max, prof_apply_max;
/* main loop: while a section plays, what was changed in it goes into it 0.3 s after the panel was last touched
 * (input_ms), in a bar's 2nd or 3rd beat (a section changes on a bar: never mid-copy) */
static void section_keep_ui(uint32_t now, uint32_t input_ms)
{
    static uint32_t kept_ms;
    int32_t s = live_sec;
    uint32_t b = clk_beat & 3u, pt0 = PROF_NOW(), was;
    if (!song.playing || s < 0 || s > 3 || input_ms == kept_ms || now - input_ms <= 300u || (b != 1u && b != 2u) ||
        !proj_ok(&proj_slot[s]))
        return;
    kept_ms = input_ms;
    was = proj_slot[s].sum;
    proj_capture(&proj_slot[s]);
    if (proj_slot[s].sum != was)
        sec_kept |= (uint8_t)(1u << s);
    if (PROF_NOW() - pt0 > prof_keep_max)
        prof_keep_max = PROF_NOW() - pt0;
}  /* (console `prof`: a section change's two halves, the worst, ticks) */
static void arrangement_apply(uint32_t scene)
{
    uint32_t k, pt1 = PROF_NOW();
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        seq_release(t);
        trk_all_off(t);
        t->nheld = t->arp_phys = t->arp_note = t->rh_n = t->rskip_n = 0;
        t->rskip_lanes = 0;
    }
    proj_apply(&proj_slot[scene], 0);
    if (PROF_NOW() - pt1 > prof_apply_max)
        prof_apply_max = PROF_NOW() - pt1;
    live_sec = (int8_t)(scene & 3u);                /* (the song's section too: what it keeps, what HOME shows) */
    sync_reload = 1;
    ui.force = 1;
}

/* ---- song mode keeps the loop you made: PLAY in song mode puts it aside (each section then plays
 * over the tracks), STOP (or the song's end) brings it back */
static project_t song_keep __attribute__((section(".pool")));
static uint8_t song_kept;
static int8_t song_keep_sec = -1;              /* the section the loop was, before the song */
static void song_backup(void)                  /* (audio ISR: seq_start) */
{
    proj_capture(&song_keep);
    song_kept = 1;
    song_keep_sec = live_sec;
    live_sec = -1;                              /* (the loop you made is not the song's first section) */
}
static void song_restore(void)                 /* (audio ISR: seq_stop) */
{
    uint32_t i;
    if (!song_kept)
        return;
    section_keep();                             /* (what was changed in the last section played: kept in it) */
    song_kept = 0;
    for (i = 0; i < NTRK; i++) {
        trk_all_off(&trk[i]);
        trk[i].nheld = trk[i].arp_phys = trk[i].arp_note = 0;
    }
    proj_apply(&song_keep, 1);
    live_sec = song_keep_sec;
    song.sel = (uint8_t)(song_keep.sel < NTRK ? song_keep.sel : 0u);
    sync_reload = 1;
    ui.force = 1;
}
