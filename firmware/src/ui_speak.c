/* SPDX-License-Identifier: GPL-3.0-only */
/* The screen reader (KARMA 3.1, GitHub issue #2): what the screen shows, in words, for Charles Vestal's FM-1
 * Screen Reader (github.com/charlesvestal/fm1-reader: a page that speaks it, or hands it to VoiceOver / NVDA).
 * announce.c keeps the protocol and sends nothing until a reader says HELLO. This says what changed, once a
 * UI frame, from what the screen just drew: the page (its name, the track, the sound and the four knobs; the
 * knobs as draw_columns left them in opk), a knob turning, the sound changing, play / stop, the octave. The
 * screen's own short words ("CUT", "ENG") are put into words by the reader with KARMA's words file
 * (firmware/words.tsv, published with the site). Messages (ui_say) are sent where they are shown. */

static char sp_buf[ANN_MAX + 1];                      /* the sentence being built (not on the stack) */
static struct {
    uint32_t ctx, snd;                                 /* the page and the sound last said */
    char kv[4][24];                                    /* each knob as last said: "CUT 13%" */
    uint8_t started, playing, cols;
    int8_t oct;
    int16_t bpm;
} sp;

static void sp_cat(const char *s)
{
    uint32_t n = str_len(sp_buf);
    str_cpy(sp_buf + n, s, sizeof sp_buf - n);
}
static void sp_sep(void)                               /* ", " between items, "" at the start */
{
    uint32_t n = str_len(sp_buf);
    if (n && sp_buf[n - 1u] != ' ')
        sp_cat(n && sp_buf[n - 1u] != '.' ? ", " : " ");
}

/* knob c as the screen shows it: "CUT 13%", "TRACK 1 of 4" ("/4" would be read as a quarter); "" if blank */
static void sp_knob(uint32_t c, char *d, uint32_t cap)
{
    const char *l = opk.lab[c], *v = opk.val[c], *u = opk.unit[c];
    d[0] = 0;
    if (!l[0] || !v[0])
        return;
    str_cpy(d, l, cap);
    if (str_eq(v, "--"))
        return;                                         /* a button (GO) or nothing to show: its name only */
    if (str_eq(v, "-"))
        v = "none";
    str_cpy(d + str_len(d), " ", cap - str_len(d));
    str_cpy(d + str_len(d), v, cap - str_len(d));
    if (u[0] == '/') {
        str_cpy(d + str_len(d), " of ", cap - str_len(d));
        str_cpy(d + str_len(d), u + 1, cap - str_len(d));
    } else if (u[0] && !str_eq(u, l)) {                /* ("BPM 120 BPM": once) */
        if (u[0] != '%')
            str_cpy(d + str_len(d), " ", cap - str_len(d));
        str_cpy(d + str_len(d), u, cap - str_len(d));
    }
}

static void sp_track(void)                             /* "track 2", "drums" */
{
    char n[4];
    if (song.sel == TRK_DRUM) {
        sp_cat("drums");
        return;
    }
    fmt_int(n, (int32_t)song.sel + 1);
    sp_cat("track ");
    sp_cat(n);
}

static void sp_sound_of(const track_t *t)              /* "JUNO SUB 106" (the engine, then the sound) */
{
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    char b[16];
    if (is_drum(t))
        return;
    b[0] = 0;
    if (user_of(t) < UP_SLOTS)
        up_name(user_of(t), b);
    else if (e->npresets)
        str_cpy(b, e->presets[t->preset % e->npresets].name, sizeof b);
    {                                                   /* the engine, unless the sound's name starts with it */
        uint32_t n = str_len(e->name), i, same = 1;
        for (i = 0; i < n; i++)
            same &= b[i] == e->name[i];
        same &= b[n] == ' ' || b[n] == 0;
        if (!same || !b[0]) {
            sp_cat(e->name);
            if (b[0])
                sp_cat(" ");
        }
        sp_cat(b);
    }
}
static void sp_sound(void) { sp_sound_of(TSEL); }

static const char *sp_title(void)                      /* the screen's name, as its header says it */
{
    const page_t *pg = cur_page();
    const engine_t *e = ENGINES[TSEL->eng_req % NENGINES];
    if (ui.menu)
        return "menu";
    if (ui.layer != LY_PLAY)
        return LAYER_NAME[ui.layer % LY_COUNT];
#if FELUCCA_ARRANGER
    if (on_song_page())
        return "song";
#endif
    if (ui.home)
        return "home";
    if (vis_shown())
        return "visualiser. HOME: back";                /* (HOME on HOME: the full-screen visualiser) */
    if (pg->scope == SC_TRK)
        return "home";                                  /* (the TRACKS page: its header says HOME) */
    if (on_drum_page())
        return "drum kit";
    return pg->scope == SC_ENGINE && !is_drum(TSEL) ? e->page_title[pg->id[0] != P_E0] : pg->title;
}

static void sp_kit(void)                               /* the drum track's kit: "HEX BOOM" */
{
    char v[12];
    const char *u;
    param_format(&DRUM_KIT_DESC, TDRUM->p[P_E0], v, &u);
    sp_cat(v);
}

/* the screens that are not a page of four knobs: what a glance at them tells */
static int sp_special(void)
{
    uint32_t i;
    char n[8];
    if (ui.menu || ui.layer != LY_PLAY)
        return 0;
#if FELUCCA_ARRANGER
    if (on_song_page()) {
        if (song_bank) {                                /* (3.0.5) the saved songs */
            sp_cat(". saved songs. song ");
            fmt_int(n, (int32_t)ui.sslot + 1);
            sp_cat(n);
            sp_cat(song_info_of(ui.sslot) ? ", used" : ", empty");
            return 1;
        }
        sp_cat(arrangement_enabled ? ". song mode. " : ". loop mode. ");
        for (i = 0; i < arrangement.count && i < ARR_STEPS; i++) {
            char sec[2] = {(char)('A' + (arrangement.entry[i].scene & 3u)), 0};
            if (i)
                sp_cat(", ");
            sp_cat("part ");
            sp_cat(sec);
            if (arrangement.entry[i].bars) {
                sp_cat(" ");
                fmt_int(n, arrangement.entry[i].bars);
                sp_cat(n);
                sp_cat(arrangement.entry[i].bars == 1u ? " bar" : " bars");
            } else {
                sp_cat(" loop");
            }
            if (str_len(sp_buf) > ANN_MAX - 24u)
                break;
        }
        return 1;
    }
#endif
    if (vis_shown())
        return 1;
    if (!ui.home && cur_page()->scope == SC_TRK) {     /* HOME (the TRACKS page): the selected track, its knobs, the four */
        sp_cat(". ");                               /* "home. track 3 selected." */
        sp_track();
        sp_cat(" selected. ");
        if (sp.cols) {                                  /* its knobs first: SWING LEVEL STEPS PAN (issue #2) */
            char k[24];
            uint32_t c;
            for (c = 0; c < 4u; c++) {
                sp_knob(c, k, sizeof k);
                if (k[0]) {
                    if (c)
                        sp_cat(", ");
                    sp_cat(k);
                }
            }
            sp_cat(". ");
        }
        for (i = 0; i < NTRK; i++) {
            if (i)
                sp_cat(", ");
            if (i == TRK_DRUM) {
                sp_cat("drums ");
                sp_kit();
            } else {
                fmt_int(n, (int32_t)i + 1);
                sp_cat(n);
                sp_cat(" ");
                sp_sound_of(&trk[i]);
            }
        }
        return 1;
    }
    if (on_drum_page()) {                              /* the drum kit screen: the kit */
        sp_cat(". kit ");
        sp_kit();
        return 1;
    }
    return 0;
}

static uint32_t sp_sound_sig(void)
{
    const track_t *t = TSEL;
    return (uint32_t)t->eng_req * 7919u + (uint32_t)t->preset * 31u + user_of(t) * 104729u + song.sel;
}

/* the whole screen: the page, the track and its sound, the knobs, the tempo, playing or not */
static void sp_screen(int all)
{
    uint32_t c;
    char k[24], n[8];
    sp_buf[0] = 0;
    sp_cat(sp_title());
    if (sp_special()) {
    } else if (ui.layer == LY_PLAY && !ui.menu) {
        if (!(is_drum(TSEL) && (str_eq(sp_title(), "DRUMS") || str_eq(sp_title(), "drum kit")))) {
            sp_cat(". ");                               /* (not "drums. drums.") */
            sp_track();
            if (!is_drum(TSEL)) {
                sp_cat(", ");
                sp_sound();
            }
        }
        sp_cat(".");
        if (sp.cols)
            for (c = 0; c < 4u; c++) {
                sp_knob(c, k, sizeof k);
                if (k[0]) {
                    sp_sep();
                    sp_cat(k);
                }
            }
    }
    if (all) {
        sp_sep();
        fmt_int(n, song.g[G_BPM]);
        sp_cat(n);
        sp_cat(" B P M, ");
        sp_cat(song.playing ? "playing" : "stopped");
    }
    if (sp_buf[0] && sp_buf[str_len(sp_buf) - 1u] != '.')
        sp_cat(".");
}

/* once a UI frame, after ui_draw: say what changed */
static void speak(uint32_t now)
{
    uint32_t c, ctx, snd;
    char k[24];
    int all = ann_where();
    if (!ann_on()) {
        sp.started = 0;
        opk_drawn = 0;
        return;
    }
    sp.cols = opk_drawn && ui.layer == LY_PLAY && !ui.menu;
    opk_drawn = 0;
    ctx = (uint32_t)vis_shown() * 50021u + (uint32_t)ui.page * 31u + ui.home * 7u + (uint32_t)ui.layer * 131u + ui.menu * 977u + song.sel * 3u +
          (uint32_t)sp.cols * 15485863u;
#if FELUCCA_ARRANGER
    ctx += (uint32_t)on_song_page() * 7919u;
#endif
    snd = sp_sound_sig();
    if (!sp.started || all) {                          /* a new reader, or it asked: everything */
        sp_screen(1);
        ann_say(sp_buf, 0, ANN_NOW);
    } else if (ctx != sp.ctx) {                        /* a new page: its name and knobs, once it settles */
        sp_screen(0);
        ann_later(sp_buf, ANN_NOW, 200u, now);
    } else if (snd != sp.snd && !is_drum(TSEL)) {      /* PRESETS turned: the sound, and its place in the list */
        uint32_t tot, pos = preset_pos(&tot);
        char n[8];
        sp_buf[0] = 0;
        sp_sound();
        sp_cat(", ");
        fmt_int(n, (int32_t)pos + 1);
        sp_cat(n);
        sp_cat(" of ");
        fmt_int(n, (int32_t)tot);
        sp_cat(n);
        ann_later(sp_buf, ANN_NOW, 120u, now);
    } else if (sp.cols && ui.hot_t) {                  /* a knob turning: its name and value (only while a knob is
                                                        * turned: a drum key played on the KIT screen picks its
                                                        * sound, and is not said) */
        for (c = 0; c < 4u; c++) {
            sp_knob(c, k, sizeof k);
            if (k[0] && !str_eq(k, sp.kv[c])) {
                ann_later(k, ANN_NOW, 120u, now);
                break;
            }
        }
    }
    if (sp.started && song.g[G_BPM] != sp.bpm && !mclk_on()) {   /* SELECT, tap tempo: the tempo (not while a
                                                                    * clock from outside sets it) */
        char n[8];
        sp_buf[0] = 0;
        fmt_int(n, song.g[G_BPM]);
        sp_cat(n);
        sp_cat(" B P M");
        ann_later(sp_buf, ANN_NOW, 200u, now);
    }
    if (sp.started && song.playing != sp.playing)
        ann_say(song.playing ? "play" : "stop", 0, ANN_NOW);
    if (sp.started && song.octave != sp.oct) {
        char n[8];
        sp_buf[0] = 0;
        sp_cat("octave ");
        if (song.octave > 0)
            sp_cat("plus ");
        else if (song.octave < 0)
            sp_cat("minus ");
        fmt_int(n, song.octave < 0 ? -song.octave : song.octave);
        sp_cat(n);
        ann_say(sp_buf, 0, ANN_NOW);
    }
    sp.started = 1;
    sp.ctx = ctx;
    sp.snd = snd;
    sp.playing = song.playing;
    sp.oct = song.octave;
    sp.bpm = song.g[G_BPM];
    if (sp.cols)
        for (c = 0; c < 4u; c++)
            sp_knob(c, sp.kv[c], sizeof sp.kv[c]);
}
