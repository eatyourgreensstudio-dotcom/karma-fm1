/* SPDX-License-Identifier: GPL-3.0-only */
/* KARMA 3.0.4: ELEMENTS' BOW and BLOW at half the rate (phys_dsp.c PHYS_HALF). Built twice (-DPX_HALF=0 / 1), each
 * build plays the BOW and BLOW presets the same way (notes 36 to 84 held 1.5 s, 1 s of release each) through the
 * real audio path, the part alone, and writes OUT_DIR/elem-PRESET.raw (mono int32 at 44.1 kHz) and the host time
 * per block. tests/elem_half_compare.py compares the two builds: the pitch, the level, the brightness, the decay.
 *   elem_half_test OUT_DIR */
#define main hostsim_main
#include "hostsim.c"
#undef main

int main(int argc, char **argv)
{
    static const uint8_t NOTES[] = {36, 48, 60, 72, 84};
    const char *dir = argc > 1 ? argv[1] : "build/host";
    const uint32_t np = ENGINES[ENGI_ELEM]->npresets, sec = FS / CTL;
    uint32_t k, j, b, i;
    for (k = 0; k < np; k++) {
        const preset_t *pr = &ENGINES[ENGI_ELEM]->presets[k];
        track_t *t = &trk[1];
        char path[512];
        FILE *f;
        uint64_t ns = 0, nb = 0;
        if (pr->e[0] != EM_BOW && pr->e[0] != EM_BLOW)
            continue;
        host_tracks_init();
        for (i = 0; i < NTRK; i++)
            steps_clear(&trk[i]);
        trk[0].p[P_LEVEL] = trk[2].p[P_LEVEL] = 0;
        song.g[G_DRLVL] = 0;
        host_preset(t, ENGI_ELEM, k);
        for (i = 0; i < 4u; i++)                     /* the sends off: the dry voice only */
            t->p[P_DIST + i] = 0;
        snprintf(path, sizeof path, "%s/elem-%s.raw", dir, pr->name);
        for (i = 0; path[i]; i++)
            if (path[i] == ' ')
                path[i] = '_';
        f = fopen(path, "wb");
        if (!f)
            return 1;
        for (j = 0; j < sizeof NOTES; j++) {
            for (b = 0; b < (5u * sec) / 2u; b++) {
                int32_t o[2 * CTL], m[CTL];
                uint64_t t0;
                if (b == 0)
                    trk_note_on(t, NOTES[j], getenv("VEL") ? (uint32_t)atoi(getenv("VEL")) : 100u);
                if (b == (3u * sec) / 2u)
                    trk_note_off(t, NOTES[j]);
                t0 = now_ns();
                mix_block(o, CTL);
                ns += now_ns() - t0;
                nb++;
                for (i = 0; i < CTL; i++)
                    m[i] = (o[2 * i] + o[2 * i + 1]) / 2;
                fwrite(m, sizeof m[0], CTL, f);
            }
        }
        fclose(f);
        printf("%-12s %6.2f us/block (host)\n", pr->name, (double)ns / nb / 1000.0);
    }
    return 0;
}
