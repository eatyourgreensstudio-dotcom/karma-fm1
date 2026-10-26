/* SPDX-License-Identifier: GPL-3.0-only */
/* A listening demo of the drum track's five factory synth kits (HEX BOOM .. HEX ROOM, kits 0..4): for each kit its
 * 16 lanes one after the other (0.4 s apart, velocity 110), then two bars of a groove at 120 BPM. Stereo 16-bit WAV
 * of the drum track's output (GLO > DRUMS LVL 100, no master: the drums_render mix as the FX get it).
 *   cc -O2 -w -Ibuild/gen -Ifirmware/src -o build/host/hex_demo tests/hex_demo.c build/host/libplx.a -lc++ -lm
 *   build/host/hex_demo OUT.wav
 *   build/host/hex_demo cost        (the cost of six voices of each kit at once, host instructions and ns)
 *   build/host/hex_demo lanes FILE  (every sound alone, for tools/level_hexkits.py) */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <libproc.h>
#include <sys/resource.h>
static uint64_t instr_now(void)
{
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
    return 0;
}
/* the cost of the kits' voices: per kit, 6 sounds (kick, snare, clap, open hat, crash, low tom) hit together every
 * 0.25 s for 10 s (all six voices busy), instructions and ns per sample of drums_render (host, cc -O2) */
static void cost(void)
{
    static const uint8_t SIX[6] = {36, 38, 39, 46, 49, 43};
    uint32_t kit, b, k;
    int32_t l[CTL], r[CTL], rv[CTL];
    for (kit = 0; kit < 5u; kit++) {
        uint64_t i0, t0, n = 0;
        memset(&drums, 0, sizeof drums);
        drums.set = -2;
        TDRUM->p[P_E0] = (int16_t)kit;
        i0 = instr_now();
        t0 = now_ns();
        for (b = 0; b < FS * 10u / CTL; b++) {
            if ((b * CTL) % (FS / 4u) < CTL)
                for (k = 0; k < 6u; k++)
                    drum_on(SIX[k], 110);
            memset(l, 0, sizeof l), memset(r, 0, sizeof r), memset(rv, 0, sizeof rv);
            drums_render(l, r, rv, rv, CTL);
            n += CTL;
        }
        printf("cost %-10s 6 voices: %6.0f instr  %6.1f ns per sample\n", DRUM_KIT_NAMES[kit],
               (double)(instr_now() - i0) / n, (double)(now_ns() - t0) / n);
    }
}

static FILE *out;
static uint32_t frames;
static void run(uint32_t n)                           /* n samples of the drums into the file */
{
    int32_t l[CTL], r[CTL], rv[CTL];
    uint32_t b, k;
    for (b = 0; b < n / CTL; b++) {
        memset(l, 0, sizeof l), memset(r, 0, sizeof r), memset(rv, 0, sizeof rv);
        drums_render(l, r, rv, rv, CTL);
        for (k = 0; k < CTL; k++)
            wav_put(out, clamp(l[k], -32767, 32767), clamp(r[k], -32767, 32767));
        frames += CTL;
    }
}
int main(int argc, char **argv)
{
    /* the groove, 16ths: kick, snare, clap, hats (closed / open / pedal), toms, rim, percussion */
    static const uint8_t P[32][4] = {
        {36, 42}, {0}, {42}, {0}, {38, 42}, {0}, {42, 37}, {70}, {36, 42}, {36}, {46}, {0}, {38, 39, 42}, {0}, {42, 63}, {56},
        {36, 42}, {0}, {42}, {35}, {38, 42}, {0}, {42}, {70}, {36, 42}, {0}, {46, 49}, {43}, {38, 39}, {48}, {43, 37}, {40}};
    static const uint8_t LANE_NOTE_[16] = {36, 35, 38, 39, 42, 46, 44, 37, 40, 43, 48, 49, 51, 70, 63, 56};
    uint32_t kit, j, q, step = FS * 60u / 120u / 4u;
    if (argc > 2 && !strcmp(argv[1], "lanes")) {      /* each kit x lane alone, one hit at 110, 2 s, the voice's own
                                                       * output (int32 mono) into argv[2]: tools/level_hexkits.py */
        FILE *f = fopen(argv[2], "wb");
        static hex_v_t v;
        int32_t b[32];
        uint32_t i;
        if (!f)
            return 1;
        host_tracks_init();
        for (kit = 0; kit < 5u; kit++)
            for (j = 0; j < 16u; j++) {
                hex_on(&v, HEX_KITS[kit].s, DS_LANE_NOTE[j], 110);
                for (i = 0; i < FS * 2u / 32u; i++) {
                    hex_render(&v, b, 32);
                    fwrite(b, 4, 32, f);
                }
            }
        fclose(f);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "cost")) {
        host_tracks_init();
        cost();
        return 0;
    }
    if (argc < 2 || !(out = fopen(argv[1], "wb")))
        return 1;
    host_tracks_init();
    song.g[G_DRLVL] = 100;
    song.g[G_DRREV] = 0;
    song.g[G_DRDLY] = 0;
    wav_hdr(out, 0);
    memset(&drums, 0, sizeof drums);
    drums.set = -2;
    for (kit = 0; kit < 5u; kit++) {
        TDRUM->p[P_E0] = (int16_t)kit;
        for (j = 0; j < 16u; j++) {
            drum_on(LANE_NOTE_[j], 110);
            run(FS * 2u / 5u / CTL * CTL);
        }
        run(FS / 4u / CTL * CTL);
        for (j = 0; j < 64u; j++) {
            for (q = 0; q < 4u && P[j % 32u][q]; q++)
                drum_on(P[j % 32u][q], q ? 92 : (j % 4u ? 100 : 118));
            run(step / CTL * CTL);
        }
        run(FS / CTL * CTL);
    }
    fseek(out, 0, SEEK_SET);
    wav_hdr(out, frames);
    fclose(out);
    printf("%s: 5 kits, %.1f s\n", argv[1], frames / (double)FS);
    return 0;
}
