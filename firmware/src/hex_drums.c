/* SPDX-License-Identifier: GPL-3.0-only */
/* HEX: synthesised drums in the manner of the LXR drum machine's voices (written here from what such a voice does,
 * no code of the LXR's): every sound of a kit is one hex_snd_t, a small synth with
 *   OSC     a waveform (sine, triangle, saw, square, noise) at a pitch (COARSE a note, FINE cents)
 *   PITCH   a fall from PAMT semitones above, as fast as PDEC: the body of a kick or a tom
 *   FM      a sine modulating the oscillator's phase: RATIO to it, AMT how deep (it fades with the sound)
 *   CLICK   a transient on the attack: a tick, a noise burst, a pop, a blip or a zap, its level and tone
 *   FILTER  low-pass, high-pass, band-pass or notch: CUT, RES; DRIVE before it
 *   AEG     ATK, DEC, SLOPE (from a long tail to a short punch), LEVEL
 *   CRUSH   the sample rate reduced
 * and its model, which says what the oscillator is part of:
 *   DRUM    the oscillator alone: kicks, toms, percussion, zaps
 *   SNARE   the oscillator (the head) and a noise (the wires: AUX1 its level, AUX2 its decay), the filter on the noise
 *   CLAP    noise in a few quick bursts (AUX1 how far apart), then the tail (AUX2 its level), through the filter
 *   CYM     three squares at inharmonic ratios, ringing into each other (FM AMT), and a noise (AUX1)
 *   HAT     six squares at the ratios of the metal of an analogue hat (AUX2 their spread), and a noise (AUX1)
 * Float, on the AC79's FPU (as eng_acid.c). The kits: HEX_KITS below; drums.c plays them as kits 0..4. */
enum { HM_DRUM, HM_SNARE, HM_CLAP, HM_CYM, HM_HAT, HM_COUNT };
enum { HW_SINE, HW_TRI, HW_SAW, HW_SQR, HW_NOISE, HW_COUNT };
enum { HF_OFF, HF_LP, HF_HP, HF_BP, HF_NOTCH, HF_COUNT };
enum { HC_OFF, HC_TICK, HC_NOISE, HC_POP, HC_BLIP, HC_ZAP, HC_COUNT };

typedef struct {
    uint8_t model, wave, coarse;                 /* HM_*, HW_*, the note (MIDI) */
    int8_t fine;                                 /* cents, -64..63 */
    uint8_t atk, dec, slope, level;              /* AEG: 0..127 (level 100 = 0 dB) */
    uint8_t pamt, pdec;                          /* PITCH: semitones x 4 / 3 above at the hit, its fall */
    uint8_t fmratio, fmamt;                      /* FM: ratio (64 = 1:1, 16 a step of an octave), depth */
    uint8_t click, clvl, ctone;                  /* CLICK: HC_*, level, tone */
    uint8_t ftype, cut, res, drive;              /* FILTER: HF_*, cutoff, resonance; drive before it */
    uint8_t aux1, aux2;                          /* the model's own (above) */
    uint8_t crush;                               /* 0 off .. 127 the rate down to ~1/16 */
} hex_snd_t;
#define HEX_LANES 16
typedef struct {
    const char *name;
    hex_snd_t s[HEX_LANES];
} hex_kit_t;

typedef struct {
    const hex_snd_t *d;
    float ph, ph2, inc, base, fm_inc;            /* the oscillator (turns), the FM sine, Hz -> turns */
    float pe, kpe;                               /* pitch envelope (1 at the hit) and its fall a sample */
    float amp, ka, att, katt;                    /* the amplitude envelope, its fall; the attack ramp */
    float nz, kn;                                /* SNARE / CLAP: the noise's envelope */
    float ck, kck, cph, cinc;                    /* the click: its envelope, its oscillator */
    float kb;                                    /* CLAP: a burst's fall a sample */
    float mph[6], minc[6];                       /* CYM / HAT: the squares */
    float s1, s2, g, k, a1;                      /* the filter (TPT state variable) */
    float hold, held;                            /* CRUSH: the sample held, how far into the hold */
    float vel, gain;
    uint32_t rnd, t, burst;                      /* noise, samples since the hit, CLAP: bursts so far */
} hex_v_t;

static inline float hx_noise(hex_v_t *v)        /* -1 .. 1 */
{
    v->rnd = v->rnd * 1664525u + 1013904223u;
    return (float)(int32_t)v->rnd * (1.0f / 2147483648.0f);
}
static inline float hx_sin(float ph)             /* sin(2 pi ph), ph in turns (any), parabolic, refined */
{
    float x = ph - fm_floorf(ph), y;
    x = x < 0.5f ? x : x - 1.0f;                 /* -0.5 .. 0.5 */
    y = 8.0f * x - 16.0f * x * (x < 0.0f ? -x : x);
    return 0.225f * (y * (y < 0.0f ? -y : y) - y) + y;
}
static inline float hx_wave(uint32_t w, float ph, float dt, hex_v_t *v)
{
    float x = ph - fm_floorf(ph), y, t;
    switch (w) {
    case HW_TRI:
        return x < 0.5f ? 4.0f * x - 1.0f : 3.0f - 4.0f * x;
    case HW_SAW:                                 /* (a polyBLEP on its edge) */
        y = 2.0f * x - 1.0f;
        if (x < dt) { t = x / dt; y -= t + t - t * t - 1.0f; }
        else if (x > 1.0f - dt) { t = (x - 1.0f) / dt; y -= t * t + t + t + 1.0f; }
        return y;
    case HW_SQR:
        return x < 0.5f ? 1.0f : -1.0f;
    case HW_NOISE:
        return hx_noise(v);
    default:
        return hx_sin(ph);
    }
}
/* a time of 0..127: 1 ms .. ~4 s, exponential; the factor a sample that falls -60 dB in it */
static float hx_ms(uint32_t c) { return 1.0f * fm_exp2f((float)c * (12.0f / 127.0f)); }
static float hx_fall(float ms) { return fm_exp2f(-9.966f / (ms * 44.1f)); }
static float hx_hz(int32_t note16) { return 440.0f * fm_exp2f(((float)note16 * (1.0f / 16.0f) - 69.0f) * (1.0f / 12.0f)); }
static void hx_filter(hex_v_t *v, float fc)      /* fc in Hz; k from the resonance */
{
    float w = fc * (3.14159265f / 44100.0f);
    if (w > 1.5f)
        w = 1.5f;
    v->g = fm_tanf(w);
    v->a1 = 1.0f / (1.0f + v->g * (v->g + v->k));
}

static const float HX_CYM[3] = {1.0f, 1.4471f, 1.6170f};
static const float HX_HAT[6] = {1.0f, 1.3420f, 1.2312f, 1.6532f, 1.9523f, 2.1523f};

static void hex_on(hex_v_t *v, const hex_snd_t *kit, uint32_t note, uint32_t vel)
{
    int32_t semi;
    uint32_t lane = ds_lane(note, &semi), i;
    const hex_snd_t *d = &kit[lane % HEX_LANES];
    float hz;
    memset(v, 0, sizeof *v);
    v->d = d;
    v->vel = 0.25f + 0.75f * (float)vel * (1.0f / 127.0f);
    v->gain = fm_exp2f(((float)d->level - 100.0f) * (1.0f / 16.0f));   /* 16 a 6 dB step */
    hz = hx_hz(((int32_t)d->coarse + semi) * 16 + (d->fine * 16) / 100);
    v->base = hz * (1.0f / 44100.0f);
    v->fm_inc = v->base * fm_exp2f(((float)d->fmratio - 64.0f) * (1.0f / 16.0f));
    v->pe = d->pamt ? 1.0f : 0.0f;
    v->kpe = hx_fall(hx_ms(d->pdec) * 0.5f);
    v->ka = hx_fall(hx_ms(d->dec));
    v->katt = d->atk ? 1.0f / (hx_ms(d->atk >> 1) * 44.1f) : 1.0f;
    v->att = d->atk ? 0.0f : 1.0f;
    v->amp = 1.0f;
    v->nz = 1.0f;
    v->kn = d->model == HM_CLAP ? 1.0f : hx_fall(hx_ms(d->aux2));
    v->ck = d->click ? (float)d->clvl * (1.0f / 127.0f) : 0.0f;
    v->cinc = (200.0f * fm_exp2f((float)d->ctone * (6.0f / 127.0f))) * (1.0f / 44100.0f);
    v->kck = hx_fall(d->click == HC_POP ? 18.0f : d->click == HC_ZAP ? 12.0f : 4.0f);
    v->kb = hx_fall(9.0f);
    for (i = 0; i < 6u; i++)
        v->minc[i] = v->base * (d->model == HM_CYM ? (i < 3u ? HX_CYM[i] : HX_CYM[i - 3u] * 2.0f)
                                                   : 1.0f + (HX_HAT[i] - 1.0f) * (0.5f + (float)d->aux2 * (1.0f / 127.0f)));
    v->k = 2.0f - 1.96f * (float)d->res * (1.0f / 127.0f);
    hx_filter(v, 20.0f * fm_exp2f((float)d->cut * (10.0f / 127.0f)) * (0.6f + 0.4f * v->vel));
    v->rnd = 0x9E3779B9u ^ (note * 2654435761u) ^ rng();
}

/* n samples of a voice into out[] (the drums' scale: about +-32767 at full); 0 when it has rung out */
static int hex_render(hex_v_t *v, int32_t *out, uint32_t n)
{
    const hex_snd_t *d = v->d;
    uint32_t i, k, m = d->model, cr = d->crush ? 1u + (uint32_t)d->crush / 8u : 0u;
    float pamt = (float)d->pamt * (1.0f / 36.0f), fmi = (float)d->fmamt * (1.0f / 16.0f);   /* octaves; radians / 2 pi */
    float drive = 1.0f + (float)d->drive * (1.0f / 16.0f), out_g = 26000.0f * v->gain * v->vel;
    float slope = (float)d->slope * (1.0f / 127.0f), aux1 = (float)d->aux1 * (1.0f / 127.0f);
    for (i = 0; i < n; i++) {
        float a = v->amp * (1.0f - slope + slope * v->amp * v->amp), env = a * v->att, x = 0.0f, nz = 0.0f, f;
        float inc = v->base * fm_exp2f(pamt * v->pe);
        /* the source */
        if (m == HM_CYM || m == HM_HAT) {
            float sq = 0.0f;
            for (k = 0; k < 6u; k++) {
                float p = v->mph[k] + (m == HM_CYM && k ? fmi * 0.05f * (sq > 0.0f ? 1.0f : -1.0f) : 0.0f);
                sq += p - fm_floorf(p) < 0.5f ? 1.0f : -1.0f;
                v->mph[k] += v->minc[k] * (1.0f + pamt * v->pe);
            }
            x = sq * (1.0f / 6.0f) * (1.0f - aux1) + hx_noise(v) * aux1;
        } else if (m == HM_CLAP) {
            x = 0.0f;
            nz = hx_noise(v);
        } else {
            float pm = fmi > 0.0f ? fmi * env * hx_sin(v->ph2) : 0.0f;
            x = hx_wave(d->wave, v->ph + pm, inc, v);
            v->ph2 += v->fm_inc * (1.0f + pamt * v->pe);
            if (m == HM_SNARE)
                nz = hx_noise(v) * aux1 * v->nz;
        }
        v->ph += inc;
        if (v->ph > 64.0f) {
            v->ph -= 64.0f;
            v->ph2 -= fm_floorf(v->ph2);
        }
        /* the click */
        if (v->ck > 1e-4f) {
            float c;
            switch (d->click) {
            case HC_TICK: c = hx_sin(v->cph * 4.0f); break;
            case HC_NOISE: c = hx_noise(v); break;
            case HC_POP: c = hx_sin(v->cph * 0.5f); break;
            case HC_BLIP: c = v->cph - fm_floorf(v->cph) < 0.5f ? 1.0f : -1.0f; break;
            default: c = hx_sin(v->cph * 2.0f); v->cinc *= 0.9985f; break;   /* ZAP: its pitch falls */
            }
            v->cph += v->cinc;
            x += c * v->ck * 1.5f;
            v->ck *= v->kck;
        }
        /* the filter: on everything, but on the noise alone in a snare, after the drive */
        f = m == HM_SNARE ? nz : m == HM_CLAP ? nz : x;
        if (d->drive)
            f = fm_tanhf(f * drive) / (drive > 2.0f ? 1.0f : drive * 0.5f + 0.5f);
        if (d->ftype != HF_OFF) {
            float hp = (f - (v->g + v->k) * v->s1 - v->s2) * v->a1, bp = v->g * hp + v->s1, lp = v->g * bp + v->s2;
            v->s1 = v->g * hp + bp;
            v->s2 = v->g * bp + lp;
            f = d->ftype == HF_LP ? lp : d->ftype == HF_HP ? hp : d->ftype == HF_BP ? bp * v->k : hp + lp;
        }
        if (m == HM_SNARE)
            f = x + f;
        else if (m == HM_CLAP) {                 /* bursts, then the tail */
            uint32_t gap = 220u + (uint32_t)d->aux1 * 7u;
            float b;
            if (v->burst < 3u && v->t >= (v->burst + 1u) * gap) {
                v->burst++;
                v->nz = 1.0f;
            }
            b = v->burst < 3u ? v->nz : 0.0f;
            f = f * (b + (float)d->aux2 * (1.0f / 127.0f) * v->amp * (v->burst ? 1.0f : 0.3f));
            env = v->att;
            v->nz *= v->kb;
        }
        /* the amplitude, the crush */
        x = f * env;
        if (cr) {
            if (v->held <= 0.0f) {
                v->hold = x;
                v->held = (float)cr;
            }
            v->held -= 1.0f;
            x = v->hold;
        }
        out[i] = (int32_t)(fm_clampf(x, -1.2f, 1.2f) * out_g);
        v->amp *= v->ka;
        v->nz *= v->kn;
        v->pe *= v->kpe;
        if (v->att < 1.0f) {
            v->att += v->katt;
            if (v->att > 1.0f)
                v->att = 1.0f;
        }
        v->t++;
    }
    v->s1 = fm_flush(v->s1);
    v->s2 = fm_flush(v->s2);
    return v->amp > 1e-4f || (m == HM_CLAP && v->burst < 3u) || v->ck > 1e-4f || v->att < 1.0f;
}

/* ---- the kits: {MODEL, WAVE, COARSE, FINE, ATK, DEC, SLOPE, LEVEL, PAMT, PDEC, RATIO, FM, CLICK, CLVL, CTONE,
 * FILTER, CUT, RES, DRIVE, AUX1, AUX2, CRUSH}. DEC: 60 ~50 ms, 80 ~190 ms, 100 ~0.7 s, 120 ~2.5 s; CUT: 64 ~660 Hz,
 * 90 ~2.7 kHz, 110 ~8 kHz; LEVEL 100 = 0 dB, 16 a 6 dB step. Lanes: kick snare clap hat open-hat tom-lo tom-hi crash
 * ride shaker conga rim cowbell clave kick-2 snare-2 */
#define HS(...) {__VA_ARGS__}
static const hex_kit_t HEX_KITS[5] = {
    {"HEX BOOM", {
        HS(HM_DRUM, HW_SINE, 33, 0, 0, 92, 40, 120, 72, 52, 64, 0, HC_POP, 70, 60, HF_LP, 120, 0, 20, 0, 0, 0),
        HS(HM_SNARE, HW_TRI, 50, 0, 0, 70, 60, 106, 40, 40, 64, 0, HC_TICK, 60, 90, HF_HP, 90, 20, 0, 100, 72, 0),
        HS(HM_CLAP, HW_NOISE, 60, 0, 0, 76, 0, 127, 0, 0, 64, 0, HC_OFF, 0, 0, HF_BP, 95, 50, 10, 16, 80, 0),
        HS(HM_HAT, HW_SQR, 70, 0, 0, 52, 80, 123, 0, 0, 64, 0, HC_NOISE, 40, 110, HF_HP, 108, 30, 0, 40, 64, 0),
        HS(HM_HAT, HW_SQR, 70, 0, 0, 86, 40, 112, 0, 0, 64, 0, HC_NOISE, 40, 110, HF_HP, 108, 30, 0, 40, 64, 0),
        HS(HM_DRUM, HW_SINE, 43, 0, 0, 84, 40, 114, 40, 50, 64, 0, HC_TICK, 50, 70, HF_LP, 110, 0, 0, 0, 0, 0),
        HS(HM_DRUM, HW_SINE, 50, 0, 0, 80, 40, 116, 40, 46, 64, 0, HC_TICK, 50, 80, HF_LP, 112, 0, 0, 0, 0, 0),
        HS(HM_CYM, HW_SQR, 64, 0, 0, 112, 30, 95, 0, 0, 64, 40, HC_NOISE, 50, 110, HF_HP, 100, 20, 0, 50, 0, 0),
        HS(HM_CYM, HW_SQR, 72, 0, 0, 104, 20, 101, 0, 0, 64, 20, HC_TICK, 50, 120, HF_HP, 104, 30, 0, 20, 0, 0),
        HS(HM_DRUM, HW_NOISE, 60, 0, 20, 50, 60, 116, 0, 0, 64, 0, HC_OFF, 0, 0, HF_HP, 112, 10, 0, 0, 0, 0),
        HS(HM_DRUM, HW_SINE, 57, 0, 0, 74, 40, 114, 20, 30, 64, 0, HC_POP, 50, 90, HF_OFF, 0, 0, 0, 0, 0, 0),
        HS(HM_DRUM, HW_TRI, 70, 0, 0, 40, 60, 127, 30, 20, 90, 30, HC_TICK, 90, 100, HF_BP, 96, 50, 20, 0, 0, 0),
        HS(HM_DRUM, HW_SQR, 68, 0, 0, 70, 40, 103, 0, 0, 73, 20, HC_OFF, 0, 0, HF_BP, 92, 40, 0, 0, 0, 0),
        HS(HM_DRUM, HW_SINE, 84, 0, 0, 44, 50, 122, 0, 0, 64, 0, HC_TICK, 60, 110, HF_OFF, 0, 0, 0, 0, 0, 0),
        HS(HM_DRUM, HW_SINE, 36, 0, 0, 80, 60, 127, 90, 42, 64, 30, HC_ZAP, 60, 100, HF_LP, 100, 30, 30, 0, 0, 0),
        HS(HM_SNARE, HW_SINE, 55, 0, 0, 60, 60, 96, 50, 30, 64, 0, HC_NOISE, 60, 100, HF_BP, 100, 30, 20, 110, 60, 0)}},
    {"HEX ACID", {                                /* bright, tight, techno */
        HS(HM_DRUM, HW_SINE, 36, 0, 0, 82, 70, 127, 60, 40, 64, 10, HC_TICK, 80, 90, HF_LP, 110, 10, 40, 0, 0, 0),
        HS(HM_SNARE, HW_TRI, 55, 0, 0, 60, 70, 114, 36, 30, 64, 0, HC_NOISE, 70, 100, HF_HP, 100, 20, 10, 110, 64, 0),
        HS(HM_CLAP, HW_NOISE, 60, 0, 0, 70, 0, 120, 0, 0, 64, 0, HC_OFF, 0, 0, HF_BP, 100, 60, 20, 12, 70, 0),
        HS(HM_HAT, HW_SQR, 74, 0, 0, 44, 90, 127, 0, 0, 64, 0, HC_TICK, 50, 120, HF_HP, 112, 40, 10, 30, 80, 0),
        HS(HM_HAT, HW_SQR, 74, 0, 0, 80, 50, 122, 0, 0, 64, 0, HC_TICK, 50, 120, HF_HP, 112, 40, 10, 30, 80, 0),
        HS(HM_DRUM, HW_SINE, 45, 0, 0, 76, 60, 127, 48, 40, 64, 10, HC_TICK, 60, 90, HF_LP, 110, 0, 10, 0, 0, 0),
        HS(HM_DRUM, HW_SINE, 52, 0, 0, 72, 60, 127, 48, 36, 64, 10, HC_TICK, 60, 90, HF_LP, 112, 0, 10, 0, 0, 0),
        HS(HM_CYM, HW_SQR, 68, 0, 0, 108, 40, 102, 0, 0, 64, 60, HC_NOISE, 60, 120, HF_HP, 106, 30, 10, 40, 0, 0),
        HS(HM_CYM, HW_SQR, 76, 0, 0, 100, 30, 103, 0, 0, 64, 30, HC_TICK, 60, 124, HF_HP, 108, 40, 0, 20, 0, 0),
        HS(HM_DRUM, HW_NOISE, 60, 0, 10, 44, 70, 126, 0, 0, 64, 0, HC_OFF, 0, 0, HF_HP, 116, 20, 0, 0, 0, 0),
        HS(HM_DRUM, HW_SINE, 60, 0, 0, 68, 50, 119, 24, 24, 64, 0, HC_POP, 60, 100, HF_OFF, 0, 0, 0, 0, 0, 0),
        HS(HM_DRUM, HW_SQR, 72, 0, 0, 36, 70, 127, 20, 16, 96, 40, HC_TICK, 100, 110, HF_BP, 100, 60, 30, 0, 0, 0),
        HS(HM_DRUM, HW_SQR, 70, 0, 0, 66, 50, 115, 0, 0, 73, 30, HC_OFF, 0, 0, HF_BP, 96, 50, 10, 0, 0, 0),
        HS(HM_DRUM, HW_SINE, 88, 0, 0, 40, 60, 126, 0, 0, 64, 0, HC_TICK, 70, 120, HF_OFF, 0, 0, 0, 0, 0, 0),
        HS(HM_DRUM, HW_SINE, 34, 0, 0, 88, 50, 127, 100, 36, 64, 20, HC_ZAP, 70, 110, HF_LP, 96, 40, 60, 0, 0, 0),
        HS(HM_SNARE, HW_SQR, 58, 0, 0, 56, 70, 96, 40, 24, 80, 30, HC_TICK, 80, 110, HF_BP, 104, 40, 30, 120, 56, 0)}},
    {"HEX METAL", {                               /* FM, drive, industrial */
        HS(HM_DRUM, HW_SINE, 31, 0, 0, 90, 50, 120, 80, 46, 72, 40, HC_ZAP, 70, 90, HF_LP, 104, 30, 80, 0, 0, 0),
        HS(HM_SNARE, HW_SINE, 52, 0, 0, 68, 60, 102, 30, 30, 101, 60, HC_TICK, 80, 110, HF_BP, 98, 50, 40, 100, 70, 0),
        HS(HM_CLAP, HW_NOISE, 60, 0, 0, 74, 0, 123, 0, 0, 64, 0, HC_OFF, 0, 0, HF_BP, 90, 80, 60, 20, 80, 0),
        HS(HM_HAT, HW_SQR, 66, 0, 0, 48, 80, 117, 0, 0, 64, 0, HC_TICK, 60, 120, HF_HP, 104, 60, 40, 20, 110, 0),
        HS(HM_HAT, HW_SQR, 66, 0, 0, 84, 40, 101, 0, 0, 64, 0, HC_TICK, 60, 120, HF_HP, 104, 60, 40, 20, 110, 0),
        HS(HM_DRUM, HW_SINE, 40, 0, 0, 82, 50, 113, 50, 50, 83, 50, HC_TICK, 60, 80, HF_LP, 104, 20, 50, 0, 0, 0),
        HS(HM_DRUM, HW_SINE, 47, 0, 0, 78, 50, 116, 50, 46, 83, 50, HC_TICK, 60, 90, HF_LP, 106, 20, 50, 0, 0, 0),
        HS(HM_CYM, HW_SQR, 60, 0, 0, 114, 30, 82, 0, 0, 64, 100, HC_NOISE, 60, 110, HF_HP, 96, 40, 40, 30, 0, 0),
        HS(HM_CYM, HW_SQR, 70, 0, 0, 106, 20, 84, 0, 0, 64, 70, HC_TICK, 60, 120, HF_BP, 106, 60, 30, 10, 0, 0),
        HS(HM_DRUM, HW_NOISE, 60, 0, 10, 48, 60, 112, 0, 0, 64, 0, HC_OFF, 0, 0, HF_BP, 108, 60, 30, 0, 0, 0),
        HS(HM_DRUM, HW_SINE, 55, 0, 0, 72, 50, 113, 30, 30, 90, 40, HC_POP, 60, 90, HF_OFF, 0, 0, 20, 0, 0, 0),
        HS(HM_DRUM, HW_SQR, 74, 0, 0, 34, 70, 127, 20, 16, 110, 80, HC_TICK, 100, 110, HF_BP, 100, 80, 60, 0, 0, 0),
        HS(HM_DRUM, HW_SQR, 66, 0, 0, 72, 40, 110, 0, 0, 77, 70, HC_OFF, 0, 0, HF_BP, 94, 70, 30, 0, 0, 0),
        HS(HM_DRUM, HW_SINE, 82, 0, 0, 44, 50, 124, 0, 0, 107, 60, HC_TICK, 70, 110, HF_OFF, 0, 0, 0, 0, 0, 0),
        HS(HM_DRUM, HW_SAW, 33, 0, 0, 84, 50, 127, 90, 40, 64, 0, HC_ZAP, 80, 110, HF_LP, 92, 60, 90, 0, 0, 0),
        HS(HM_SNARE, HW_SQR, 50, 0, 0, 64, 60, 96, 40, 30, 107, 80, HC_NOISE, 70, 100, HF_HP, 100, 50, 60, 110, 64, 0)}},
    {"HEX GLITCH", {                              /* crushed, zapped, short */
        HS(HM_DRUM, HW_SINE, 35, 0, 0, 76, 80, 127, 96, 30, 64, 0, HC_ZAP, 90, 120, HF_LP, 112, 20, 50, 0, 0, 40),
        HS(HM_SNARE, HW_SQR, 57, 0, 0, 52, 80, 109, 50, 20, 64, 0, HC_NOISE, 90, 120, HF_HP, 104, 30, 30, 120, 50, 60),
        HS(HM_CLAP, HW_NOISE, 60, 0, 0, 60, 0, 122, 0, 0, 64, 0, HC_OFF, 0, 0, HF_BP, 104, 70, 30, 8, 60, 50),
        HS(HM_HAT, HW_SQR, 78, 0, 0, 36, 100, 117, 0, 0, 64, 0, HC_BLIP, 50, 120, HF_HP, 114, 30, 0, 50, 90, 70),
        HS(HM_HAT, HW_SQR, 78, 0, 0, 70, 60, 120, 0, 0, 64, 0, HC_BLIP, 50, 120, HF_HP, 114, 30, 0, 50, 90, 70),
        HS(HM_DRUM, HW_SQR, 45, 0, 0, 62, 80, 127, 80, 24, 64, 0, HC_ZAP, 70, 100, HF_LP, 100, 40, 30, 0, 0, 50),
        HS(HM_DRUM, HW_SQR, 52, 0, 0, 58, 80, 127, 80, 22, 64, 0, HC_ZAP, 70, 110, HF_LP, 104, 40, 30, 0, 0, 50),
        HS(HM_CYM, HW_SQR, 70, 0, 0, 96, 50, 91, 0, 0, 64, 80, HC_NOISE, 60, 120, HF_HP, 108, 30, 20, 60, 0, 80),
        HS(HM_CYM, HW_SQR, 80, 0, 0, 90, 40, 104, 0, 0, 64, 50, HC_BLIP, 60, 124, HF_HP, 110, 40, 0, 20, 0, 60),
        HS(HM_DRUM, HW_NOISE, 60, 0, 0, 36, 90, 127, 0, 0, 64, 0, HC_OFF, 0, 0, HF_HP, 118, 20, 0, 0, 0, 90),
        HS(HM_DRUM, HW_TRI, 62, 0, 0, 56, 70, 127, 60, 16, 64, 0, HC_BLIP, 60, 100, HF_OFF, 0, 0, 0, 0, 0, 40),
        HS(HM_DRUM, HW_SQR, 76, 0, 0, 30, 90, 127, 40, 10, 96, 60, HC_ZAP, 100, 124, HF_BP, 104, 60, 40, 0, 0, 70),
        HS(HM_DRUM, HW_SQR, 72, 0, 0, 56, 60, 119, 0, 0, 79, 40, HC_BLIP, 50, 110, HF_BP, 100, 60, 20, 0, 0, 60),
        HS(HM_DRUM, HW_SINE, 92, 0, 0, 34, 70, 127, 30, 10, 64, 0, HC_ZAP, 80, 124, HF_OFF, 0, 0, 0, 0, 0, 30),
        HS(HM_DRUM, HW_SAW, 31, 0, 0, 80, 70, 127, 110, 24, 64, 30, HC_ZAP, 90, 120, HF_LP, 100, 60, 70, 0, 0, 60),
        HS(HM_SNARE, HW_NOISE, 60, 0, 0, 48, 90, 96, 0, 0, 64, 0, HC_ZAP, 80, 120, HF_BP, 108, 60, 40, 127, 40, 90)}},
    {"HEX ROOM", {                                /* soft, round, warm */
        HS(HM_DRUM, HW_SINE, 31, 0, 2, 96, 30, 125, 48, 60, 64, 0, HC_POP, 50, 40, HF_LP, 96, 0, 10, 0, 0, 0),
        HS(HM_SNARE, HW_SINE, 48, 0, 0, 76, 40, 97, 24, 40, 64, 0, HC_TICK, 40, 70, HF_BP, 92, 20, 0, 90, 84, 0),
        HS(HM_CLAP, HW_NOISE, 60, 0, 4, 84, 0, 120, 0, 0, 64, 0, HC_OFF, 0, 0, HF_BP, 88, 30, 0, 24, 90, 0),
        HS(HM_HAT, HW_SQR, 68, 0, 2, 56, 60, 118, 0, 0, 64, 0, HC_OFF, 0, 0, HF_HP, 104, 10, 0, 60, 40, 0),
        HS(HM_HAT, HW_SQR, 68, 0, 2, 90, 30, 100, 0, 0, 64, 0, HC_OFF, 0, 0, HF_HP, 104, 10, 0, 60, 40, 0),
        HS(HM_DRUM, HW_SINE, 41, 0, 0, 90, 30, 109, 30, 56, 64, 0, HC_POP, 40, 50, HF_LP, 100, 0, 0, 0, 0, 0),
        HS(HM_DRUM, HW_SINE, 48, 0, 0, 86, 30, 110, 30, 52, 64, 0, HC_POP, 40, 60, HF_LP, 104, 0, 0, 0, 0, 0),
        HS(HM_CYM, HW_SQR, 62, 0, 4, 116, 20, 85, 0, 0, 64, 20, HC_OFF, 0, 0, HF_HP, 96, 10, 0, 70, 0, 0),
        HS(HM_CYM, HW_SQR, 70, 0, 0, 110, 10, 90, 0, 0, 64, 10, HC_TICK, 30, 110, HF_BP, 100, 20, 0, 40, 0, 0),
        HS(HM_DRUM, HW_NOISE, 60, 0, 30, 56, 40, 108, 0, 0, 64, 0, HC_OFF, 0, 0, HF_BP, 106, 20, 0, 0, 0, 0),
        HS(HM_DRUM, HW_SINE, 55, 0, 0, 80, 30, 109, 16, 36, 64, 0, HC_POP, 40, 80, HF_OFF, 0, 0, 0, 0, 0, 0),
        HS(HM_DRUM, HW_TRI, 67, 0, 0, 46, 50, 123, 20, 20, 90, 20, HC_TICK, 60, 90, HF_BP, 92, 30, 0, 0, 0, 0),
        HS(HM_DRUM, HW_SINE, 74, 0, 0, 76, 30, 105, 0, 0, 73, 10, HC_OFF, 0, 0, HF_BP, 90, 20, 0, 0, 0, 0),
        HS(HM_DRUM, HW_SINE, 79, 0, 0, 50, 40, 117, 0, 0, 64, 0, HC_TICK, 40, 100, HF_OFF, 0, 0, 0, 0, 0, 0),
        HS(HM_DRUM, HW_TRI, 28, 0, 4, 100, 20, 121, 36, 70, 64, 0, HC_POP, 40, 30, HF_LP, 90, 10, 20, 0, 0, 0),
        HS(HM_SNARE, HW_TRI, 52, 0, 0, 80, 30, 94, 20, 40, 64, 0, HC_OFF, 0, 0, HF_LP, 104, 10, 0, 80, 96, 0)}},
};
