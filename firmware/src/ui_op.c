/* SPDX-License-Identifier: GPL-3.0-only */
/* The three bands every LIVE screen shares, top to bottom: the header (a tag that says where you are,
 * the tempo, the transport), the four knob readouts (KNOB 1..4 in blue, green, white, orange: the label
 * and its value in the knob's colour) and the track bar (the four tracks, the selected one filled).
 * The screen's own picture goes between the header and the readouts, rows OP_BODY_Y .. OP_KNOB_Y - 1.
 * Each band is its own canvas with its own signature, as the rest of ui_studio.c. */
#define OP_HEAD_H 22
#define OP_BODY_Y OP_HEAD_H
#define OP_KNOB_Y 154
#define OP_KNOB_H 58
#define OP_BAR_Y 212
#define OP_BAR_H 28
#define OP_PUR RGB(142, 134, 201)        /* line art: what is neither touched nor playing */
#define OP_PUR_D RGB(58, 50, 112)        /* stands, the floor, empty steps */
static int32_t batt_shown(void);         /* ui_draw.c */
static int32_t batt_level(void);
static int32_t meter_px(int32_t a);

static void op_box(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)   /* a 1 px outline */
{
    cv_rect(x, y, w, 1, c);
    cv_rect(x, y + h - 1, w, 1, c);
    cv_rect(x, y, 1, h, c);
    cv_rect(x + w - 1, y, 1, h, c);
}

/* the header: [tag] title | BPM, play / stop, the four beats, REC, the battery */
static void op_header(const char *tag, uint16_t tc, const char *title, uint32_t *cache)
{
    char b[12];
    uint32_t beat = clk_beat, k, playing = song.playing;
    uint32_t sig = studio_hash((uint32_t)song.g[G_BPM] * 7u + playing * 3u + (song.rec != 0) * 1999u +
                               (playing ? beat * 131u : 0u) + (ui.bpm_t != 0) * 104729u + (uint32_t)batt_shown() * 7777u, title);
    sig = studio_hash(sig * 31u + tc, tag);
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, OP_HEAD_H, C_BLACK);
    op_box(2, 1, 38, 18, tc);
    te_text_c(21, 2, tag, tc);
    str_cpy(b, title, 10);                             /* (9 characters: up to the tempo) */
    cv_text(46, 2, &FONT_S, b, TE_G3);
    fmt_int(b, song.g[G_BPM]);
    cv_text(150 - text_w(&FONT_S, b), 2, &FONT_S, b, ui.bpm_t ? C_WHITE : TE_G4);
    if (playing) {
        int32_t i;
        for (i = 0; i < 10; i++)
            cv_rect(156 + i, 4 + i / 2, 1, 12 - i, TE_COL[1]);
    } else {
        cv_rect(156, 5, 10, 10, TE_G3);
    }
    for (k = 0; k < 4u; k++)                           /* the four beats of the bar */
        cv_rect(172 + (int32_t)k * 8, 7, 6, 6, playing && beat % 4u == k ? (k ? C_WHITE : TE_RED) : TE_G2);
    if (song.rec)
        te_disc(209, 10, 4, TE_RED);
    {   /* battery, 3 bars (filling in turn while a USB host charges it) */
        int32_t lvl = batt_shown(), i;
        op_box(217, 5, 17, 10, TE_G3);
        cv_rect(234, 8, 2, 4, TE_G3);
        for (i = 0; i < lvl; i++)
            cv_rect(219 + i * 5, 7, 3, 6, lvl == 1 && batt_level() <= 1 ? TE_RED : TE_G4);
    }
    cv_rect(0, OP_HEAD_H - 1, 240, 1, TE_G1);
    cv_blit(0, 0);
}

/* a value of up to three digits (and a sign): drawn tall and thin */
static int op_is_num(const char *s)
{
    uint32_t n = 0;
    if (*s == '-')
        s++;
    for (; *s; s++, n++)
        if (*s < '0' || *s > '9')
            return 0;
    return n >= 1u && n <= 3u;
}

/* the knob readouts: under each knob its label, a thin gauge and the value (then its unit, small), all
 * in the knob's colour; the knob being turned: its value white, underlined; a value that does nothing
 * now (dim: a bit per knob): grey. A label left empty: an empty column. unit may be 0. A message
 * replaces the band (as te_dials) */
static void op_knobs(const char *const lab[4], const char *const val[4], const char *const *unit, const int32_t ratio[4],
                     uint32_t dim, uint32_t sig, uint32_t *cache)
{
    uint32_t k;
    sig = studio_hash(sig + dim * 977u, ui.msg_t ? ui.msg : "");
    for (k = 0; k < 4u; k++) {
        sig = studio_hash(sig * 7u + (uint32_t)ratio[k] + (ui.hot_t && ui.hot_col == k) * 5003u, lab[k]);
        sig = studio_hash(sig, val[k]);
        if (unit)
            sig = studio_hash(sig, unit[k]);
    }
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, OP_KNOB_H, C_BLACK);
    cv_rect(0, 2, 240, 1, TE_G1);
    if (ui.msg_t) {                                     /* a message: a white bar */
        cv_rect(0, 14, 240, 32, C_WHITE);
        cv_text((240 - text_w(&FONT_S, ui.msg)) / 2, 22, &FONT_S, ui.msg, C_BLACK);
        cv_blit(0, OP_KNOB_Y);
        return;
    }
    for (k = 0; k < 4u; k++) {
        int32_t x0 = 4 + 58 * (int32_t)k, x = x0;
        uint32_t hot = ui.hot_t && ui.hot_col == k;
        uint16_t c = hot ? C_WHITE : (dim >> k) & 1u ? TE_G2 : TE_COL[k];
        const char *v = val[k], *u = unit ? unit[k] : "";
        int32_t uw = u[0] ? text_w(&FONT_S, u) + 3 : 0;
        if (!lab[k][0])
            continue;
        cv_text(x0, 5, &FONT_S, lab[k], TE_COL[k]);
        if (ratio[k] >= 0) {
            cv_rect(x0, 22, 52, 1, TE_G1);
            cv_rect(x0, 22, ratio[k] * 52 / 1000, 1, TE_MID[k]);
        }
        if (op_is_num(v) && (int32_t)str_len(v) * 16 + uw <= 58) {   /* (with a unit that fits beside them) */
            if (*v == '-') {
                cv_rect(x, 39, 7, 2, c);
                x += 10;
                v++;
            }
            for (; *v; v++) {                           /* (a 1 is a bar: it takes less room) */
                int32_t w = *v == '1' ? 4 : 12;
                te_digit(x, 27, w, 26, 2, (uint32_t)(*v - '0'), c);
                x += w + 4;
            }
        } else if (text_w(&FONT_L, v) + uw <= 56) {
            x = cv_text(x0, 24, &FONT_L, v, c);
        } else {
            x = cv_text(x0, 32, &FONT_S, v, c);
        }
        if (uw && x + uw <= x0 + 57)
            cv_text(x + 3, 38, &FONT_S, u, TE_G3);
        if (hot)
            cv_rect(x0, 55, 52, 2, TE_COL[k]);
    }
    cv_blit(0, OP_KNOB_Y);
}

/* the track bar: 1 2 3 DRUM, the selected track filled in its colour; a muted track dim, a soloed one
 * marked white, the one that records marked red */
static void op_trackbar(uint32_t *cache)
{
    static const char *const NAME[NTRK] = {"1", "2", "3", "DRUM"};
    uint32_t i, sig = 1u + song.sel + song.rec * 16u + song.solo * 256u;
    for (i = 0; i < NTRK; i++)
        sig = sig * 3u + (trk_silent(&trk[i]) != 0);
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, OP_BAR_H, C_BLACK);
    for (i = 0; i < NTRK; i++) {
        int32_t x = 4 + 58 * (int32_t)i;
        uint32_t sel = song.sel == i, mute = trk_silent(&trk[i]) != 0;
        uint16_t c = mute && !sel ? TE_DIM[i] : TE_COL[i];
        if (sel)
            cv_rect(x, 3, 55, 22, c);
        else
            op_box(x, 3, 55, 22, c);
        te_text_c(x + 27, 6, NAME[i], sel ? C_BLACK : mute ? TE_G3 : c);
        if ((song.rec >> i) & 1u)
            te_disc(x + 47, 14, 3, TE_RED);
        else if ((song.solo >> i) & 1u)
            cv_rect(x + 45, 11, 6, 6, sel ? C_BLACK : C_WHITE);
    }
    cv_blit(0, OP_BAR_Y);
}

/* ---------------------------------------------------------------- DRUMS --- */
/* the GRID page: a drummer at a kit of the 16 sounds (a piece flashes white on its hit, the sound of
 * KNOB 1 is blue, the sticks go to what is hit), the step in large digits, the 16 x 16 steps of this
 * page in small at the right, then the 16 steps of KNOB 1's sound as the 16 white keys. */
#define OP_SCENE_H 96
#define OP_STEPS_H 36

/* an arc of an ellipse, a0 .. a1 in 1/1024 turns (0 = right, 256 = down), as 2 px chords */
static void op_arc(int32_t cx, int32_t cy, int32_t rx, int32_t ry, uint32_t a0, uint32_t a1, uint16_t c)
{
    int32_t px = 0, py = 0;
    uint32_t a, first = 1;
    for (a = a0;; a += 32u) {
        uint32_t m = (a > a1 ? a1 : a) & 1023u;
        int32_t x = cx + ((SINE[(m + 256u) & 1023u] * rx + 16384) >> 15), y = cy + ((SINE[m] * ry + 16384) >> 15);
        if (!first)
            cv_line(px, py, x, y, c);
        first = 0;
        px = x;
        py = y;
        if (a >= a1)
            break;
    }
}
static void op_ell(int32_t cx, int32_t cy, int32_t rx, int32_t ry, uint16_t c) { op_arc(cx, cy, rx, ry, 0, 1024, c); }
/* a drum seen from the front: the head, the sides, the bottom edge */
static void op_shell(int32_t cx, int32_t cy, int32_t rx, int32_t ry, int32_t d, uint16_t c)
{
    op_ell(cx, cy, rx, ry, c);
    cv_line(cx - rx, cy, cx - rx, cy + d, c);
    cv_line(cx + rx, cy, cx + rx, cy + d, c);
    op_arc(cx, cy + d, rx, ry, 0, 512, c);
}
static void op_stand(int32_t x, int32_t y0)             /* a stand: a pole and two feet on the floor */
{
    cv_line(x, y0, x, 84, OP_PUR_D);
    cv_line(x, 84, x - 8, 92, OP_PUR_D);
    cv_line(x, 84, x + 8, 92, OP_PUR_D);
}

/* where a stick lands for each sound (0: no stick: the kick's pedal, the hat's, the clap, the shaker) */
static const uint8_t KIT_X[DRUM_LANES] = {0, 0, 80, 0, 58, 58, 0, 88, 80, 148, 108, 66, 160, 0, 174, 140};
static const uint8_t KIT_Y[DRUM_LANES] = {0, 0, 54, 0, 38, 34, 0, 56, 54, 56, 35, 13, 25, 0, 58, 36};

static void op_drum_scene(uint32_t bank, uint32_t len)
{
    uint32_t i, j, n, lused = 0, rused = 0;
    int32_t lx = 88, ly = 18, rx = 136, ry = 18;        /* the sticks at rest: up */
    uint16_t c;
#define LIT(a) (pad_lit[a] != 0)
#define SEL(a) (drum_lane == (a))
#define KC(lit, sel) ((lit) ? C_WHITE : (sel) ? TE_COL[0] : OP_PUR)
    cv_begin(240, OP_SCENE_H, C_BLACK);
    for (i = 40; i < 188u; i += 4u)
        cv_pset((int32_t)i, 93, OP_PUR_D);
    /* the step: the one that plays (white), stopped the one of KNOB 2 (green) */
    n = (song.playing ? TDRUM->seq_idx % len : drum_cursor) + 1u;
    c = song.playing ? C_WHITE : TE_COL[1];
    te_digit(4, 4, 14, 30, 2, n / 10u, c);
    te_digit(22, 4, 14, 30, 2, n % 10u, c);
    cv_text(4, 38, &FONT_S, "STEP", TE_G3);
    /* stands first, the kit over them */
    op_stand(54, 45);
    op_stand(78, 70);
    cv_line(62, 17, 68, 30, OP_PUR_D);
    cv_line(166, 29, 160, 40, OP_PUR_D);
    cv_line(95, 86, 91, 92, OP_PUR_D);
    cv_line(129, 86, 133, 92, OP_PUR_D);
    cv_line(138, 80, 136, 92, OP_PUR_D);
    cv_line(162, 80, 164, 92, OP_PUR_D);
    op_ell(62, 14, 14, 3, KC(LIT(11), SEL(11)));                         /* crash */
    op_ell(166, 26, 15, 3, KC(LIT(12), SEL(12)));                        /* ride */
    c = KC(LIT(4) || LIT(5) || LIT(6), SEL(4) || SEL(5) || SEL(6));      /* hi-hat: OPEN lifts the top */
    op_ell(54, LIT(5) ? 35 : 39, 13, 2, c);
    op_ell(54, 43, 13, 2, c);
    c = KC(LIT(2) || LIT(8), SEL(2) || SEL(8));                          /* snare; RIM: its hoop alone */
    op_shell(78, 56, 13, 4, 10, c);
    if (LIT(7) || SEL(7))
        op_ell(78, 56, 13, 4, KC(LIT(7), 1));
    op_ell(112, 68, 21, 21, KC(LIT(0), SEL(0)));                         /* kick, KICK2: the inner ring */
    op_ell(112, 68, 14, 14, KC(LIT(1), SEL(1)));
    op_shell(112, 37, 11, 3, 7, KC(LIT(10), SEL(10)));                   /* rack tom */
    op_shell(150, 58, 13, 4, 20, KC(LIT(9), SEL(9)));                    /* floor tom */
    c = KC(LIT(15), SEL(15));                                            /* cowbell */
    cv_line(136, 35, 144, 35, c);
    cv_line(144, 35, 147, 45, c);
    cv_line(147, 45, 133, 45, c);
    cv_line(133, 45, 136, 35, c);
    c = KC(LIT(14), SEL(14));                                            /* conga */
    op_ell(176, 60, 8, 3, c);
    cv_line(168, 60, 170, 90, c);
    cv_line(184, 60, 182, 90, c);
    cv_line(170, 90, 182, 90, c);
    c = KC(LIT(13), SEL(13));                                            /* shaker: it moves when it sounds */
    op_ell(22, LIT(13) ? 70 : 74, 5, 8, c);
    if (LIT(13)) {
        cv_line(30, 62, 33, 59, c);
        cv_line(31, 70, 35, 70, c);
        cv_line(30, 78, 33, 81, c);
    }
    c = KC(LIT(3), SEL(3));                                              /* clap: two palms, sparks on the hit */
    cv_line(138, 18, 144, 8, c);
    cv_line(LIT(3) ? 140 : 143, 20, LIT(3) ? 146 : 149, 10, c);
    if (LIT(3)) {
        cv_line(150, 7, 153, 4, c);
        cv_line(151, 13, 155, 13, c);
    }
    /* the drummer: headphones, a mouth that opens on the kick */
    op_ell(112, 14, 8, 8, OP_PUR);
    op_arc(112, 14, 10, 10, 576, 960, OP_PUR);
    cv_rect(101, 10, 3, 8, OP_PUR);
    cv_rect(121, 10, 3, 8, OP_PUR);
    cv_rect(108, 12, 2, 2, C_WHITE);
    cv_rect(115, 12, 2, 2, C_WHITE);
    if (LIT(0) || LIT(1))
        op_ell(112, 18, 2, 2, OP_PUR);
    else
        op_arc(112, 16, 3, 2, 0, 512, OP_PUR);
    cv_line(112, 22, 112, 26, OP_PUR);
    cv_line(104, 26, 120, 26, OP_PUR);
    cv_line(104, 26, 98, 32, OP_PUR);
    cv_line(120, 26, 126, 32, OP_PUR);
    for (i = 0; i < DRUM_LANES; i++) {                  /* a stick to each of two hits, the nearer hand first */
        if (!LIT(i) || !KIT_X[i])
            continue;
        if ((KIT_X[i] < 112u && !lused) || rused) {
            if (lused)
                continue;
            lused = 1;
            lx = KIT_X[i];
            ly = KIT_Y[i];
        } else {
            rused = 1;
            rx = KIT_X[i];
            ry = KIT_Y[i];
        }
    }
    cv_line(98, 32, lx, ly, lused ? C_WHITE : TE_DRUM);
    cv_line(126, 32, rx, ry, rused ? C_WHITE : TE_DRUM);
    /* this page of steps in small: 16 sounds down, 16 steps across, the column that plays framed */
    for (i = 0; i < DRUM_LANES; i++)
        for (j = 0; j < 16u; j++) {
            uint32_t p = bank * 16u + j;
            if (p >= len)
                continue;
            cv_rect(190 + (int32_t)j * 3, 4 + (int32_t)i * 3, 2, 2,
                    dstep_has(&TDRUM->dstep[p], i) ? (SEL(i) ? TE_COL[0] : OP_PUR) : SEL(i) ? TE_DIM[0] : RGB(30, 26, 58));
        }
    if (song.playing && TDRUM->seq_idx % len / 16u == bank)
        op_box(189 + (int32_t)(TDRUM->seq_idx % len % 16u) * 3, 3, 4, 50, C_WHITE);
    for (i = 0; i * 16u < len && len > 16u; i++) {      /* the pages of steps (more than one), this one filled */
        if (i == bank)
            cv_rect(190 + (int32_t)i * 8, 58, 6, 6, C_WHITE);
        else
            op_box(190 + (int32_t)i * 8, 58, 6, 6, OP_PUR);
    }
#undef LIT
#undef SEL
#undef KC
    cv_blit(0, OP_BODY_Y);
}

/* the 16 steps of KNOB 1's sound: a hit filled in its level's colour (a notch per ratchet), the cursor
 * framed white, the step that plays underlined */
static void op_drum_steps(uint32_t bank, uint32_t len)
{
    uint32_t j, r;
    cv_begin(240, OP_STEPS_H, C_BLACK);
    for (j = 0; j < 16u; j++) {
        uint32_t p = bank * 16u + j;
        const dstep_t *s = &TDRUM->dstep[p < NSTEP ? p : 0];
        int32_t x = 4 + (int32_t)j * 14 + (int32_t)(j / 4u) * 2;
        if (p >= len)
            continue;
        if (dstep_has(s, drum_lane)) {
            cv_rect(x, 4, 12, 22, lvl_col(dstep_lvl(s, drum_lane)));
            for (r = 0; r < dstep_rat(s, drum_lane); r++)
                cv_rect(x + 2, 7 + (int32_t)r * 4, 8, 2, C_BLACK);
        } else {
            op_box(x, 4, 12, 22, j % 4u ? OP_PUR_D : OP_PUR);
        }
        if (p == drum_cursor)
            op_box(x - 2, 2, 16, 26, C_WHITE);
        if (song.playing && p == TDRUM->seq_idx % len)
            cv_rect(x, 31, 12, 2, C_WHITE);
    }
    cv_blit(0, OP_BODY_Y + OP_SCENE_H);
}

/* ---------------------------------------------------------------- SYNTH --- */
/* a synth track's page without a graph of its own (EDIT, VOICE, the DEST pages...): the track's
 * character in its own scene (below), moved by the sound: the track's meter, its last note, each note
 * as it starts. */
static uint32_t op_ph;                                  /* the wave scrolls while there is sound */
static int32_t op_lvl;                                  /* the meter, falling */

/* one cycle of the wave, ph in 1/1024: kind 0..4 ANALOG's SAW SQR TRI SIN PWM, 5 a sine with more
 * harmonics as morph (0..1000) rises; -32767..32767 */
static int32_t op_wave(uint32_t ph, uint32_t kind, int32_t morph)
{
    int32_t p = (int32_t)(ph & 1023u);
    switch (kind) {
    case 0: return 32767 - p * 64;
    case 1: return p < 512 ? 32767 : -32767;
    case 2: return p < 512 ? -32767 + p * 128 : 32767 - (p - 512) * 128;
    case 3: return SINE[p];
    case 4: return p < 256 ? 32767 : -32767;
    default: return (SINE[p] + SINE[(p * 2) & 1023] * morph / 2000 + SINE[(p * 3) & 1023] * morph / 3000) * 2 / 3;
    }
}

/* the characters, standing on (cx, fy), drawn op_k / 2 times the size they are written at (1.5 in their
 * scenes, 1 on the envelope); up: arms in the air (a jump); c: the track's colour */
static int32_t op_k = 3;
static uint16_t op_ink = OP_PUR;                        /* their line colour (dim: a muted track) */
#define CX(d) (cx + (d) * op_k / 2)
#define FY(d) (fy + (d) * op_k / 2)
#define SZ(n) (((n) * op_k + 1) / 2)
static void op_alien(int32_t cx, int32_t fy, int up, uint16_t c)
{
    cv_line(CX(0), FY(-5), CX(-4), FY(0), op_ink);
    cv_line(CX(0), FY(-5), CX(4), FY(0), op_ink);
    cv_line(CX(0), FY(-13), CX(0), FY(-5), op_ink);
    if (up) {
        cv_line(CX(0), FY(-11), CX(-8), FY(-19), op_ink);
        cv_line(CX(0), FY(-11), CX(8), FY(-19), op_ink);
    } else {
        cv_line(CX(0), FY(-11), CX(-6), FY(-11), op_ink);
        cv_line(CX(-6), FY(-11), CX(-9), FY(-15), op_ink);
        cv_line(CX(0), FY(-11), CX(6), FY(-9), op_ink);
        cv_line(CX(6), FY(-9), CX(9), FY(-5), op_ink);
    }
    op_arc(CX(0), FY(-22), SZ(8), SZ(7), 448, 1088, op_ink);       /* the head: round on top, a pointed chin */
    cv_line(CX(-7), FY(-19), CX(0), FY(-13), op_ink);
    cv_line(CX(7), FY(-19), CX(0), FY(-13), op_ink);
    te_disc(CX(-3), FY(-21), SZ(2), C_WHITE);               /* large eyes, slanted */
    te_disc(CX(3), FY(-21), SZ(2), C_WHITE);
    cv_rect(CX(-5), FY(-23), SZ(2), SZ(2), C_WHITE);
    cv_rect(CX(4), FY(-23), SZ(2), SZ(2), C_WHITE);
    cv_line(CX(-3), FY(-28), CX(-6), FY(-32), op_ink);  /* antennae */
    cv_line(CX(3), FY(-28), CX(6), FY(-32), op_ink);
    te_disc(CX(-7), FY(-33), SZ(1), c);
    te_disc(CX(7), FY(-33), SZ(1), c);
}
static void op_android(int32_t cx, int32_t fy, int up, uint16_t c)
{
    cv_line(CX(-3), FY(0), CX(-3), FY(-6), op_ink);
    cv_line(CX(3), FY(0), CX(3), FY(-6), op_ink);
    op_box(CX(-5), FY(-15), SZ(11), SZ(10), op_ink);
    cv_rect(CX(-1), FY(-11), SZ(3), SZ(3), c);                  /* a light on its chest */
    if (up) {
        cv_line(CX(-5), FY(-12), CX(-10), FY(-18), op_ink);
        cv_line(CX(5), FY(-12), CX(10), FY(-18), op_ink);
    } else {
        cv_line(CX(-5), FY(-12), CX(-10), FY(-9), op_ink);
        cv_line(CX(5), FY(-12), CX(10), FY(-15), op_ink);
        cv_line(CX(10), FY(-15), CX(12), FY(-17), op_ink);   /* a claw */
        cv_line(CX(10), FY(-15), CX(13), FY(-14), op_ink);
    }
    cv_line(CX(0), FY(-15), CX(0), FY(-17), op_ink);
    op_box(CX(-7), FY(-27), SZ(15), SZ(10), op_ink);
    cv_rect(CX(-5), FY(-25), SZ(3), SZ(3), C_WHITE);
    cv_rect(CX(3), FY(-25), SZ(3), SZ(3), C_WHITE);
    cv_rect(CX(-2), FY(-20), SZ(5), SZ(1), op_ink);
    cv_line(CX(0), FY(-27), CX(0), FY(-31), op_ink);
    te_disc(CX(0), FY(-32), SZ(1), c);
}
static void op_dog(int32_t cx, int32_t fy, int up, uint16_t c)
{
    int32_t wag = (ui.frame / 4u) & 1u ? 2 : 0;         /* the tail wags */
    cv_line(CX(-4), FY(-12), CX(-6), FY(-5), op_ink);
    cv_line(CX(-6), FY(-5), CX(-4), FY(0), op_ink);
    cv_line(CX(4), FY(-12), CX(6), FY(-5), op_ink);
    cv_line(CX(6), FY(-5), CX(4), FY(0), op_ink);
    cv_line(CX(-1), FY(0), CX(-1), FY(-5), op_ink);
    cv_line(CX(2), FY(0), CX(2), FY(-5), op_ink);
    cv_line(CX(5), FY(-3), CX(9), FY(-5), op_ink);
    cv_line(CX(9), FY(-5), CX(10 + wag), FY(-11), op_ink);
    cv_rect(CX(-4), FY(-13), SZ(9), SZ(2), c);                  /* a collar */
    op_ell(CX(0), FY(-19), SZ(6), SZ(6), op_ink);
    if (up) {                                           /* ears: down, flying in a jump */
        cv_line(CX(-5), FY(-23), CX(-11), FY(-25), op_ink);
        cv_line(CX(5), FY(-23), CX(11), FY(-25), op_ink);
    } else {
        cv_line(CX(-5), FY(-23), CX(-9), FY(-22), op_ink);
        cv_line(CX(-9), FY(-22), CX(-9), FY(-15), op_ink);
        cv_line(CX(5), FY(-23), CX(9), FY(-22), op_ink);
        cv_line(CX(9), FY(-22), CX(9), FY(-15), op_ink);
    }
    cv_rect(CX(-3), FY(-22), SZ(2), SZ(2), C_WHITE);
    cv_rect(CX(2), FY(-22), SZ(2), SZ(2), C_WHITE);
    op_ell(CX(0), FY(-17), SZ(3), SZ(2), op_ink);                  /* the snout, a tongue out */
    cv_rect(CX(0), FY(-18), SZ(2), SZ(1), op_ink);
    cv_rect(CX(0), FY(-15), SZ(1), SZ(3), TE_COL[3]);
}
/* the drummer, off the kit (the mixer): headphones, a stick in each hand; hl / hr: that stick comes down */
static void op_drummer(int32_t cx, int32_t fy, int hl, int hr)
{
    cv_line(CX(-3), FY(0), CX(-2), FY(-8), op_ink);
    cv_line(CX(3), FY(0), CX(2), FY(-8), op_ink);
    op_box(CX(-3), FY(-16), SZ(7), SZ(9), op_ink);
    op_ell(CX(0), FY(-21), SZ(5), SZ(5), op_ink);
    op_arc(CX(0), FY(-21), SZ(6), SZ(6), 576, 960, op_ink);
    cv_rect(CX(-7), FY(-23), SZ(2), SZ(4), op_ink);
    cv_rect(CX(5), FY(-23), SZ(2), SZ(4), op_ink);
    cv_rect(CX(-2), FY(-22), 1, 1, C_WHITE);
    cv_rect(CX(2), FY(-22), 1, 1, C_WHITE);
    cv_line(CX(-3), FY(-14), CX(-10), hl ? FY(-8) : FY(-21), TE_COL[3]);
    cv_line(CX(3), FY(-14), CX(10), hr ? FY(-8) : FY(-21), TE_COL[3]);
}
#undef CX
#undef FY
#undef SZ

/* the sound of the track, at the top of the graph canvas: engine, preset */
static void op_names(const track_t *t)
{
    char b[16];
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    int32_t x = cv_text(4, 2, &FONT_S, e->name, TE_G4);
    b[0] = 0;
    if (user_of(t) < UP_SLOTS)
        up_name(user_of(t), b);
    else if (e->npresets)
        str_cpy(b, e->presets[t->preset % e->npresets].name, sizeof b);
    cv_text(x + 8, 2, &FONT_S, b, TE_G3);
}

/* the voice whose envelope the ENV page follows: the one earliest in its envelope (a new note first), then
 * the loudest; 0: the track is silent */
static const voice_t *op_env_voice(const track_t *t)
{
    const voice_t *best = 0;
    uint32_t i;
    for (i = 0; i < NVOICE; i++) {
        const voice_t *v = &t->v[i];
        if (!v->active || !v->stage || v->stage > 3u)
            continue;
        if (!best || v->stage < best->stage || (v->stage == best->stage && v->env > best->env))
            best = v;
    }
    return best;
}
/* the track's character at (x, y) on the envelope's line, each with its own way to travel it: the alien on
 * a saucer, the android on its jet, the dog on foot; down: the note is let go (arms up on the way down) */
static void op_env_rider(uint32_t trk_i, int32_t x, int32_t y, int down)
{
    op_k = 2;
    if (trk_i % 3u == 0u) {
        op_ell(x, y - 3, 11, 3, C_WHITE);
        cv_line(x - 6, y - 1, x - 7, y, C_WHITE);
        cv_line(x + 6, y - 1, x + 7, y, C_WHITE);
        op_alien(x, y - 6, down, TE_COL[0]);
    } else if (trk_i % 3u == 1u) {
        cv_line(x - 3, y - 5, x - 4, y, TE_COL[3]);
        cv_line(x + 3, y - 5, x + 4, y, TE_COL[3]);
        op_android(x, y - 5, down, TE_COL[1]);
    } else {
        op_dog(x, y - 1, down, TE_COL[2]);
    }
    op_k = 3;
}

/* The three scenes draw into the graph canvas (240 x 124, ui_draw.c draw_graph). ratio: the four knobs,
 * 0..1000 (-1: none); each owns a part of the picture, in its colour. */

/* synth 1, the alien: it surfs. The wave is the sound's level, its ripples the last note's pitch; a note:
 * a jump and a splash. KNOB 1 the wave's shape (ANALOG's WAVE as it is, else from round to sharp), KNOB 2 a
 * second wave that drifts away, KNOB 3 a moon that rises, KNOB 4 sparks on the wave */
static void op_scene_surf(track_t *t, const int32_t ratio[4], int analog_wave)
{
    uint32_t kind = analog_wave ? (uint32_t)clamp(t->p[P_E0], 0, 4) : 5u;
    uint32_t cyc = (uint32_t)clamp(384 + ((int32_t)op_note - 36) * 13, 256, 1536), rnd = (ui.frame / 2u) * 2654435761u + 1u;
    int32_t morph = ratio[0] < 0 ? 0 : ratio[0], amp = 3 + op_lvl * 17 / 82, x, py = 0, air, y0, y1, i;
#define WY(xx, f, ph) (94 - ((op_wave((uint32_t)(((xx) - 8) * (int32_t)(f) / 56) + (ph), kind, morph) * amp) >> 15))
    for (x = 8; x < 232; x += 8)
        cv_pset(x, 94, OP_PUR_D);
    if (ratio[2] >= 0)                                  /* KNOB 3: the moon */
        op_ell(220, 60 - ratio[2] * 34 / 1000, 5, 5, TE_COL[2]);
    if (ratio[1] > 0)                                   /* KNOB 2: the second wave, further away as it rises */
        for (x = 8; x <= 232; x += 2)
            cv_pset(x, WY(x, cyc + cyc * (uint32_t)ratio[1] / 4000u, op_ph + (uint32_t)ratio[1] / 4u), TE_COL[1]);
    for (x = 8; x <= 232; x += 2) {                     /* KNOB 1: the wave; white as a note starts */
        int32_t y = WY(x, cyc, op_ph);
        if (x > 8)
            cv_line(x - 2, py, x, y, op_hit > 6u ? C_WHITE : TE_COL[0]);
        py = y;
    }
    for (i = 0; ratio[3] > 0 && i < ratio[3] * 30 / 1000; i++) {   /* KNOB 4: sparks */
        rnd = rnd * 1664525u + 1013904223u;
        x = 8 + (int32_t)((rnd >> 8) % 224u);
        cv_pset(x, WY(x, cyc, op_ph) + (int32_t)((rnd >> 20) % 13u) - 6, TE_COL[3]);
    }
    /* the alien on its board: on the wave, in the air for 8 frames after a note */
    air = op_hit ? (SINE[(8 - op_hit) * 64] * 14) >> 15 : 0;
    y0 = WY(104, cyc, op_ph);
    y1 = WY(136, cyc, op_ph);
    cv_line(104, y0 - 1 - air, 136, y1 - 1 - air, C_WHITE);
    if (op_hit > 4u) {                                  /* a splash where it left the water */
        cv_line(99, (y0 + y1) / 2 - 2, 94, (y0 + y1) / 2 - 8, C_WHITE);
        cv_line(141, (y0 + y1) / 2 - 2, 146, (y0 + y1) / 2 - 8, C_WHITE);
    }
    op_alien(120, (y0 + y1) / 2 - 2 - air, air > 3, TE_COL[0]);
#undef WY
}

/* synth 2, the android: it flies. Its height is the last note's pitch (it glides there), so a melody
 * draws its line behind it; a note: a burst of its jet; the stars pass faster the louder the sound.
 * KNOB 1 the trail's wobble, KNOB 2 a planet that grows, KNOB 3 more stars, KNOB 4 a longer flame */
static void op_scene_fly(const int32_t ratio[4])
{
    static uint8_t trail[28];
    static int32_t alt = 96;
    int32_t ty = clamp(114 - ((int32_t)op_note - 36) * 3 / 2, 64, 114), wob = ratio[0] < 0 ? 0 : ratio[0] * 7 / 1000;
    int32_t i, n, px = 0, py = 0, flame;
    alt += (ty - alt) / 3;
    if (!trail[0])
        memset(trail, (uint8_t)alt, sizeof trail);
    if (op_lvl || op_hit) {                             /* the line it leaves moves away to the left */
        for (i = (int32_t)sizeof trail - 1; i > 0; i--)
            trail[i] = trail[i - 1];
        trail[0] = (uint8_t)alt;
    }
    n = ratio[2] < 0 ? 6 : 6 + ratio[2] * 22 / 1000;    /* KNOB 3: the stars, three depths */
    for (i = 0; i < n; i++) {
        uint32_t h = (uint32_t)(i + 1) * 2654435761u;
        int32_t x = (int32_t)(((h >> 4) % 240u + 240u * 64u - (op_ph >> (3u + (uint32_t)i % 3u)) % 240u) % 240u);
        cv_rect(x, 22 + (int32_t)((h >> 14) % 98u), 1 + (i % 5 == 0), 1, TE_COL[2]);
    }
    if (ratio[1] >= 0) {                                /* KNOB 2: the planet and its ring */
        int32_t r = 4 + ratio[1] * 9 / 1000;
        op_ell(206, 44, r, r, TE_COL[1]);
        op_arc(206, 44, r + 6, 2, 0, 512, TE_COL[1]);
        cv_pset(206 - r - 6, 44, TE_COL[1]);
        cv_pset(206 + r + 6, 44, TE_COL[1]);
    }
    for (i = 0; i < (int32_t)sizeof trail; i++) {       /* KNOB 1: the trail */
        int32_t x = 116 - i * 4, y = trail[i] - 18 + ((SINE[((uint32_t)i * 96u + op_ph * 2u) & 1023u] * wob) >> 15);
        if (i)
            cv_line(px, py, x, y, i < 3 && op_hit > 5u ? C_WHITE : TE_COL[0]);
        px = x;
        py = y;
    }
    flame = 5 + (ratio[3] < 0 ? 0 : ratio[3] * 12 / 1000) + op_hit * 2 + op_lvl / 12;   /* KNOB 4: the flame */
    op_box(120, alt - 30, 6, 16, OP_PUR);               /* the jet on its back */
    cv_line(121, alt - 13, 119, alt - 13 + flame, TE_COL[3]);
    cv_line(123, alt - 13, 123, alt - 9 + flame, op_hit > 5u ? C_WHITE : TE_COL[3]);
    cv_line(125, alt - 13, 127, alt - 13 + flame, TE_COL[3]);
    op_android(134, alt, op_hit > 4u, TE_COL[1]);
}

/* synth 3, the dog: it plays fetch. A note throws a ball (the higher the note, the higher its arc) and the
 * dog jumps to catch it; the ground passes under it while there is sound. KNOB 1 the hills, KNOB 2 the
 * grass, KNOB 3 a cloud that rises, KNOB 4 a bigger ball */
static void op_scene_fetch(const int32_t ratio[4])
{
    int32_t hill = ratio[0] < 0 ? 0 : ratio[0] * 16 / 1000, x, py = 0, i, n, air, fy, br, bx, by;
#define GY(xx) (106 - (((SINE[((uint32_t)(xx) * 5u + op_ph) & 1023u] + 32768) * hill) >> 16))
    for (x = 4; x <= 236; x += 4) {                     /* KNOB 1: the hills */
        int32_t y = GY(x);
        if (x > 4)
            cv_line(x - 4, py, x, y, TE_COL[0]);
        py = y;
    }
    n = ratio[1] < 0 ? 0 : ratio[1] * 18 / 1000;        /* KNOB 2: the grass */
    for (i = 0; i < n; i++) {
        uint32_t h = (uint32_t)(i + 1) * 2246822519u;
        x = 4 + (int32_t)(((h >> 6) % 232u + 232u * 64u - (op_ph / 5u) % 232u) % 232u);
        cv_line(x, GY(x) - 1, x - 2, GY(x) - 5, TE_COL[1]);
        cv_line(x, GY(x) - 1, x + 2, GY(x) - 5, TE_COL[1]);
    }
    if (ratio[2] >= 0) {                                /* KNOB 3: the cloud */
        int32_t cy = 58 - ratio[2] * 26 / 1000;
        op_arc(178, cy, 6, 5, 512, 1024, TE_COL[2]);
        op_arc(187, cy - 3, 8, 7, 512, 1024, TE_COL[2]);
        op_arc(197, cy, 6, 5, 512, 1024, TE_COL[2]);
        cv_line(172, cy, 203, cy, TE_COL[2]);
    }
    air = op_hit ? (SINE[(8 - op_hit) * 64] * 12) >> 15 : 0;
    fy = GY(96) - air - (op_lvl && (op_ph / 160u) & 1u ? 1 : 0);
    br = 2 + (ratio[3] < 0 ? 0 : ratio[3] * 3 / 1000);  /* KNOB 4: the ball; in flight for 8 frames, then in its mouth */
    if (op_hit) {
        int32_t u = 8 - (int32_t)op_hit, top = clamp(12 + ((int32_t)op_note - 36) * 2 / 3, 12, 44);
        bx = 232 - (232 - 104) * u / 8;
        by = 92 + (fy - 26 - 92) * u / 8 - ((SINE[u * 64] * top) >> 15);
    } else {
        bx = 104;
        by = fy - 26;
    }
    op_dog(96, fy, air > 3, TE_COL[2]);
    te_disc(bx, by, br, TE_COL[3]);
#undef GY
}

static void op_synth_scene(track_t *t, const int32_t ratio[4], int analog_wave)
{
    int32_t m = meter_px(t->peak);
    char b[8];
    t->peak = 0;
    op_lvl = m > op_lvl ? m : op_lvl > 4 ? op_lvl - 4 : 0;
    if (op_lvl || op_hit)
        op_ph += 40u;
    op_names(t);
    note_name(b, op_note);                              /* the last note, large */
    cv_text(4, 22, &FONT_L, b, op_hit > 5u ? C_WHITE : TE_G4);
    switch ((uint32_t)(t - trk) % 3u) {
    case 0: op_scene_surf(t, ratio, analog_wave); break;
    case 1: op_scene_fly(ratio); break;
    default: op_scene_fetch(ratio); break;
    }
}

/* the pages of the family, the one shown filled (one page: nothing) */
static void op_pages(uint32_t k, uint32_t n, uint32_t *cache)
{
    uint32_t i, sig = 1u + k + n * 16u;
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 8, C_BLACK);
    for (i = 0; i < n && n > 1u; i++) {
        if (i + 1u == k)
            cv_rect(4 + (int32_t)i * 9, 1, 6, 6, C_WHITE);
        else
            op_box(4 + (int32_t)i * 9, 1, 6, 6, OP_PUR);
    }
    cv_blit(0, 146);
}

/* the four tracks' meters, for the screens that show the whole band (the mixer, HOME), once a frame: up at
 * once, falling; a rise: the track's character hops for 6 frames */
static uint8_t op_mx[NTRK], op_hop[NTRK];
static void op_band_tick(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {
        int32_t pk = i == TRK_DRUM ? drums.peak : trk[i].peak, m;
        if (i == TRK_DRUM)
            drums.peak = 0;
        else
            trk[i].peak = 0;
        m = trk_silent(&trk[i]) ? 0 : meter_px(pk);
        if (m > op_mx[i] + 8)
            op_hop[i] = 6;
        else if (op_hop[i])
            op_hop[i]--;
        op_mx[i] = (uint8_t)(m > op_mx[i] ? m : op_mx[i] > 3 ? op_mx[i] - 3 : 0);
    }
}
static void op_band_member(uint32_t i, int32_t cx, int32_t fy, int up, int hl, int hr, uint16_t c)
{
    if (i == 0)
        op_alien(cx, fy, up, c);
    else if (i == 1)
        op_android(cx, fy, up, c);
    else if (i == 2)
        op_dog(cx, fy, up, c);
    else
        op_drummer(cx, fy, hl, hr);
}

/* ------------------------------------------------------------------- FX --- */
/* the FX pages (the sends, and those without a graph: FILTER, INSERT, DLY, REV/CHO): a wizard, a spell a
 * knob from the tip of its wand, each in the knob's colour and as strong as the knob is turned: KNOB 1 a
 * bolt of lightning, KNOB 2 two ribbons that wave, KNOB 3 stars that bounce away, smaller each, KNOB 4
 * rings that spread. The selected track's sound moves them, each note flicks the wand. */
static void op_fx_scene(const int32_t ratio[4])
{
    static uint32_t ph;
    uint32_t sel = song.sel & 3u, live, rnd, j;
    int32_t flick, tx = 112, ty, x, py = 0, i, n;
    op_band_tick();
    live = op_mx[sel] != 0u || op_hop[sel] != 0u;
    if (live)
        ph += 48u;
    flick = op_hop[sel] > 3u ? 5 : op_hop[sel] ? 2 : 0;
    ty = 56 - flick;
    for (x = 8; x < 232; x += 8)
        cv_pset(x, 116, OP_PUR_D);
    /* the wizard: a robe, a beard, a hat with its tip bent, an arm up with the wand */
    cv_line(52, 115, 60, 82, OP_PUR);
    cv_line(60, 82, 80, 82, OP_PUR);
    cv_line(80, 82, 88, 115, OP_PUR);
    cv_line(52, 115, 88, 115, OP_PUR);
    op_ell(70, 71, 8, 8, OP_PUR);
    cv_line(63, 76, 70, 94, C_WHITE);
    cv_line(77, 76, 70, 94, C_WHITE);
    cv_rect(66, 69, 2, 2, C_WHITE);
    cv_rect(73, 69, 2, 2, C_WHITE);
    cv_line(54, 64, 86, 64, OP_PUR);
    cv_line(59, 64, 71, 34, OP_PUR);
    cv_line(71, 34, 81, 64, OP_PUR);
    cv_line(71, 34, 79, 29, OP_PUR);
    cv_rect(78, 26, 1, 5, TE_COL[sel]);                 /* a star at its tip, in the track's colour */
    cv_rect(76, 28, 5, 1, TE_COL[sel]);
    cv_line(60, 86, 52, 98, OP_PUR);
    cv_line(80, 86, 96, 72 - flick, OP_PUR);
    cv_line(96, 72 - flick, tx, ty, C_WHITE);
    if (ratio[3] > 0) {                                 /* KNOB 4: the rings */
        int32_t maxr = 16 + ratio[3] * 30 / 1000;
        n = 1 + ratio[3] * 3 / 1000;
        for (i = 0; i < n; i++) {
            int32_t r = 6 + (int32_t)((ph / 6u + (uint32_t)i * (uint32_t)maxr / (uint32_t)n) % (uint32_t)maxr);
            op_arc(tx, ty, r, r, 800, 1248, r > maxr - 6 ? TE_DIM[3] : TE_COL[3]);
        }
    }
    if (ratio[0] > 0) {                                 /* KNOB 1: the bolt, new at each frame while it sounds */
        int32_t len = 50 + ratio[0] * 64 / 1000, jag = 2 + ratio[0] * 9 / 1000, px = tx + 2;
        rnd = (live ? ui.frame / 2u : 7u) * 2654435761u + 1u;
        py = ty - 2;
        for (i = 1; i <= 6; i++) {
            int32_t nx = tx + 2 + len * i / 6, ny;
            rnd = rnd * 1664525u + 1013904223u;
            ny = ty - 2 - (len * i / 6) * 26 / 114 + (i == 6 ? 0 : (int32_t)((rnd >> 12) % (uint32_t)(2 * jag + 1)) - jag);
            cv_line(px, py, nx, ny, i > 4 && live ? C_WHITE : TE_COL[0]);
            px = nx;
            py = ny;
        }
    }
    if (ratio[1] > 0) {                                 /* KNOB 2: the ribbons */
        int32_t amp = 2 + ratio[1] * 7 / 1000;
        for (j = 0; j < 2u; j++)
            for (x = tx + 6; x <= 232; x += (int32_t)j + 1) {
                int32_t y = ty + 12 + (int32_t)j * 7 + ((SINE[((uint32_t)x * 22u + ph * 2u + j * 300u) & 1023u] * amp) >> 15);
                if (j == 0 && x > tx + 6)
                    cv_line(x - 1, py, x, y, TE_COL[1]);
                else
                    cv_pset(x, y, TE_COL[1]);
                py = y;
            }
    }
    if (ratio[2] > 0) {                                 /* KNOB 3: the stars */
        n = 1 + ratio[2] * 5 / 1000;
        for (i = 0; i < n; i++) {
            int32_t sx = tx + 14 + i * 19, s = 5 - i * 4 / 5, b = SINE[(ph * 3u + (uint32_t)i * 170u) & 1023u];
            int32_t sy = ty + 44 - (((b < 0 ? -b : b) * (9 - i)) >> 15);
            cv_rect(sx - s, sy, 2 * s + 1, 1, TE_COL[2]);
            cv_rect(sx, sy - s, 1, 2 * s + 1, TE_COL[2]);
            if (s > 2) {
                cv_pset(sx - 2, sy - 2, TE_COL[2]);
                cv_pset(sx + 2, sy - 2, TE_COL[2]);
                cv_pset(sx - 2, sy + 2, TE_COL[2]);
                cv_pset(sx + 2, sy + 2, TE_COL[2]);
            }
        }
    }
}

/* ---------------------------------------------------------------- MIXER --- */
/* GLO held: the band on stage, a track a column (as the knobs and the track bar under it). A podium as
 * high as the track's level, its meter inside; its character on top, hopping as the track sounds. Muted:
 * dashed and dim, a red M; soloed: a white S. Under the stage the 16 white keys and what they do here:
 * M mute 1..4, S solo 1..4, F a fill while held, B fill the next bar, T tap the tempo (lit on the beat).
 * flags: 1 a fill now, 2 the next bar is one, 4 it is asked for, 8 the beat. */
static void op_mix_draw(const char *const val[4], const int32_t ratio[4], uint32_t flags)
{
    static const char *const LAB[4] = {"SYN1", "SYN2", "SYN3", "DRUM"};
    static const char KEYS[17] = "MMMMSSSSFB     T";
    static uint32_t head, scene, keys, knobs, bar;
    static uint8_t side;
    const uint8_t *mx = op_mx, *hop = op_hop;
    uint32_t i, sig = 7u, anylit = 0;
    op_header("MIX", C_WHITE, ui.msg_t ? ui.msg : "", &head);   /* (a message: in the title, the levels stay) */
    for (i = 0; i < DRUM_LANES; i++)
        anylit |= pad_lit[i] == 6u;
    if (anylit)
        side ^= 1u;
    op_band_tick();
    for (i = 0; i < NTRK; i++)
        sig = sig * 131u + mx[i] + hop[i] * 97u + (uint32_t)ratio[i] * 7u + (trk[i].p[P_MUTE] != 0) * 3u;
    sig = sig * 31u + song.solo + anylit * 64u + side * 128u;
    if (ui.force || sig != scene) {
        scene = sig;
        cv_begin(240, 96, C_BLACK);
        cv_rect(0, 94, 240, 1, TE_G1);
        op_k = 2;
        for (i = 0; i < NTRK; i++) {
            int32_t cx = 4 + 58 * (int32_t)i + 27, h = 6 + ratio[i] * 46 / 1000, top = 94 - h, y, n = (h - 3) / 4, lit;
            int32_t air = hop[i] ? (SINE[(6 - hop[i]) * 85] * 5) >> 15 : 0;
            uint32_t mute = trk[i].p[P_MUTE] != 0;
            uint16_t c = mute ? TE_DIM[i] : TE_COL[i];
            if (mute) {                                 /* a muted podium: dashed */
                for (y = top; y < 94; y += 4) {
                    cv_rect(cx - 17, y, 1, 2, c);
                    cv_rect(cx + 16, y, 1, 2, c);
                }
                for (y = -17; y < 17; y += 4)
                    cv_rect(cx + y, top, 2, 1, c);
            } else {
                op_box(cx - 17, top, 34, h + 1, c);
            }
            lit = n * mx[i] / 82;
            for (y = 0; y < n; y++)
                cv_rect(cx - 13, 91 - y * 4, 26, y < lit ? 2 : 1, y < lit ? TE_COL[i] : TE_G1);
            op_ink = mute ? OP_PUR_D : OP_PUR;
            op_band_member(i, cx, top - 1 - (i == TRK_DRUM ? 0 : air), air > 2, anylit && !side, anylit && side, c);
            if (mute || (song.solo >> i) & 1u) {
                cv_rect(cx + 14, 2, 12, 14, mute ? TE_RED : C_WHITE);
                cv_text(cx + 16, 1, &FONT_S, mute ? "M" : "S", C_BLACK);
            }
        }
        op_ink = OP_PUR;
        op_k = 3;
        cv_blit(0, OP_BODY_Y);
    }
    sig = flags + song.solo * 16u;
    for (i = 0; i < NTRK; i++)
        sig = sig * 2u + (trk[i].p[P_MUTE] != 0);
    if (ui.force || sig != keys) {                      /* the 16 white keys */
        keys = sig;
        cv_begin(240, 36, C_BLACK);
        for (i = 0; i < 16u; i++) {
            int32_t x = 4 + (int32_t)i * 14 + (int32_t)(i / 4u) * 2;
            uint32_t on = i < 4u ? trk[i].p[P_MUTE] != 0 : i < 8u ? (song.solo >> (i - 4u)) & 1u : i == 8u ? flags & 1u :
                          i == 9u ? (flags >> 1) & 1u : i == 15u ? (flags >> 3) & 1u : 0u;
            uint16_t c = i < 4u ? TE_COL[i] : i < 8u ? TE_MID[i - 4u] : KEYS[i] == ' ' ? OP_PUR_D : TE_G4;
            char ch[2] = {KEYS[i], 0};
            if (on)
                cv_rect(x, 4, 12, 22, i < 4u ? TE_RED : C_WHITE);
            else
                op_box(x, 4, 12, 22, c);
            if (i == 9u && (flags & 4u))
                cv_rect(x, 4, 12, 2, C_WHITE);
            cv_text(x + 2, 7, &FONT_S, ch, on ? C_BLACK : c);
        }
        cv_blit(0, OP_BODY_Y + 96);
    }
    {
        uint8_t m = ui.msg_t;
        ui.msg_t = 0;
        op_knobs(LAB, val, 0, ratio, 0, 4242u, &knobs);
        ui.msg_t = m;
    }
    op_trackbar(&bar);
}

/* ----------------------------------------------------------------- HOME --- */
/* TRACKS, between the header and the knobs. A tape deck: the bar in large digits, two reels that turn
 * while it plays (the tape goes from the left one to the right one over the selected track's loop), the
 * song's sections A..D (the one playing filled). Then the selected track's sound and state, and a lane a
 * track: its character in small (it hops as the track sounds), the 16 steps in view (a synth: each note
 * at its height; the drums: a bar, taller with more sounds), the step that plays, its level. */
static void op_home_body(void)
{
    static uint32_t top_sig, name_sig, lane_sig[NTRK];
    track_t *ts = TSEL;
    uint32_t i, j, playing = song.playing, slen = trk_len(ts), spos = ts->seq_idx % slen, sig;
    uint32_t ready = arrangement_ready();
    op_band_tick();
    sig = playing * 3u + (playing ? ui.frame / 2u * 7919u + spos * 31u : 0u) + ready * 5u + (uint32_t)(live_sec + 1) * 77u + slen * 13u;
    if (ui.force || sig != top_sig) {                   /* the deck */
        uint32_t bar = playing ? spos / 16u + 1u : 1u, a = playing ? (ui.frame * 12u) & 1023u : 0u;
        int32_t k, done = playing ? (int32_t)(spos * 8u / slen) : 0;
        top_sig = sig;
        cv_begin(240, 44, C_BLACK);
        te_digit(4, 7, 12, 28, 2, bar / 10u, playing ? C_WHITE : TE_G3);
        te_digit(20, 7, bar % 10u == 1u ? 4 : 12, 28, 2, bar % 10u, playing ? C_WHITE : TE_G3);   /* (a 1: a bar) */
        cv_text(bar % 10u == 1u ? 30 : 38, 20, &FONT_S, "BAR", TE_G3);
        for (k = 0; k < 2; k++) {
            int32_t cx = 100 + k * 48, pack = k ? 5 + done : 13 - done, sp;
            op_ell(cx, 21, 16, 16, OP_PUR);
            op_ell(cx, 21, pack, pack, TE_COL[3]);      /* the tape on this reel */
            op_ell(cx, 21, 3, 3, OP_PUR);
            for (sp = 0; sp < 3; sp++) {                /* three spokes */
                uint32_t an = (a + (uint32_t)sp * 341u) & 1023u;
                cv_line(cx + ((SINE[(an + 256u) & 1023u] * 4) >> 15), 21 + ((SINE[an] * 4) >> 15),
                        cx + ((SINE[(an + 256u) & 1023u] * 14) >> 15), 21 + ((SINE[an] * 14) >> 15), OP_PUR);
            }
        }
        cv_line(88, 33, 112, 41, TE_COL[3]);            /* the tape, past the head */
        cv_line(112, 41, 136, 41, TE_COL[3]);
        cv_line(136, 41, 160, 33, TE_COL[3]);
        cv_rect(119, 37, 10, 6, C_BLACK);
        op_box(119, 37, 10, 6, playing ? C_WHITE : TE_G3);
        for (i = 0; i < 4u; i++) {                      /* the sections */
            char ch[2] = {(char)('A' + i), 0};
            int32_t x = 188 + (int32_t)i * 12;
            int on = live_sec == (int8_t)i, used = (ready >> i) & 1u;
            if (on)
                cv_rect(x, 12, 11, 18, C_WHITE);
            else
                op_box(x, 12, 11, 18, used ? OP_PUR : OP_PUR_D);
            cv_text(x + 2, 13, &FONT_S, ch, on ? C_BLACK : used ? OP_PUR : OP_PUR_D);
        }
        cv_blit(0, OP_BODY_Y);
    }
    {   /* the selected track: its sound, its engine; REC / SOLO / MUTE */
        char b[24], e[12];
        uint32_t rec = (song.rec >> song.sel) & 1u, solo = (song.solo >> song.sel) & 1u, silent = trk_silent(ts) != 0;
        if (song.sel == TRK_DRUM) {
            str_cpy(b, DRUM_KIT_NAMES[drum_kit()], sizeof b);
            str_cpy(e, "DRUMS", sizeof e);
        } else {
            trk_short_name(song.sel, b);
            str_cpy(e, ENGINES[ts->eng_req % NENGINES]->name, sizeof e);
        }
        b[13] = 0;
        sig = studio_hash(studio_hash(song.sel + rec * 8u + solo * 16u + silent * 32u, b), e);
        if (ui.force || sig != name_sig) {
            int32_t x;
            name_sig = sig;
            cv_begin(240, 16, C_BLACK);
            x = cv_text(4, 0, &FONT_S, b, C_WHITE);
            if (x + 8 + text_w(&FONT_S, e) < 196)
                cv_text(x + 8, 0, &FONT_S, e, TE_COL[song.sel & 3u]);
            if (rec)
                cv_text(236 - text_w(&FONT_S, "REC"), 0, &FONT_S, "REC", TE_RED);
            else if (solo)
                cv_text(236 - text_w(&FONT_S, "SOLO"), 0, &FONT_S, "SOLO", C_WHITE);
            else if (silent)
                cv_text(236 - text_w(&FONT_S, "MUTE"), 0, &FONT_S, "MUTE", TE_G3);
            cv_blit(0, OP_BODY_Y + 44);
        }
    }
    for (i = 0; i < NTRK; i++) {                        /* the lanes */
        track_t *t = &trk[i];
        uint32_t sel = song.sel == i, len = trk_len(t), pos = t->seq_idx % len, bank = playing ? pos / 16u : 0u;
        uint32_t level = i == TRK_DRUM ? (uint32_t)song.g[G_DRLVL] : (uint32_t)t->p[P_LEVEL], silent = trk_silent(t) || !level;
        uint32_t rec = (song.rec >> i) & 1u;
        int32_t lo = 127, hi = 0, air = op_hop[i] > 2u ? 2 : 0;
        uint16_t col = silent ? TE_G2 : sel ? TE_COL[i] : TE_MID[i];
        sig = sel + level * 7u + silent * 997u + rec * 1999u + len * 37u + playing * 3u + (playing ? pos * 71u : 0u) + bank * 13u +
              (uint32_t)air * 5003u + ((song.solo >> i) & 1u) * 4999u;
        for (j = 0; j < len; j++) {
            if (i == TRK_DRUM) {
                sig = sig * 31u + dstep_mask(&t->dstep[j]);
            } else {
                const step_t *st = &t->step[j];
                sig = sig * 31u + (step_on(st) ? st->note[0] + 1u : 0u);
                if (step_on(st) && st->note[0] < lo)
                    lo = st->note[0];
                if (step_on(st) && st->note[0] > hi)
                    hi = st->note[0];
            }
        }
        if (!ui.force && sig == lane_sig[i])
            continue;
        lane_sig[i] = sig;
        if (hi - lo < 4)
            hi = lo + 4;
        cv_begin(240, 18, C_BLACK);
        if (sel)
            op_box(24, 0, 214, 18, TE_COL[i]);
        op_k = 1;                                       /* the character, half size */
        op_ink = silent ? OP_PUR_D : sel ? C_WHITE : OP_PUR;
        op_band_member(i, 12, 17 - air, air != 0, air != 0, 0, silent ? TE_G2 : TE_COL[i]);
        op_ink = OP_PUR;
        op_k = 3;
        for (j = 0; j < 16u; j++) {
            uint32_t p = bank * 16u + j;
            int32_t x = 28 + (int32_t)j * 11;
            if (p >= len)
                continue;
            if (i == TRK_DRUM) {                        /* a bar: 3 px a sound, up to 4 */
                uint32_t m = dstep_mask(&t->dstep[p]), n = 0;
                for (; m; m &= m - 1u)
                    n++;
                if (n)
                    cv_rect(x + 2, 14 - (int32_t)(n > 4u ? 4u : n) * 3, 5, (int32_t)(n > 4u ? 4u : n) * 3, col);
                else
                    cv_rect(x + 4, 13, 1, 1, TE_G2);
            } else if (step_on(&t->step[p])) {          /* a note at its height (the first of a chord) */
                cv_rect(x, 13 - (t->step[p].note[0] - lo) * 9 / (hi - lo), 9, 2, col);
                if (t->step[p].n > 1)
                    cv_rect(x, 9 - (t->step[p].note[0] - lo) * 9 / (hi - lo), 9, 1, col);
            } else if (t->step[p].time == ST_TIE) {
                cv_rect(x, 13, 9, 1, TE_G2);
            } else {
                cv_rect(x + 4, 13, 1, 1, TE_G2);
            }
            if (playing && p == pos)
                cv_rect(x, 15, 9, 2, C_WHITE);
        }
        cv_rect(207, 8, 28, 3, TE_G1);                  /* the level; REC: a red dot */
        if (!silent)
            cv_rect(207, 8, (int32_t)level * 28 / 127, 3, col);
        if (rec)
            te_disc(232, 4, 2, TE_RED);
        cv_blit(0, OP_BODY_Y + 60 + i * 18);
    }
}
