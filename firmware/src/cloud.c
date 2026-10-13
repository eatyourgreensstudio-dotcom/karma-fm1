/* SPDX-License-Identifier: GPL-3.0-only */
/* CLOUD: the granular processor of Clouds (Emilie Gillet, MIT: cloud_dsp.c is her grain player in fixed point) as
 * a send effect, beside the delay and the reverb. Each synth track sends to it (cloud_send, the FX > CLOUD page's
 * SEND with that track selected), each drum sound too (drum_cloud, the same knob with the drum track selected: the
 * sound of the grid's KNOB 1). What is sent is recorded, and the cloud plays grains of the recording back.
 *
 * The recording, as her low-quality modes keep it: mono, 8 bits mu-law, at half the sample rate (22.05 kHz),
 * 32768 samples = 1.49 s (hers: 1 to 8 s). So the sound has her low-fi grit and tops out near 10 kHz. There is no
 * memory left for it, so it shares the punch-in FX's ring (punch.c), word for word: the low byte of each word is the
 * cloud's sample, the high byte the punch-in ring's, which became 8 bits mu-law too for this (it was 16 bits).
 * Both keep the code's bits turned over, so that memory still at 0 is silence.
 *   POS    where in the recording the grains start, from now back to its oldest part
 *   SIZE   a grain: 23 ms .. 372 ms
 *   PITCH  the speed the grains are read at, -24 .. +24 semitones
 *   DENS   as her knob: at noon none; to the right more and more grains at random, to the left at a steady rate
 *   TEXT   the grains' window: clicking slabs, a triangle, a smooth bell
 *   FDBK   the cloud back into its own recording
 *   FRZ    the recording stands still: the grains go on playing what it holds
 * Not here: her diffuser, her reverb and stereo recording, the pitch-shifter, the looping-delay and spectral modes.
 * A project keeps the cloud's settings and every send (project.c proj_cl_put).
 * Runs in the audio ISR (mix_block, fx.c); silent and free while nothing is sent and the grains have ended. */
#define CL_NG 16
#define CL_RING_T int16_t
#define CL_RD(ring, i) CL_ULAW[~(uint32_t)(ring)[i] & 0xFFu]
#define CLD_N 32768u                                  /* the recording, samples: the punch-in ring's words (PUNCH_N) */
#define CLD_DOWN 2u                                   /* .. one for this many of the output's */
static const int16_t CL_ULAW[256] = {                 /* mu-law -> 16 bits (MuLaw2Lin of clouds/dsp/mu_law.h) */
    -32124, -31100, -30076, -29052, -28028, -27004, -25980, -24956, -23932, -22908, -21884, -20860, -19836, -18812, -17788, -16764,
    -15996, -15484, -14972, -14460, -13948, -13436, -12924, -12412, -11900, -11388, -10876, -10364, -9852, -9340, -8828, -8316,
    -7932, -7676, -7420, -7164, -6908, -6652, -6396, -6140, -5884, -5628, -5372, -5116, -4860, -4604, -4348, -4092,
    -3900, -3772, -3644, -3516, -3388, -3260, -3132, -3004, -2876, -2748, -2620, -2492, -2364, -2236, -2108, -1980,
    -1884, -1820, -1756, -1692, -1628, -1564, -1500, -1436, -1372, -1308, -1244, -1180, -1116, -1052, -988, -924,
    -876, -844, -812, -780, -748, -716, -684, -652, -620, -588, -556, -524, -492, -460, -428, -396,
    -372, -356, -340, -324, -308, -292, -276, -260, -244, -228, -212, -196, -180, -164, -148, -132,
    -120, -112, -104, -96, -88, -80, -72, -64, -56, -48, -40, -32, -24, -16, -8, 0,
    32124, 31100, 30076, 29052, 28028, 27004, 25980, 24956, 23932, 22908, 21884, 20860, 19836, 18812, 17788, 16764,
    15996, 15484, 14972, 14460, 13948, 13436, 12924, 12412, 11900, 11388, 10876, 10364, 9852, 9340, 8828, 8316,
    7932, 7676, 7420, 7164, 6908, 6652, 6396, 6140, 5884, 5628, 5372, 5116, 4860, 4604, 4348, 4092,
    3900, 3772, 3644, 3516, 3388, 3260, 3132, 3004, 2876, 2748, 2620, 2492, 2364, 2236, 2108, 1980,
    1884, 1820, 1756, 1692, 1628, 1564, 1500, 1436, 1372, 1308, 1244, 1180, 1116, 1052, 988, 924,
    876, 844, 812, 780, 748, 716, 684, 652, 620, 588, 556, 524, 492, 460, 428, 396,
    372, 356, 340, 324, 308, 292, 276, 260, 244, 228, 212, 196, 180, 164, 148, 132,
    120, 112, 104, 96, 88, 80, 72, 64, 56, 48, 40, 32, 24, 16, 8, 0,
};
#include "cloud_dsp.c"

static int16_t punch_ring[CLD_N] __attribute__((section(".pool")));   /* (punch.c: its ring, shared) */
static cl_t cloud_cl;
static struct {
    uint32_t w;                                       /* where the recording is written next */
    int32_t acc;                                      /* the samples of this third */
    uint8_t cnt, live, init;
    uint32_t quiet;                                   /* blocks since anything was sent */
    int32_t fb, fb_lp;                                /* the cloud's last output (mono) and its low end, for FDBK */
    int32_t wet;                                      /* the return's gain, Q15: up and down without a click */
} cld;

static uint8_t cl_mulaw(int32_t pcm)                  /* Lin2MuLaw of clouds/dsp/mu_law.h */
{
    int32_t mask, seg;
    pcm >>= 2;
    if (pcm < 0) {
        pcm = -pcm;
        mask = 0x7f;
    } else {
        mask = 0xff;
    }
    if (pcm > 8159)
        pcm = 8159;
    pcm += 0x84 >> 2;
    for (seg = 0; seg < 8 && pcm > (0x3f << seg | ((1 << seg) - 1)); seg++)
        ;
    if (seg >= 8)
        return (uint8_t)(0x7f ^ mask);
    return (uint8_t)(((seg << 4) | ((pcm >> (seg + 1)) & 0x0f)) ^ mask);
}
static void cloud_init(void)
{
    uint32_t i;
    for (i = 0; i < CLD_N; i++)
        punch_ring[i] = (int16_t)((uint16_t)punch_ring[i] & 0xFF00u);   /* its bytes: silence */
    cl_init(&cloud_cl, 0x21u);
    cld.w = 0;
    cld.acc = cld.fb = cld.fb_lp = 0;
    cld.cnt = 0;
    cld.init = 1;
}

/* in[]: what the tracks sent, this block (cleared here for the next); the grains are added to l[] and r[] */
static void cloud_process(int32_t *in, int32_t *l, int32_t *r, uint32_t n)
{
    static int32_t wl[CTL], wr[CTL];
    const int16_t *g = song.g;
    cl_par_t p;
    uint32_t i, any = 0;
    int32_t d = g[G_CLDENS] * 516, tx = g[G_CLTEX] * 516, fbk = g[G_CLFB] * 516, frz = g[G_CLFRZ] != 0, peak = 0;
    if (n > CTL)
        n = CTL;
    for (i = 0; i < n; i++)
        any |= (uint32_t)in[i];
    if (any)
        cld.quiet = 0;
    else if (cld.quiet < 100000u)
        cld.quiet++;
    if (!cld.live) {
        if (!any)
            return;                                   /* nothing sent, nothing sounding */
        cloud_init();
        cld.live = 1;
        cld.wet = 32767;
    }
    /* the recording: a third of the rate (three samples as one), her feedback, mu-law; FRZ: left as it is */
    for (i = 0; i < n; i++) {
        cld.acc += in[i];
        in[i] = 0;
        if (++cld.cnt < CLD_DOWN)
            continue;
        cld.cnt = 0;
        if (!frz) {
            int32_t x = cld.acc / (int32_t)(2u * CLD_DOWN);   /* (half: room for loud tracks) */
            uint16_t *q = (uint16_t *)&punch_ring[cld.w++ & (CLD_N - 1u)];
            if (fbk) {                                /* in += fb (SoftLimit(1.4 fb cloud + in) - in) */
                int32_t y = x + (int32_t)(((int64_t)(int32_t)(((int64_t)fbk * 91750) >> 16) * (cld.fb - cld.fb_lp)) >> 16);
                uint32_t a = (uint32_t)(y < 0 ? -y : y), a2, num, den;
                if (a > 98304u)
                    a = 98304u;                       /* SoftLimit: x (27 + x^2) / (27 + 9 x^2), on +-3 */
                a2 = (a * a) >> 15;                   /* Q15 */
                num = 27u * 32768u + a2;
                den = 27u * 32768u + 9u * a2;
                a = (uint32_t)(((uint64_t)a * num) / den);
                y = y < 0 ? -(int32_t)a : (int32_t)a;
                x += (int32_t)(((int64_t)fbk * (y - x)) >> 16);
            }
            *q = (uint16_t)((*q & 0xFF00u) | (~(uint32_t)cl_mulaw(clamp(x, -32768, 32767)) & 0xFFu));
        }
        cld.acc = 0;
    }
    /* her mapping of the knobs (GranularProcessor::ProcessGranular) */
    p.position = g[G_CLPOS] * 516;
    p.size = (int32_t)((cl_ratio(g[G_CLSIZE] * 48 / 127) * 1024u) >> 16);   /* 1024 .. 16384 samples */
    p.ratio = cl_ratio(clamp(g[G_CLPIT], -24, 24));
    p.steady = d < 32768;
    p.overlap = d >= 34734 ? (int32_t)(((int64_t)(d - 34734) * 138936) >> 16)
              : d <= 30802 ? (int32_t)(((int64_t)(30802 - d) * 138936) >> 16) : 0;
    if (p.overlap > 65536)
        p.overlap = 65536;
    p.window = tx < 49152 ? (int32_t)(((int64_t)tx * 87359) >> 16) : 65536;
    p.spread = 45875;
    p.trigger = 0;
    p.down = CLD_DOWN;
    for (i = 0; i < n; i++)
        wl[i] = wr[i] = 0;
    cl_play(&cloud_cl, &p, punch_ring, CLD_N - 1u, cld.w, wl, wr, n);
    for (i = 0; i < n; i++) {                         /* back to the mix's level (x 2), her 1.2 on top */
        int32_t a = (wl[i] * 77) >> 5, b = (wr[i] * 77) >> 5, m = (wl[i] + wr[i]) >> 1;
        l[i] += a;
        r[i] += b;
        peak |= a | b;
        cld.fb = m;
        cld.fb_lp += ((m - cld.fb_lp) * (200 + (int32_t)(((int64_t)fbk * fbk >> 16) * 920 >> 16))) >> 16;   /* 20 .. 120 Hz */
    }
    /* asleep again: nothing sent for longer than the recording and its feedback last, the grains over */
    if (!frz && !peak && cld.quiet > (fbk ? 9000u : 2600u))
        cld.live = 0;
}
