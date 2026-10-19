/* SPDX-License-Identifier: GPL-3.0-only */
/* The Juno-106 voice of FM-106 (github.com/PatttF/fm-106, GPL-3.0-only: firmware/src/fm106_osc.h, fm106_vcf.h,
 * fm106_chorus.h; itself after ultramaster KR-106), for the JUNO engine (eng_juno.c). The same arithmetic, renamed
 * jn_*; two changes, neither changing a sample:
 *   - the filter's coefficients (from the cutoff and resonance) are worked out once a block (jn_vcf_coef), the
 *     ladder runs per sample (jn_vcf_run): FM-106 did both per sample for a cutoff that moves at control rate;
 *   - the chorus keeps its left channel only (a SLOOP part is mono): the dry signal and one BBD tap, as the
 *     Juno-106's mono output.
 * tests/juno_test.c checks them against FM-106's own code, sample for sample. */
#include "juno_tables.h"

/* ------------------------------------------------------------- DCO --- */
static inline int32_t jn_poly_blep(uint32_t ph, uint32_t inc)
{
    uint32_t d;
    if (inc < 2u)
        return 0;
    d = inc >> 15;
    if (!d)
        d = 1;
    if (ph < inc) {
        int32_t x = (int32_t)(ph / d);
        return (x << 1) - ((x * x) >> 15) - 32768;
    }
    if (ph > (0xFFFFFFFFu - inc)) {
        int32_t x = -(int32_t)((0xFFFFFFFFu - ph) / d);
        return ((x * x) >> 15) + (x << 1) + 32768;
    }
    return 0;
}

typedef struct {
    uint32_t ph;
    uint32_t noise_seed;
    int32_t noise_lpf;
    uint8_t sub_state;
} jn_dco_t;

/* one sample (Q15): saw, pulse (pw 0..105: 50 .. 95 %), sub (sub_level 0..127), noise (noise_level 0..127) */
static inline int32_t jn_dco(jn_dco_t *d, uint32_t inc, int saw_on, int pulse_on, int32_t pw, uint32_t sub_level,
                             uint32_t noise_level)
{
    uint32_t prev = d->ph, ph = prev + inc;
    int32_t blep, total = 0;
    d->ph = ph;
    if (ph < prev)
        d->sub_state = !d->sub_state;
    blep = jn_poly_blep(ph, inc);
    if (saw_on)
        total += ((((int32_t)(ph >> 16) - 32768) - blep) * 19660) >> 15;
    if (pulse_on) {
        uint32_t pw32, eff;
        int32_t pulse;
        if (pw < 0)
            pw = 0;
        if (pw > 105)
            pw = 105;
        pw32 = 0x80000000u + ((uint32_t)pw * 18407003u);
        eff = 0xFFFFFFFFu - pw32;
        pulse = ph < eff ? -32767 : 32767;
        pulse -= blep;
        pulse += jn_poly_blep(ph - eff, inc);
        total += (pulse * 16384) >> 15;
    }
    if (sub_level) {
        int32_t sign = d->sub_state ? -32767 : 32767, ab = blep < 0 ? -blep : blep, sub;
        if (ab > 32768)
            ab = 32768;
        sub = (sign * (32768 - ab)) >> 15;
        sub = (sub * (int32_t)JN_SUB_LEVEL_J106[sub_level > 127u ? 127u : sub_level]) >> 15;
        total += (sub * 24576) >> 15;
    }
    if (noise_level) {
        uint32_t s = d->noise_seed;
        int32_t raw;
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        d->noise_seed = s;
        raw = (int32_t)(s >> 16) - 32768;
        d->noise_lpf += (9300 * (raw - d->noise_lpf)) >> 15;
        total += (int32_t)(((int64_t)((d->noise_lpf * (int32_t)JN_NOISE_LEVEL_J106[noise_level > 127u ? 127u : noise_level]) >> 15) *
                            91000LL) >> 15);
    }
    return total;
}

/* 1.59 Hz coupling (FM-106 fm106_dc_block) */
static inline int32_t jn_dc_block(int32_t in, int64_t *state)
{
    int64_t diff = ((int64_t)in << 16) - *state;
    *state += (diff * 3800LL) >> 24;
    return in - (int32_t)(*state >> 16);
}

/* ------------------------------------------------------------- VCF --- */
static inline int32_t jn_soft_limit(int32_t x)
{
    int32_t ax = x < 0 ? -x : x, ex;
    if (ax <= 24576)
        return x;
    ex = (ax - 24576) >> 5;
    ax = 24576 + (ex * 7424) / (232 + ex);
    return x < 0 ? -ax : ax;
}
/* (FM-106's a / 384 and its interpolation / 384 as multiplies: exact over the range they meet, a < 98304 and
 * |t| < 12.6 M; checked by tests/juno_test.c) */
static inline int32_t jn_ota_sat(int32_t x)
{
    int32_t a = x < 0 ? -x : x, i, y;
    if (a >= 256 * 384) {
        y = 32767;
    } else {
        int32_t rem, t, q;
        i = (int32_t)(((uint32_t)a * 43691u) >> 24);
        rem = a - i * 384;
        t = (int32_t)(JN_OTA_SAT[i + 1] - JN_OTA_SAT[i]) * rem;
        q = t >= 0 ? (int32_t)(((uint64_t)t * 11184811u) >> 32) : -(int32_t)(((uint64_t)-t * 11184811u) >> 32);
        y = (int32_t)JN_OTA_SAT[i] + q;
    }
    return x < 0 ? -y : y;
}

typedef struct {
    int32_t z[4];                /* the 4 TPT integrators (Q24) */
    uint32_t noise_seed;         /* thermal noise into a silent filter (seeds self-oscillation) */
    int32_t env_follow;          /* input peak follower (22 ms) */
} jn_vcf_t;

typedef struct {                 /* a block's coefficients: from the cutoff (DAC 0..16383) and resonance (0..127) */
    int32_t g1, g1_2, g1_3, k, invden, fb, ifb;
} jn_coef_t;

static void jn_vcf_coef(jn_coef_t *c, uint32_t dac, uint32_t res)
{
    uint32_t code = dac >> 2;
    int32_t g1, k, G, den;
    if (code > 4095u)
        code = 4095u;
    if (res > 127u)
        res = 127u;
    g1 = JN_VCF_G1[code];
    k = JN_RES_K[res];
    if (code > 3500u) {                              /* near Nyquist: the resonance faded out */
        int32_t fade = 4095 - (int32_t)code;
        if (fade < 0)
            fade = 0;
        k = (k * fade) / 595;
    }
    g1 = (g1 * (int32_t)JN_PEAK_PULL[res]) >> 12;
    if (g1 > 32767)
        g1 = 32767;
    c->g1 = g1;
    c->g1_2 = (g1 * g1) >> 15;
    c->g1_3 = (c->g1_2 * g1) >> 15;
    G = (c->g1_2 * c->g1_2) >> 15;
    den = 4096 + ((k * G) >> 15);
    c->invden = (4096 * 4096) / (den > 0 ? den : 1);
    c->k = k;
    c->fb = JN_FB_SCALE[res];
    c->ifb = JN_INV_FB_SCALE[res];
}

static inline int32_t jn_vcf_run(jn_vcf_t *f, const jn_coef_t *c, int32_t input)
{
    int32_t in_abs = input < 0 ? -input : input, decay = (f->env_follow * 32734) >> 15;
    int32_t z0 = f->z[0] >> 9, z1 = f->z[1] >> 9, z2 = f->z[2] >> 9, z3 = f->z[3] >> 9, zs, energy;
    int32_t sum_z, S, diff, sat, ota, direct, fb, u, uq;
    int j;
    f->env_follow = in_abs > decay ? in_abs : decay;
    zs = (z0 < 0 ? -z0 : z0) + (z1 < 0 ? -z1 : z1) + (z2 < 0 ? -z2 : z2) + (z3 < 0 ? -z3 : z3);
    energy = f->env_follow > zs ? f->env_follow : zs;
    f->noise_seed = f->noise_seed * 196314165u + 907633515u;
    if (energy < 50) {
        int32_t white = (int32_t)(f->noise_seed >> 16) - 32768;
        input += (white * (327 / (1 + (energy >> 2)))) >> 15;
    }
    sum_z = (int32_t)(((int64_t)z0 * c->g1_3 + (int64_t)z1 * c->g1_2 + (int64_t)z2 * c->g1) >> 15) + z3;
    S = (int32_t)(((int64_t)(32768 - c->g1) * sum_z) >> 15);
    diff = (int32_t)(((int64_t)input * 2130) >> 15) - S;
    sat = jn_ota_sat((int32_t)(((int64_t)diff * c->fb) >> 12));
    ota = (int32_t)(((int64_t)sat * c->ifb) >> 12);
    direct = (int32_t)(((int64_t)input * 4653) >> 15);
    fb = (int32_t)(((int64_t)c->k * ota) >> 12);
    u = (int32_t)(((int64_t)(direct + fb) * c->invden) >> 12);
    uq = u << 9;
    for (j = 0; j < 4; j++) {
        int32_t delta = (int32_t)(((int64_t)c->g1 * (uq - f->z[j])) >> 15), y = f->z[j] + delta, nz = y + delta;
        if (nz > 134217727)
            nz = 134217727;
        else if (nz < -134217728)
            nz = -134217728;
        f->z[j] = nz;
        uq = y;
    }
    return (int32_t)(((int64_t)(uq >> 9) * 8796LL) >> 10);
}

/* ---------------------------------------------------------- chorus --- */
#define JN_CHO_LEN 1024u
#define JN_CHO_MASK (JN_CHO_LEN - 1u)
enum { JN_CHO_OFF, JN_CHO_I, JN_CHO_II, JN_CHO_I_II };
typedef struct {
    int16_t buf[JN_CHO_LEN];
    uint32_t wr, lfo_ph;
    int32_t ic1, ic2;            /* the BBD's reconstruction filter (2nd-order Butterworth, 9.7 kHz) */
} jn_chorus_t;

static inline int32_t jn_hermite(int32_t fr, int32_t y0, int32_t y1, int32_t y2, int32_t y3)
{
    int32_t c1 = (y2 - y0) >> 1, c2 = y0 - y1 + (y2 - y1) - ((y1 - (y2 << 1) + y3) >> 1);
    int32_t c3 = ((y3 - y0) >> 1) + y1 - y2 + ((y1 - y2) >> 1);
    int64_t acc = (((int64_t)c3 * fr) >> 15) + c2;
    acc = ((acc * fr) >> 15) + c1;
    acc = ((acc * fr) >> 15) + y1;
    return (int32_t)acc;
}
/* one sample, the left channel of FM-106's fm106_chorus_process */
static inline int32_t jn_chorus(jn_chorus_t *c, uint32_t mode, int32_t in)
{
    static const uint32_t INC[4] = {0, 50098u, 82068u, 765109u};
    static const int32_t DEPTH[4] = {0, 94, 75, 10};
    int32_t x = jn_soft_limit(in), tri, dq, di, fr, wet, v1, v2, v3;
    uint32_t wr = c->wr, r1;
    if (mode == JN_CHO_OFF)
        return x;
    c->buf[wr] = (int16_t)x;
    c->wr = (wr + 1u) & JN_CHO_MASK;
    c->lfo_ph += INC[mode & 3u];
    tri = (int32_t)(c->lfo_ph >> 16) - 32768;
    if (tri < 0)
        tri = -tri;
    tri = (tri - 16384) << 1;
    dq = (154 << 16) + tri * DEPTH[mode & 3u];
    di = dq >> 16;
    fr = (dq >> 1) & 0x7FFF;
    r1 = (wr - 1u - (uint32_t)di) & JN_CHO_MASK;
    wet = jn_hermite(fr, c->buf[(r1 + 1u) & JN_CHO_MASK], c->buf[r1], c->buf[(r1 - 1u) & JN_CHO_MASK],
                     c->buf[(r1 - 2u) & JN_CHO_MASK]);
    v3 = wet - c->ic2;
    v1 = ((11534 * c->ic1) >> 15) + ((9527 * v3) >> 15);
    v2 = c->ic2 + ((27066 * v1) >> 15);
    c->ic1 = (v1 << 1) - c->ic1;
    c->ic2 = (v2 << 1) - c->ic2;
    return jn_soft_limit((int32_t)(((int64_t)x * 21209LL + (int64_t)v2 * 30892LL) >> 15));
}
