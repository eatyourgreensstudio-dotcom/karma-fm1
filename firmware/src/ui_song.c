/* SPDX-License-Identifier: GPL-3.0-only */
/* Compact 240 x 240 song-order editor. All changes and flash saves require
 * stopped transport. The four existing project slots are sections A..D. */
static uint8_t song_cursor, song_store_armed, song_load_armed, song_erase_armed;
static uint32_t song_store_deadline;
static uint8_t song_bank;                          /* (3.0.5) SAVE on the SONG screen: the saved songs (SAVE again: back) */
static int on_song_page(void) { return !ui.home && cur_page()->scope == SC_SONG; }
static void song_sane(void)                         /* a bad chain (blank / damaged settings): defaults */
{
    uint32_t i, ok = arrangement.count >= 1u && arrangement.count <= ARR_STEPS;
    for (i = 0; ok && i < arrangement.count; i++)
        ok = arrangement.entry[i].scene < ARR_SCENES && arrangement.entry[i].bars <= 64u;   /* (0: LOOP) */
    if (!ok)
        arr_defaults(&arrangement);
    if (song_cursor >= arrangement.count)
        song_cursor = (uint8_t)(arrangement.count - 1u);
}

/* SONG: the chain of sections (A..D in the track colours), bars of each, where the song is */
static void song_bank_draw(void);
static void song_screen_draw(void)
{
    if (song_bank) {
        song_bank_draw();
        return;
    }
    song_sane();
    uint32_t i, start = song_cursor > 3u ? song_cursor - 3u : 0u;
    static uint32_t previous;
    static const uint16_t SC[4] = {RGB(40, 124, 255), RGB(30, 204, 112), RGB(255, 198, 24), RGB(255, 98, 26)};
    uint32_t sig = song_cursor + 17u * arrangement_enabled + 37u * song.playing +
                   71u * arrangement_clock.index + 127u * arrangement_clock.bar +
                   257u * arrangement.count + 509u * song.g[G_BPM];
    char b[30];
    for (i = 0; i < arrangement.count; i++)
        sig = sig * 31u + arrangement.entry[i].scene + 7u * arrangement.entry[i].bars;
    for (i = 0; i < ARR_SCENES; i++) sig = sig * 3u + (uint32_t)project_used(i);
    if (!ui.force && !ui.msg_t && sig == previous) return;
    previous = ui.msg_t ? ~sig : sig;               /* redraw after a message expires */
    /* Draw one small band at a time: never exceed the 124-row canvas. */
    cv_begin(240, 40, C_BLACK);
    cv_text(4, 4, &FONT_L, "SONG", C_WHITE);
    cv_text(76, 20, &FONT_S, arrangement_enabled ? "song mode" : "loop mode", arrangement_enabled ? SC[3] : RGB(118, 118, 126));
    fmt_int(b, song.g[G_BPM]);
    cv_text(236 - text_w(&FONT_S, b) - 28, 4, &FONT_S, b, C_WHITE);
    cv_text(236 - 24, 4, &FONT_S, "bpm", RGB(118, 118, 126));
    cv_text(236 - text_w(&FONT_S, song.playing ? "playing" : "stopped"), 20, &FONT_S,
            song.playing ? "playing" : "stopped", song.playing ? SC[1] : RGB(118, 118, 126));
    cv_rect(0, 39, 240, 1, RGB(26, 26, 30));
    cv_blit(0, 0);
    for (i = 0; i < 5u; i++) {
        uint32_t pos = start + i;
        cv_begin(240, 26, C_BLACK);
        if (pos < arrangement.count) {
            const arr_entry_t *e = &arrangement.entry[pos];
            int selected = pos == song_cursor, used = project_used(e->scene);
            uint16_t sc = SC[e->scene & 3u];
            int32_t w;
            fmt_int(b, (int32_t)pos + 1);
            cv_text(4, 5, &FONT_S, b, selected ? C_WHITE : RGB(118, 118, 126));
            cv_rect(30, 3, 22, 20, used ? sc : RGB(26, 26, 30));   /* the section tile */
            b[0] = (char)('A' + e->scene); b[1] = 0;
            cv_text(37, 5, &FONT_S, b, used ? C_BLACK : sc);
            {
                uint32_t eb = arr_entry_bars(&arrangement, pos);   /* (LOOP: the section's own length) */
                w = (int32_t)eb * 120 / 64 + 4;             /* its length as a bar */
                cv_rect(60, 9, w, 8, used ? (selected ? sc : RGB(54, 54, 60)) : RGB(26, 26, 30));
                if (!e->bars) {
                    str_cpy(b, "loop ", 8);
                    fmt_int(b + 5, (int32_t)eb);
                    str_cpy(b + str_len(b), "b", 4);
                } else {
                    fmt_int(b, e->bars);
                    str_cpy(b + str_len(b), e->bars == 1 ? " bar" : " bars", 8);
                }
            }
            cv_text(60 + w + 6, 5, &FONT_S, used ? b : "empty", selected ? C_WHITE : RGB(118, 118, 126));
            if (arrangement_clock.running && arrangement_clock.index == pos) {
                cv_rect(60, 19, (int32_t)(arrangement_clock.bar + 1u) * w / (int32_t)arr_entry_bars(&arrangement, pos), 2, C_WHITE);
                cv_rect(226, 9, 9, 9, SC[1]);
            }
            if (selected) cv_rect(0, 1, 240, 1, RGB(54, 54, 60)), cv_rect(0, 24, 240, 1, RGB(54, 54, 60));
        }
        cv_blit(0, 42 + 26u * i);
    }
    cv_begin(240, 68, C_BLACK);
    if (ui.msg_t) {
        cv_rect(0, 4, 240, 30, C_WHITE);
        cv_text((240 - text_w(&FONT_S, ui.msg)) / 2, 11, &FONT_S, ui.msg, C_BLACK);
    } else {
        static const char *const L[4] = {"entry", "section", "bars", "length"};
        for (i = 0; i < 4u; i++) {
            cv_rect((int32_t)i * 60 + 4, 6, 52, 3, SC[i]);
            cv_text((int32_t)i * 60 + 30 - text_w(&FONT_S, L[i]) / 2, 12, &FONT_S, L[i], RGB(118, 118, 126));
        }
    }
    cv_text(4, 30, &FONT_S, "rec: store  oct+ x2: load", RGB(196, 196, 204));
    cv_text(4, 45, &FONT_S, "edit x2: erase  save: songs", RGB(196, 196, 204));
    cv_blit(0, 172);
}

/* (3.0.5) the saved songs, on the SONG screen (SAVE): SONG 1..3, each with its parts; KNOB 1 the song, KNOB 2 ERASE,
 * 3 LOAD, 4 SAVE (GO: twice). A song is the four parts, the order and the loop (project.c song_go) */
static void song_bank_draw(void)
{
    static const uint16_t SC[4] = {RGB(40, 124, 255), RGB(30, 204, 112), RGB(255, 198, 24), RGB(255, 98, 26)};
    static uint32_t previous;
    uint32_t i, sig = 0x5B4Eu + ui.sslot * 7u + 131u * song.playing;
    char b[16];
    for (i = 0; i < NSONG; i++)
        sig = sig * 257u + song_info_of(i);
    if (!ui.force && !ui.msg_t && sig == previous) return;
    previous = ui.msg_t ? ~sig : sig;
    cv_begin(240, 40, C_BLACK);
    cv_text(4, 4, &FONT_L, "SONGS", C_WHITE);
    cv_text(236 - text_w(&FONT_S, "save: back"), 20, &FONT_S, "save: back", RGB(118, 118, 126));
    cv_rect(0, 39, 240, 1, RGB(26, 26, 30));
    cv_blit(0, 0);
    for (i = 0; i < NSONG; i++) {
        uint32_t inf = song_info_of(i), k;
        int sel = i == ui.sslot;
        cv_begin(240, 44, C_BLACK);                     /* (rows 40..171: every row of the screen redrawn) */
        str_cpy(b, "SONG ", sizeof b);
        b[5] = (char)('1' + i);
        b[6] = 0;
        cv_text(14, 14, &FONT_S, b, sel ? C_WHITE : RGB(118, 118, 126));
        if (sel)
            cv_rect(4, 20, 4, 4, C_WHITE), cv_rect(0, 4, 240, 1, RGB(54, 54, 60)), cv_rect(0, 39, 240, 1, RGB(54, 54, 60));
        if (!inf) {
            cv_text(90, 14, &FONT_S, "empty", RGB(118, 118, 126));
        } else {
            for (k = 0; k < 4u; k++) {                  /* its parts, as the SONG screen draws them */
                int used = (inf >> k) & 1u;
                cv_rect(90 + (int32_t)k * 30, 11, 22, 20, used ? SC[k] : RGB(26, 26, 30));
                b[0] = (char)('A' + k);
                b[1] = 0;
                cv_text(97 + (int32_t)k * 30, 13, &FONT_S, b, used ? C_BLACK : SC[k]);
            }
        }
        cv_blit(0, 40 + 44u * i);
    }
    cv_begin(240, 68, C_BLACK);
    if (ui.msg_t) {
        cv_rect(0, 4, 240, 30, C_WHITE);
        cv_text((240 - text_w(&FONT_S, ui.msg)) / 2, 11, &FONT_S, ui.msg, C_BLACK);
    } else {
        static const char *const L[4] = {"song", "erase", "load", "save"};
        for (i = 0; i < 4u; i++) {
            cv_rect((int32_t)i * 60 + 4, 6, 52, 3, SC[i]);
            cv_text((int32_t)i * 60 + 30 - text_w(&FONT_S, L[i]) / 2, 12, &FONT_S, L[i], RGB(118, 118, 126));
        }
    }
    cv_text(4, 34, &FONT_S, "erase load save: twice", RGB(196, 196, 204));
    cv_text(4, 50, &FONT_S, "song: parts order loop", RGB(118, 118, 126));
    cv_blit(0, 172);
}
static void song_bank_input(uint32_t pressed, uint32_t home)
{
    static const char *const GO[3] = {"ERASE", "LOAD", "SAVE"};
    uint32_t k;
    int32_t steps;
    if (home == 1u) { song_bank = 0; go_home(); return; }
    if (pressed & (1u << panel.btn[B_PLAY])) {         /* (PLAY as on the SONG screen) */
        if (!song.playing && arrangement_enabled && !arr_valid(&arrangement, arrangement_ready()))
            ui_message("EMPTY SECTION: REC");
        else
            transport_req = song.playing ? 2 : 1;
    }
    for (k = 0; k < 4u; k++) {
        steps = panel_enc(EN_K1 + k);
        if (!steps) continue;
        if (k == 0u) {
            ui.sslot = (uint8_t)clamp((int32_t)ui.sslot + steps, 0, (int32_t)NSONG - 1);
            ui.arm = 0;
            continue;
        }
        if (steps <= 0) continue;
        if (ui.arm != 0xF0u + k) {                       /* one detent arms, a second one within ~1.5 s acts */
            ui.arm = (uint8_t)(0xF0u + k);
            ui.arm_t = 90;
            ui_say("AGAIN: ", GO[k - 1u]);
            continue;
        }
        ui.arm = 0;
        song_go(k - 1u, ui.sslot);
    }
    panel_enc(EN_SELECT); panel_enc(EN_ALGO); panel_enc(EN_PRESET);
}

/* (3.0.5) the SONG screen's taps of the layer buttons (ui_input.c layer_tap): SAVE the saved songs and back (the
 * order keeps itself: project.c), EDIT twice erases the part at the cursor (SAVE > PROJECT did) */
static void song_save_tap(void)
{
    song_bank ^= 1u;
    ui.arm = 0;
    song_erase_armed = 0;
    ui.force = 1;
}
static void song_edit_tap(void)
{
    uint32_t scene;
    song_sane();
    scene = arrangement.entry[song_cursor].scene & 3u;
    if (song_bank)
        return;
    if (song.playing || transport_req) { ui_message("STOP FIRST"); return; }
    if (!project_used(scene)) { ui_message("EMPTY PART"); return; }
    if (song_erase_armed && (int32_t)(fm1_ms - song_store_deadline) <= 0) {
        song_erase_armed = 0;
        project_erase(scene);
        song_sane();
        return;
    }
    song_erase_armed = 1;
    song_store_armed = song_load_armed = 0;
    song_store_deadline = fm1_ms + 3000u;
    {
        char b[2] = {(char)('A' + scene), 0};
        ui_say("EDIT AGAIN: ERASE ", b);
    }
}

static void song_screen_input(uint32_t pressed, uint32_t home)
{
    uint32_t k, b;
    int32_t steps;
    if (song_bank) {
        song_bank_input(pressed, home);
        return;
    }
    song_sane();
    if (home == 1u) { go_home(); return; }       /* BT_TAP */
    for (k = 0; k < NB; k++) {
        if (!((pressed >> panel.btn[k]) & 1u)) continue;
        b = k;
        if ((b == B_PLAY || b == B_REC) && ft_owns_press())
            continue;                            /* (they close a free take: seq.c) */
        if (b == B_PLAY) {
            if (!song.playing && arrangement_enabled && !arr_valid(&arrangement, arrangement_ready())) {
                ui_message("EMPTY SECTION: REC");
            } else transport_req = song.playing ? 2 : 1;
        } else if (b == B_SEQ) {
            open_family(FAM_SEQ);
        } else if (b == B_SAVE || b == B_REC || b == B_OCTDN || b == B_OCTUP) {
            uint32_t scene = arrangement.entry[song_cursor].scene;
            if (song.playing || transport_req) { ui_message("STOP FIRST"); continue; }
            if (b == B_SAVE) {
                arrangement_save();
                song_store_armed = 0;
            } else if (b == B_OCTDN) {
                arrangement_enabled ^= 1u;
                ui_message(arrangement_enabled ? "SONG MODE" : "LOOP MODE");
            } else if (b == B_OCTUP) {                   /* load: a second press within 3 s (unsaved work goes) */
                if (song_load_armed && (int32_t)(fm1_ms - song_store_deadline) <= 0) {
                    song_load_armed = 0;
                    project_load(scene);
                } else {
                    song_load_armed = 1;
                    song_store_armed = 0;
                    song_store_deadline = fm1_ms + 3000u;
                    ui_message("OCT+ AGAIN: LOAD");
                }
            } else if (project_used(scene) &&
                       (!song_store_armed || (int32_t)(fm1_ms - song_store_deadline) > 0)) {
                song_store_armed = 1;
                song_store_deadline = fm1_ms + 3000u;
                ui_message("REC AGAIN: REPLACE");
            } else {
                project_save(scene);
                settings_save();
                song_store_armed = 0;
            }
        }
    }
    for (k = 0; k < 4u; k++) {
        steps = panel_enc(EN_K1 + k);
        if (!steps) continue;
        song_store_armed = 0;
        song_load_armed = 0;
        song_erase_armed = 0;
        if (k == 0) {
            song_cursor = (uint8_t)clamp(song_cursor + steps, 0, arrangement.count - 1);
            continue;
        }
        if (song.playing || transport_req) { ui_message("STOP FIRST"); continue; }
        if (k == 1) arrangement.entry[song_cursor].scene = (uint8_t)clamp(arrangement.entry[song_cursor].scene + steps, 0, 3);
        if (k == 2) arrangement.entry[song_cursor].bars = (uint8_t)clamp(arrangement.entry[song_cursor].bars + steps, 0, 64);   /* 0: LOOP */
        if (k == 3) {
            arrangement.count = (uint8_t)clamp(arrangement.count + steps, 1, ARR_STEPS);
            if (song_cursor >= arrangement.count) song_cursor = arrangement.count - 1;
        }
    }
    /* Unused encoders have no hidden action on this screen. */
    panel_enc(EN_SELECT); panel_enc(EN_ALGO); panel_enc(EN_PRESET);
}
