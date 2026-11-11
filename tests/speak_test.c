/* SPDX-License-Identifier: GPL-3.0-only */
/* The screen reader (ui_speak.c + announce.c, fm1-reader's protocol): a reader says HELLO, then the UI is used
 * as on the FM-1 (ui_pages_test.c's harness) and what KARMA sends is checked: the whole screen first, a sound
 * picked with PRESETS and its place in the list, a page and its knobs, a knob turning, play / stop, the octave,
 * a message; nothing at all before a HELLO. SPEAK_LOG=1 prints every SAY. */
#define KARMA_ANN 1
#define UI_PAGES_HARNESS_ONLY 1
#include "ui_pages_test.c"
#define FELUCCA_VERSION_ANN "KARMA TEST"
#define ANN_NAME FELUCCA_VERSION_ANN
#define ANN_WORDS_URL "https://example.invalid/words/test.json"
#define ANN_MAX 160u
#include "../firmware/src/announce.c"
#include "../firmware/src/ui_speak.c"
#include <stdlib.h>

static char said[64][ANN_MAX + 8];                    /* the SAYs since the last clear */
static uint32_t nsaid, hellos, sent_any, listening;
static int ann_wire_send(const uint8_t *p, uint32_t n)
{
    char t[ANN_MAX + 8];
    uint32_t i, k = 0;
    sent_any++;
    if (n < 7u || p[0] != 0xF0 || p[1] != 0x7D || p[2] != 0x53 || p[3] != 0x52 || p[n - 1u] != 0xF7)
        return -1;
    if (p[4] == 1u) {
        hellos++;
        return 0;
    }
    for (i = 6; i + 1u < n && k < sizeof t - 1u; i++)
        t[k++] = (char)p[i];
    t[k] = 0;
    if (getenv("SPEAK_LOG"))
        printf("    SAY %s\"%s\"\n", p[5] & 1u ? "now " : "", t);
    if (nsaid < 64u)
        str_cpy(said[nsaid++], t, sizeof said[0]);
    return 0;
}
static void hello(void) { static const uint8_t h[] = {0x7D, 0x53, 0x52, 1, 1}; listening = 1; ann_frame(h, sizeof h, fm1_ms); }
static void sframes(uint32_t n)                        /* frames, with the main loop's speak + tick after each */
{
    while (n--) {
        frame();
        if (listening && !(n % 8u))
            hello();                                    /* (the reader says HELLO every ~2 s; often here) */
        speak(fm1_ms);
        ann_tick(fm1_ms);
    }
}
static int heard(const char *s)                        /* a SAY since the last clear contains s */
{
    uint32_t i;
    for (i = 0; i < nsaid; i++)
        if (strstr(said[i], s))
            return 1;
    return 0;
}
static void clear(void) { nsaid = 0; }

int main(void)
{
    fails = 0;
    uint32_t i;
    panel = PANEL_DEFAULT;
    layers_init();
    host_tracks_init();
    for (i = 0; i < NPART; i++) { set_engine_of(&trk[i], TRK_DEF[i][0]); apply_preset_to(&trk[i], TRK_DEF[i][1]); trk[i].engine = trk[i].eng_req; }
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
    go_home(); ui.force = 1;
    frames(30);
    sframes(20);
    check(!sent_any, "screen reader: nothing sent before a reader says HELLO");
    hello();
    sframes(10);
    check(hellos >= 1u, "HELLO answered (name and words link)");
    check(nsaid >= 1u && heard("B P M") && heard("track"), "a new reader hears the whole screen (track, tempo)");
    check(heard("stopped"), "... and that it is stopped");
    check(heard("home. track 1 selected") && heard("swing ") && heard("steps "), "HOME: the track, then SWING LEVEL STEPS PAN (issue #2)");
    clear();
    encs[panel.enc[EN_K3]] = 1; frame(); sframes(20);
    check(heard("steps 17"), "HOME: KNOB 3 turned: \"steps 17\"");
    clear();
    encs[panel.enc[EN_K3]] = -1; frame(); sframes(20);
    clear();
    encs[panel.enc[EN_SELECT]] = 2; frame(); sframes(20);
    check(heard("122 B P M"), "SELECT turned: the tempo");
    clear();
    encs[panel.enc[EN_SELECT]] = -2; frame(); sframes(20);
    clear();
    tap(B_HOME); sframes(20);
    check(heard("visualiser"), "HOME on HOME: \"visualiser\"");
    clear();
    tap(B_HOME); sframes(20);
    check(heard("home. track 1"), "HOME again: back to HOME");
    clear();
    {
        uint32_t s0 = sp_sound_sig();
        encs[panel.enc[EN_PRESET]] = 1; frame(); sframes(20);
        if (getenv("SPEAK_LOG")) printf("    (page %s home %u: sound %u -> %u)\n", cur_page()->title, ui.home, s0, sp_sound_sig());
    }
    check(heard(" of "), "PRESETS turned: the sound and its place in the list (\"3 of 40\")");
    clear();
    encs[panel.enc[EN_ALGO]] = 1; frame(); sframes(20);
    check(heard("track 2"), "ALGORITHM turned: the new track is said");
    clear();
    tap(B_ENV); sframes(20);
    check(nsaid >= 1u, "a page opened: its name and knobs are said");
    clear();
    encs[panel.enc[EN_K1]] = 3; frame(); sframes(20);
    check(nsaid >= 1u, "KNOB 1 turned: its name and value are said");
    clear();
    tap(B_PLAY); sframes(10);
    check(heard("play"), "PLAY: \"play\"");
    clear();
    tap(B_PLAY); sframes(10);
    check(heard("stop"), "PLAY again: \"stop\"");
    clear();
    tap(B_OCTUP); sframes(10);
    check(heard("octave"), "OCT+: the octave");
    clear();
    ui_message("SAVED"); sframes(4);
    check(heard("SAVED"), "a message on the screen is said (\"SAVED\")");
    go_home(); sframes(20);
    boot_t = 0; sp.started = 0; clear();                 /* a reader there while the FM-1 starts */
    sframes(40);
    check(nsaid == 0u, "during the start-up animation: nothing said");
    sframes(120);
    check(heard("home. track") && heard("swing "), "after it: HOME, with its knobs");
    clear();
    {
        static const uint8_t bye[] = {0x7D, 0x53, 0x52, 2};
        uint32_t before;
        listening = 0;
        ann_frame(bye, sizeof bye, fm1_ms);
        before = sent_any;
        encs[panel.enc[EN_PRESET]] = 1; frame(); speak(fm1_ms); ann_tick(fm1_ms); frames(10);
        check(sent_any == before, "after BYE: nothing more is sent");
    }
    if (getenv("SPEAK_DUMP")) {                         /* everything the reader can hear, for fm1-reader's audit */
        FILE *f = fopen(getenv("SPEAK_DUMP"), "w");
        uint32_t t, p, c, n, tot;
        char k[24];
        for (t = 0; t < NTRK; t++) {
            track_select(t);
            for (p = 0; p < NPAGES; p++) {
                ui.page = (uint8_t)p; ui.home = 0; ui.force = 1; frame();
                sp.cols = opk_drawn && ui.layer == LY_PLAY && !ui.menu; opk_drawn = 0;
                sp_screen(0); fprintf(f, "SCREEN\t%s\n", sp_buf);
                for (c = 0; sp.cols && c < 4u; c++) { sp_knob(c, k, sizeof k); if (k[0]) fprintf(f, "KNOB\t%s\n", k); }
            }
            if (!is_drum(TSEL)) {
                tot = pl_total(TSEL);
                for (n = 0; n < tot; n++) {
                    preset_go_to(TSEL, n); frame();
                    sp_buf[0] = 0; sp_sound(); fprintf(f, "ROW\t%s, %u of %u\n", sp_buf, n + 1, tot);
                }
            }
        }
        fclose(f);
    }
    printf("speak: %s\n", fails ? "FAIL" : "all passed");
    return fails != 0;
}
