/* SPDX-License-Identifier: GPL-3.0-only */
/* ACID GEN (firmware/src/acid_gen.c) against X0X's TB-3PO (firmware/src/seq/tb3po.c of github.com/charlesvestal/fm1-x0x,
 * in the reference checkout): the same settings and seed must write the same line, step by step (note, rest, accent,
 * slide), and a VARY the same changes.
 *   cc -O1 -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal -Itests -I$X0X/firmware/src/seq -o build/host/acid_gen_test \
 *      tests/acid_gen_test.c -lm      (X0X: ~/fm1-firmware/ref/fm1-x0x) */
#define UI_PAGES_HARNESS_ONLY 1
#include "ui_pages_test.c"
#undef NSTEPS
#include "tb3po.c"

int main(void)
{
    static const uint8_t SET[6][7] = {   /* density, accent, slide, oct range, root, scale, length */
        {70, 40, 25, 2, 9, 0, 16}, {100, 0, 0, 1, 0, 5, 16}, {30, 80, 60, 3, 4, 3, 32}, {55, 25, 50, 2, 11, 1, 64},
        {90, 50, 10, 1, 2, 2, 7}, {10, 10, 90, 3, 7, 4, 13}};
    int c, s, fails = 0, notes = 0;
    host_tracks_init();
    for (c = 0; c < 6; c++)
        for (s = 0; s < 40; s++) {
            static bpart_t b;
            ag_cfg_t g;
            track_t *t = &trk[0];
            uint32_t r1 = 0, r2 = 0;
            int i, bad = 0, v;
            memset(&b, 0, sizeof b);
            g.density = b.gen.density = SET[c][0];
            g.accent = b.gen.accent = SET[c][1];
            g.slide = b.gen.slide = SET[c][2];
            g.oct_range = b.gen.oct_range = SET[c][3];
            g.root = b.gen.root = SET[c][4];
            g.scale = b.gen.scale = SET[c][5];
            g.base_oct = b.gen.base_oct = 2;
            g.seed = b.gen.seed = 0x9E3779B9u * (uint32_t)(s + 1);
            b.len = SET[c][6];
            tb3po_generate(&b);
            ag_generate(t, &g, SET[c][6]);
            for (v = 0; v < 3; v++) {                    /* the line, then after one and two VARY */
                for (i = 0; i < SET[c][6]; i++) {
                    int gate = bstep_gate(&b.step[i]) != G_REST, f = b.step[i].flags;
                    uint8_t k = ag_kind_of(&t->step[i]);
                    int want = !gate ? AG_REST : (f & BS_SLIDE) ? AG_SLIDE : (f & BS_ACCENT) ? AG_ACC : AG_NOTE;
                    if (k != want || (gate && t->step[i].note[0] != b.step[i].note))
                        bad++;
                    notes += gate;
                }
                tb3po_mutate(&b, &r1);
                ag_mutate(t, &g, SET[c][6], &r2);
            }
            if (bad) {
                printf("settings %d seed %d: %d steps differ\n", c, s, bad);
                fails++;
            }
        }
    printf(fails ? "acid gen: %d FAIL\n" : "acid gen: 240 lines (and two VARY each, %d notes) as X0X's TB-3PO: all ok\n", fails ? fails : notes);
    return fails ? 1 : 0;
}
