/*
 * lxr_dsp.c: the drum voices of the LXR-02 firmware, for KARMA's drum track
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 * (In KARMA: their NOTICE.md is LICENSES/LXR-NOTICE.md, their LICENSE is KARMA's LICENSE, the same GPL-3.0.)
 *
 * Portions Copyright (C) 2026 Ariel Muchnik: adapted for KARMA, custom firmware for the M-VAVE FM-1 (a modified
 * version, not the LXR's firmware; not affiliated with or endorsed by Erica Synths).
 *
 * From the LXR firmware's Core/Src/Soundengine/DSPAudio: DrumVoice.c (calcDrumVoiceAsync, calcDrumVoiceSyncBlock,
 * Drum_trigger), Snare.c, CymbalVoice.c, HiHat.c (their _trigger, _calcAsync, _calcSyncBlock), Oscillator.c,
 * transientGenerator.c, SlopeEg2.c, Decay.c, snapEg.c, ResonantFilter.c (SVF_calcBlockZDF and its helpers),
 * distortion.c, BufferTools.h, mixer.c (mixer_decimateBlock); the parameters' curves from MidiParser.c and
 * valueShaper.h. The same algorithms, in the same order, block for block (an envelope step and the pitch every 64
 * samples, OUTPUT_DMA_SIZE with USE_BIG_SAMPLE_BUFFER; the int16_t stages with their saturations and truncations).
 * What KARMA changed:
 *   - the oscillators: the sine from KARMA's 1024-point table (theirs: 4096 points, the same curve); of the LXR's
 *     band-limited wavetables (11 octaves x 3 waves, 76 KB) only the saw and the square it plays under 440 Hz
 *     (lxr_waves.h), above that polyBLEP waves; the triangle computed plain; noise: their sample-and-hold of a random
 *     number at the oscillator's rate, the random numbers from a xorshift (theirs: the STM32's hardware RNG)
 *   - the transient samples: 9 of 12, trimmed (lxr_transients.h); one not kept plays a kept one
 *   - a voice is a C struct of its own (KARMA plays up to 6 at once, from any of 16 sounds), its parameters given as
 *     the LXR's 0..127 values in lx_par_t; the fader, the LFOs, the velocity routing, pan and the trigger outputs are
 *     not here (KARMA's drum track has its own); the volume and drive smoothers taken as settled
 *   - the sample rate is 44100 Hz (the LXR: 44140); the Cortex-M7's float conversions (saturating) written out
 *   - float where the LXR used double (the amplitude envelope); a float too large for an int16_t is held at the
 *     range's end (theirs: wraps around, a click)
 */
#ifndef LX_FS
#define LX_FS 44100.0f                   /* REAL_FS */
#endif
#define LX_BLOCK 64u                     /* OUTPUT_DMA_SIZE */

enum { LW_SINE, LW_TRI, LW_SAW, LW_REC, LW_NOISE, LW_PWM, LW_COUNT };                       /* OscWaveforms */
enum { LF_LP = 1, LF_HP, LF_BP, LF_UBP, LF_NOTCH, LF_PEAK, LF_LP2, LF_OFF };                 /* filterTypeEnum */
enum { LV_DRUM, LV_SNARE, LV_CYM, LV_HAT };                                                 /* the voices */
enum { LE_STOPPED, LE_A, LE_D, LE_REPEAT };                                                  /* SlopeEg2 states */

/* a sound: the LXR's own parameter values (its CC values, 0..127, as MidiParser.c takes them) */
typedef struct {
    uint8_t kind;                        /* LV_* */
    uint8_t wave, coarse, fine;          /* OSC_WAVE, COARSE (a note; 60 + the note played - 60), FINE (64: +0.2 cents) */
    uint8_t mwave, mcoarse, mamt;        /* DRUM: MOD_WAVE, FM_FREQ, FMAMNT. CYM / HAT: WAVE2, MOD_OSC_F1, MOD_OSC_GAIN1 */
    uint8_t m2wave, m2coarse, m2amt;     /* CYM / HAT: WAVE3, MOD_OSC_F2, MOD_OSC_GAIN2 */
    uint8_t mix;                         /* DRUM: MIX_MOD (0 FM, 1 mix); SNARE: SNARE_MIX (the noise) */
    uint8_t noisef;                      /* SNARE: SNARE_NOISE_F */
    uint8_t atk, dec, slope, repeat;     /* VELOA, VELOD, VOL_SLOPE, REPEAT (SNARE, CYM) */
    uint8_t pamt, pdec, pslope;          /* DRUM, SNARE: MODAMNT, PITCHD, PITCH_SLOPE */
    uint8_t twave, tvol, tfreq;          /* TRANSx_WAVE (0 snap, 1 offset, 2.. the samples), _VOL, _FREQ */
    uint8_t ftype, fcut, fres, fdrive;   /* FILTER_TYPE (1..8: filterTypeEnum), FILTER_FREQ, RESO, FILTER_DRIVE */
    uint8_t dist, decim, vol, pwm;       /* OSCx_DIST / *_DISTORTION, VOICE_DECIMATION (127: none), VOL, PWM */
} lx_par_t;

typedef struct {                         /* OscInfo: what the sound needs of it */
    uint32_t phase, inc;
    float freq;
    int16_t out;                         /* (the noise's held value) */
    uint8_t wave, band;                  /* band: tableOffset, the wavetable of the octave (here: 4 or not) */
} lx_osc_t;

typedef struct {
    lx_osc_t osc, mod, mod2;             /* DRUM: osc, modOsc. SNARE: osc, noiseOsc. CYM / HAT: osc, modOsc, modOsc2 */
    float pitchMod, fmMod;
    float pval;                          /* DecayEg (the pitch) */
    float snap;                          /* SnapEg */
    float aval, gain, lastGain;          /* SlopeEg2's value; DRUM: ampFilterInput / lastGain, the others: egValueOscVol */
    float s1, s2, fa, fb;                /* ResonantFilter */
    float dcnt;                          /* the decimator */
    float velo;
    uint32_t tph;                        /* TransientGenerator */
    int16_t dheld;
    uint8_t astate, arep, kind, pos;     /* SlopeEg2 state, repeatCnt; the voice; the sample in the block of 64 */
} lx_voice_t;

/* ------------------------------------------------------------------------------------------ helpers --- */
static uint32_t lx_rng_state = 0x2545F491u;
static inline uint32_t lx_rng(void)                    /* GetRngValue (there the STM32's RNG) */
{
    uint32_t x = lx_rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return lx_rng_state = x;
}
static inline uint32_t lx_u32(float x)                 /* (uint32_t)float as the Cortex-M7 does it: saturating */
{
    if (!(x > 0.0f))
        return 0;
    if (x >= 4294967296.0f)
        return 0xFFFFFFFFu;
    return (uint32_t)x;
}
static inline int16_t lx_i16(float x)                 /* a float into an int16_t: truncated (theirs wraps around */
{                                                     /* past the range, here it is held at its end) */
    return (int16_t)(x >= 32767.0f ? 32767 : x <= -32768.0f ? -32768 : (int32_t)x);
}
static inline int16_t lx_sat16(int32_t x) { return (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x); }   /* __SSAT, __QADD16 */
static inline float lx_fabsf(float x) { return x < 0.0f ? -x : x; }
static inline float lx_shape(float val, float shape)  /* valueShaperF2F */
{
    const float k = 2 * shape / (1.0001f - shape);
    return ((1 + k) * val / (1 + k * lx_fabsf(val)));
}

/* MidiNoteFrequencies[n] (note 0: 8.18 Hz) */
static float lx_note_hz(int32_t n)
{
    static const float SEMI[12] = {8.1757989156f, 8.6619572180f, 9.1770239974f, 9.7227182413f, 10.3008611535f,
                                   10.9133822323f, 11.5623257097f, 12.2498573744f, 12.9782717994f, 13.7500000000f,
                                   14.5676175474f, 15.4338531643f};
    n = n < 0 ? 0 : n > 127 ? 127 : n;
    return SEMI[n % 12] * (float)(1u << (n / 12));
}
static float lx_detune(uint32_t value)                 /* midiParser_calcDetune: +- half a semitone */
{
    float frac = (value / 127.f - 0.5f);
    return 1 + frac * (1.0594630943592952645618252949463f - 1);
}
static void lx_osc_note(lx_osc_t *o, uint32_t coarse, uint32_t fine, uint32_t note)   /* osc_setBaseNote */
{
    o->freq = lx_note_hz((int32_t)coarse + ((int32_t)note - 60)) * lx_detune(fine);
}
static void lx_osc_freq(lx_osc_t *o, float pitchMod, int table)  /* osc_setFreq (modNodeValue 1) */
{
    const float f = o->freq * pitchMod;
    if (!table && (o->wave == LW_SINE || o->wave == LW_NOISE)) {
        o->inc = lx_u32((((4096 * f) / LX_FS)) * 1048576);        /* freq2PhaseIncr */
    } else {
        const uint32_t r = lx_u32(f / 440.f);                     /* freqToTableIndex: 4 under 440 Hz, then an octave */
        o->inc = lx_u32((((1024 * f) / LX_FS)) * 4194304);        /* freq2PhaseIncr1024 */
        o->band = (uint8_t)(r ? 5u + (31u - (uint32_t)__builtin_clz(r)) : 4u);
    }
}

/* the waves, int16_t as the tables give them: sine_table is -cos, sawTable falls from +1 (its jump at 0), recTable
 * +1 then -1, triTable -1 at 0, +1 at the half. Under 440 Hz (band 4) the saw and the square are their tables
 * (lxr_waves.h), linearly interpolated as theirs; above, polyBLEP waves (their tables there: fewer harmonics, the
 * octave's). dt: the phase increment in cycles (the polyBLEPs' width), idt 1 / dt */
#include "lxr_waves.h"
static inline int32_t lx_saw4(uint32_t i)             /* sawTable[4][i], i 0..1024 */
{
    return i <= 512u ? LX_SAW4[i] : -LX_SAW4[1024u - i];
}
static inline int32_t lx_table4(uint32_t wave, uint32_t ph)   /* calcWavetableOscBlock's interpolation */
{
    const uint32_t i = ph >> 22, fr = (ph >> 7) & 0x7FFFu;
    const int32_t a = wave == LW_SAW ? lx_saw4(i) : LX_REC4[i], b = wave == LW_SAW ? lx_saw4(i + 1u) : LX_REC4[i + 1u];
    return (int16_t)(a + (int16_t)(((b - a) * (int32_t)fr) >> 15));
}
static inline int32_t lx_sine(uint32_t ph)
{
    uint32_t p = ph - 0x40000000u, i = p >> 22;
    int32_t a = SINE[i], b = SINE[(i + 1u) & 1023u];
    return a + (((b - a) * (int32_t)((p >> 7) & 0x7FFFu)) >> 15);
}
static inline float lx_blep(float x, float dt, float idt)  /* the polyBLEP residual of a step of +2 at x = 0 */
{
    float t;
    if (x < dt) {
        t = x * idt;
        return t + t - t * t - 1.0f;
    }
    if (x > 1.0f - dt) {
        t = (x - 1.0f) * idt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}
static inline int32_t lx_saw(uint32_t ph, float dt, float idt)
{
    float x = (float)(ph >> 8) * (1.0f / 16777216.0f);
    return (int32_t)(32767.0f * (1.0f - 2.0f * x + lx_blep(x, dt, idt)));
}
static inline int32_t lx_wave(uint32_t wave, uint32_t ph, float dt, float idt, float pwm, uint32_t band)
{
    float x;
    if (band == 4u && wave != LW_TRI) {
        if (wave != LW_PWM)
            return lx_table4(wave, ph);
        return lx_sat16(lx_table4(LW_SAW, ph) - lx_table4(LW_SAW, lx_u32(ph + pwm * (float)(0xFFFFFFFFu / 2))));
    }
    switch (wave) {
    case LW_SINE:
        return lx_sine(ph);
    case LW_TRI:
        x = (float)(ph >> 8) * (1.0f / 16777216.0f);
        return (int32_t)(32767.0f * (x < 0.5f ? 4.0f * x - 1.0f : 3.0f - 4.0f * x));
    case LW_SAW:
        return lx_saw(ph, dt, idt);
    case LW_REC: {
        float y, x2;
        x = (float)(ph >> 8) * (1.0f / 16777216.0f);
        x2 = x < 0.5f ? x + 0.5f : x - 0.5f;
        y = (x < 0.5f ? 1.0f : -1.0f) + lx_blep(x, dt, idt) - lx_blep(x2, dt, idt);
        return (int32_t)(32767.0f * y);
    }
    default: {                                        /* PWM: two saws, the second pwm x half a cycle on (calcPwmBlock) */
        const uint32_t ph2 = lx_u32(ph + pwm * (float)(0xFFFFFFFFu / 2));   /* (float: as theirs) */
        return lx_sat16(lx_saw(ph, dt, idt) - lx_saw(ph2, dt, idt));
    }
    }
}
/* one sample of calcNextOscSampleBlock (fmMod < 0) or calcNextOscSampleFmBlock (modulated by mod x fmMod) */
static inline int16_t lx_osc(lx_osc_t *o, float gain, int32_t mod, float fmMod, float dt, float idt, float pwm)
{
    int32_t v;
    if (o->wave == LW_NOISE) {                         /* calcNoiseBlock: a new random value at each wrap */
        const uint32_t last = o->phase;
        o->phase += o->inc;
        if (last > o->phase)
            o->out = (int16_t)lx_rng();
        return lx_i16(o->out * gain);
    }
    if (fmMod < 0.0f)
        v = lx_wave(o->wave, o->phase, dt, idt, pwm, o->band);
    else                                              /* calcFmSineBlock (<< 17), calcFmBlock / calcPwmFmBlock (<< 19) */
        v = lx_wave(o->wave, o->phase + (lx_u32(mod * fmMod) << (o->wave == LW_SINE ? 17 : 19)), dt, idt, pwm, o->band);
    o->phase += o->inc;
    return lx_i16((int16_t)v * gain);
}

/* ------------------------------------------------------------------------------------------ envelopes --- */
static float lx_eg_shape(float val, float slope) { return (1 + slope) * val / (1 + slope * lx_fabsf(val)); }
static float lx_pitch_time(uint32_t data2)            /* DecayEg_calcTime (USE_BIG_SAMPLE_BUFFER) */
{
    const float K = (2 * 0.99f / (1.f - 0.99f)), val = (data2) / 127.f;
    const float ret = (1.f - ((1.f + K) * val / (1.f + K * lx_fabsf(val)))) * 2.f;
    return ret > 1.f ? 1.f : ret;
}
static float lx_pitch_slope(uint32_t data2)           /* DecayEg_setSlope */
{
    const float amount = ((data2 / 127.f) - 0.5f) * 2;
    return 2 * amount / (1 - amount);
}
static float lx_eg_time(uint32_t data2, float amount) /* slopeEg2_calcTime (USE_BIG_SAMPLE_BUFFER) */
{
    const float val = (data2) / 127.0f;
    const float ret = (1.0f - ((1.0f + amount) * val / (1.0f + amount * lx_fabsf(val)))) * 2.f;
    return ret > 1.f ? 1.f : ret;
}
#define LX_K_ATTACK (2 * 0.99f / (1.0f - 0.99f))
#define LX_K_DECAY (2 * 0.999f / (1.0f - 0.999f))
static float lx_eg_slope(uint32_t data2, int inv)    /* slopeEg2_setSlope: slope, invSlope */
{
    const float amount = ((data2 / 127.f) - 0.5f) * 1.999f * (inv ? -1.0f : 1.0f);
    return 2 * amount / (1 - amount);
}
static float lx_decay_eg(lx_voice_t *v, const lx_par_t *p)   /* DecayEg_calc */
{
    float val = v->pval;
    if (val > 0)
        val -= lx_pitch_time(p->pdec);
    else
        val = 0;
    v->pval = val;
    return lx_eg_shape(val, lx_pitch_slope(p->pslope));
}
static float lx_snap_eg(lx_voice_t *v, float time)    /* SnapEg_calc */
{
    float ret = 0;
    if (v->snap > 0) {
        ret = v->snap * v->snap * 24.f;
        v->snap -= 0.2f * time;
    }
    return ret;
}
static void lx_amp_trigger(lx_voice_t *v, const lx_par_t *p)   /* slopeEg2_trigger */
{
    if (!p->repeat) {
        v->astate = LE_A;
    } else {
        v->astate = LE_REPEAT;
        v->aval = 1.0f;
    }
    v->arep = p->repeat;
}
static float lx_amp_eg(lx_voice_t *v, const lx_par_t *p)  /* slopeEg2_calc */
{
    float val = v->aval;
    switch (v->astate) {
    case LE_REPEAT:                                   /* the attack time as a second decay, repeatCnt times */
        if (val > 0) {
            val -= lx_eg_time(p->atk, LX_K_ATTACK);
        } else {
            v->arep--;
            val = 1.0f;
            if (!(v->arep > 0))
                v->astate = LE_D;
        }
        v->aval = val;
        return lx_eg_shape(val, lx_eg_slope(p->slope, 0));
    case LE_A:
        val += lx_eg_time(p->atk, LX_K_ATTACK);
        if (val >= 1.0f) {
            val = 1.0f;
            v->astate = LE_D;
        }
        v->aval = val;
        return lx_eg_shape(val, lx_eg_slope(p->slope, 1));
    case LE_D:
        if (val > 0) {
            val -= lx_eg_time(p->dec, LX_K_DECAY);
            v->aval = val;
            return lx_eg_shape(val, lx_eg_slope(p->slope, 0));
        }
        v->aval = 0;
        return 0;
    default:
        return 0.0f;
    }
}

/* ------------------------------------------------------------------------------------------ the filter --- */
typedef struct {
    float f, g, q, drive, R, ff, g0, f1, if1, fl2, ql2;   /* SVF_directSetFilterValue, SVF_setReso, SVF_setDrive; per block */
    float din;                                        /* drive / 0x7fff: the input's scale (a multiply a sample, not a divide) */
    uint32_t type;
} lx_svf_t;
static float lx_fast_tan(float x)
{
    const float x2 = x * x;
    const float A = -15 * x + x * x2;
    const float B = 3 * (-5 + 2 * x2);
    return A / B;
}
static void lx_svf_set(lx_svf_t *c, const lx_par_t *p)
{
    float q, d = p->fdrive / 127.f;
    c->type = p->ftype < LF_LP || p->ftype > LF_OFF ? LF_OFF : p->ftype;
    c->f = lx_shape(p->fcut / 127.f, -0.9f) * (0.5f * 0.90f);     /* FILTER_SHAPER */
    c->g = lx_fast_tan(3.14159265358979f * c->f);                   /* SVF_recalcFreq */
    q = 1 - p->fres / 127.f;
    c->q = q < 0.1f ? 0.02f : q;
    c->drive = 0.4f + (d) * (d) * 6;
    c->din = c->drive * (1.0f / (float)0x7fff);
    c->R = c->f >= 0.4499f ? 1 : c->q;
    c->ff = c->g * c->g;
    c->g0 = 1.f / (1.f + c->g * 2 * c->R);
    c->f1 = c->ff * c->g0;
    c->if1 = 1.f / (c->f1 + 1);                                     /* (theirs: a divide a sample) */
    c->fl2 = c->f * 2.21f;
    c->ql2 = (1 - c->q) * 1.4f + (1 - c->q) / (1.0f - c->fl2);   /* (theirs in double) */
}
static inline float lx_tanh_xdx(float x)
{
    const float a = x * x;
    return ((a + 105) * a + 945) / ((15 * a + 420) * a + 945);
}
static inline float lx_soft_clip2(float in) { return in * lx_tanh_xdx(0.5f * in); }
static inline float lx_fast_tanh(float var)
{
    if (var < -1.95f)
        return -1.0f;
    if (var > 1.95f)
        return 1.0f;
    return 4.15f * var / (4.29f + var * var);
}
/* one sample of SVF_calcBlockZDF (ENABLE_NONLINEAR_INTEGRATORS 0) */
static inline int16_t lx_svf(lx_voice_t *v, const lx_svf_t *c, int16_t in)
{
    const float f = c->g;
    float x, y0, y1, xx, s1c;
    if (c->type == LF_OFF)
        return in;
    x = lx_soft_clip2((float)in * c->din);           /* (2026-10-28: theirs divides; the FM-1's FPU divides slowly) */
    switch (c->type) {
    case LF_LP2:                                      /* the naive two-pole low-pass */
        v->fa += c->fl2 * ((x - v->fa) + c->ql2 * (v->fa - v->fb));
        v->fa = v->fa > 1 ? 1 : v->fa < -1 ? -1 : v->fa;
        v->fb += c->fl2 * (v->fa - v->fb);
        v->fb = v->fb > 1 ? 1 : v->fb < -1 ? -1 : v->fb;
        return lx_sat16((int32_t)(v->fb * 0x7fff));
    default:
        break;
    }
    y1 = (c->f1 * x + v->s2 + f * c->g0 * v->s1) * c->if1;
    xx = x - y1;
    s1c = lx_soft_clip2(v->s1);
    y0 = (s1c + f * xx) * c->g0;
    v->s1 = s1c + 2 * f * (xx - 2 * c->R * y0);
    v->s2 = (v->s2) + 2 * f * y0;
    switch (c->type) {
    case LF_LP:
        return lx_sat16((int32_t)(lx_fast_tanh(y1) * 0x7fff));
    case LF_HP:
        return lx_sat16((int32_t)((x - 2 * c->R * y0 - y1) * 0x7fff));
    case LF_BP:
        return lx_sat16((int32_t)(y0 * 0x7fff));
    case LF_UBP:
        return lx_sat16((int32_t)(2 * c->R * y0 * 0x7fff));
    case LF_NOTCH:
        return lx_sat16((int32_t)((x - 2 * c->R * y0) * 0x7fff));
    default:                                          /* LF_PEAK */
        return lx_sat16((int32_t)((y1 - (x - 2 * c->R * y0 - y1)) * 0x7fff));
    }
}

/* ------------------------------------------------------------------------------------------ transients --- */
#ifndef LX_TR_EXTERNAL
#include "lxr_transients.h"                            /* LX_TR[], lx_tr_of() */
#endif
static inline int16_t lx_transient(lx_voice_t *v, uint32_t wave, float vol, float pitch)   /* transient_calcBlock */
{
    const int8_t *d;
    uint32_t i, len;
    int16_t s;
    if (wave <= 1u || v->tph >= 2311061504u)
        return 0;
    i = v->tph >> 20;
    d = lx_tr_of(wave - 2u, &len);
    s = lx_i16(vol * (float)((i < len ? d[i] : 0) * 256) * (i < 2205u));
    v->tph = lx_u32(v->tph + (pitch * (1 << 20)));
    return s;
}

/* ------------------------------------------------------------------------------------------ the voices --- */
static void lx_waves(lx_voice_t *v, const lx_par_t *p)   /* the oscillators' waveforms (live, as MidiParser sets them) */
{
    v->osc.wave = p->wave < LW_COUNT ? p->wave : LW_SINE;
    v->mod.wave = v->kind == LV_SNARE ? LW_NOISE : p->mwave < LW_COUNT ? p->mwave : LW_SINE;
    v->mod2.wave = p->m2wave < LW_COUNT ? p->m2wave : LW_NOISE;
}
static void lx_notes(lx_voice_t *v, const lx_par_t *p, uint32_t note)
{
    lx_osc_note(&v->osc, p->coarse, p->fine, note);
    if (v->kind == LV_SNARE) {
        v->mod.freq = p->noisef / 127.f * 22000;      /* SNARE_NOISE_F (not transposed) */
    } else {
        lx_osc_note(&v->mod, p->mcoarse, 64, note);   /* (the mod oscs' fine: their init value, 64) */
        lx_osc_note(&v->mod2, p->m2coarse, 64, note);
    }
}
/* a hit: Drum_trigger, Snare_trigger, Cymbal_trigger, HiHat_trigger. note: the note played (60: the sound's own pitch),
 * vel 1..127. A fresh voice (all its state 0 but what is set here) */
static void lx_trigger(lx_voice_t *v, const lx_par_t *p, uint32_t note, uint32_t vel)
{
    float offset = 1;
    v->kind = p->kind;
    lx_waves(v, p);
    if (p->twave == 1u)                               /* offset mode */
        offset -= p->tvol / 127.f;
    if (v->kind == LV_DRUM) {
        if (v->osc.wave == LW_SINE)
            v->osc.phase = lx_u32(1024 + ((0x3ff << 20) - 1024) * offset);
        else if (v->osc.wave > LW_SINE && v->osc.wave <= LW_REC)
            v->osc.phase = lx_u32((0xff << 22) * offset);
        else
            v->osc.phase = 0;
        v->mod.phase = v->mod.wave == LW_SINE ? 1024 + ((0x3ff << 20) - 1024)
                     : v->mod.wave > LW_SINE && v->mod.wave <= LW_REC ? (0xff << 22) : 0;
        v->s1 = v->s2 = v->fa = v->fb = 0;            /* SVF_reset */
    } else {
        if (v->osc.wave == LW_SINE)
            v->osc.phase = lx_u32((0x3ff << 20) * offset);
        else if (v->osc.wave > LW_SINE && v->osc.wave <= LW_REC)
            v->osc.phase = lx_u32((0xff << 22) * offset);
        else
            v->osc.phase = 0;
        if (v->kind != LV_SNARE)
            v->mod.phase = v->mod2.phase = 0;
    }
    lx_notes(v, p, note);
    v->pitchMod = 1.0f;
    v->pval = 1.f;                                    /* DecayEg_trigger */
    lx_amp_trigger(v, p);
    v->velo = vel / 127.f;
    v->tph = 0;                                       /* transient_trigger */
    v->snap = 1;                                      /* SnapEg_trigger */
    v->pos = 0;
}
/* the start of a block of 64: calcDrumVoiceAsync, Snare_calcAsync, Cymbal_calcAsync, HiHat_calcAsync */
static void lx_async(lx_voice_t *v, const lx_par_t *p, uint32_t note)
{
    lx_waves(v, p);
    lx_notes(v, p, note);                             /* (osc_recalcFreq when a parameter moves) */
    if (v->kind <= LV_SNARE) {
        const float egPitchVal = lx_decay_eg(v, p), pa = p->pamt / 127.f;
        v->pitchMod = 1 + egPitchVal * (pa * pa * 32);                 /* calcPitchModAmount: PITCH_AMOUNT_FACTOR */
        if (v->kind == LV_SNARE)
            v->gain = lx_amp_eg(v, p);
        if (p->twave == 0u)
            v->pitchMod += lx_snap_eg(v, 1.f + ((p->tfreq / 33.9f) - 0.75f)) * (p->tvol / 127.f);
        if (v->kind == LV_DRUM) {
            v->fmMod = (p->mamt / 127.f) * egPitchVal;
            if (p->atk == 0u && (v->astate == LE_A || v->astate == LE_REPEAT)) {   /* attack 1: no ramp */
                v->gain = lx_amp_eg(v, p);
                v->lastGain = v->gain;
            } else {
                v->lastGain = v->gain;
                v->gain = lx_amp_eg(v, p);
            }
        }
    } else {
        v->gain = lx_amp_eg(v, p);
        if (p->twave == 0u)
            v->pitchMod = 1 + lx_snap_eg(v, 1.f + ((p->tfreq / 33.9f) - 0.75f)) * (p->tvol / 127.f);
    }
    lx_osc_freq(&v->osc, v->pitchMod, 0);
    lx_osc_freq(&v->mod, 1.0f, v->kind == LV_SNARE);   /* (the snare's noise osc keeps its init waveform, 1: TRI) */
    if (v->kind >= LV_CYM)
        lx_osc_freq(&v->mod2, 1.0f, 0);
}
static inline float lx_dist_shape(uint32_t d)         /* setDistortionShape */
{
    float shape = d / 127.f - 0.0078125f;
    return 2 * (shape) / (1 - (shape));
}
static inline float lx_dt(const lx_osc_t *o) { float dt = (float)(o->inc >> 8) * (1.0f / 16777216.0f); return dt > 0.5f ? 0.5f : dt; }

/* n samples of a voice (calcDrumVoiceSyncBlock, Snare_calcSyncBlock, Cymbal_calcSyncBlock, HiHat_calcSyncBlock, then
 * mixer_decimateBlock) into out[], as the LXR's int16_t; the envelopes stepped every 64. note: as lx_trigger's.
 * Returns 0 when the voice has rung out */
static int lx_render(lx_voice_t *v, const lx_par_t *p, int16_t *out, uint32_t n, uint32_t note)
{
    lx_svf_t c;
    const float tvol = p->tvol / 127.f, tpitch = 1.f + ((p->tfreq / 33.9f) - 0.75f), vol = p->vol / 127.f;
    const float shape = lx_dist_shape(p->dist), drate = lx_shape(p->decim / 127.f, -0.7f), pwm = p->pwm / 127.f;
    const uint32_t tw = p->twave;
    uint32_t i = 0;
    lx_svf_set(&c, p);
    if (!(v->s1 == v->s1) || !(v->s2 == v->s2))      /* removeMaliciousSample */
        v->s1 = v->s2 = 0;
    while (i < n) {
        uint32_t m, k;
        float dt0, idt0, dt1, idt1, dt2, idt2;
        if (!v->pos)
            lx_async(v, p, note);
        m = LX_BLOCK - v->pos < n - i ? LX_BLOCK - v->pos : n - i;
        dt0 = lx_dt(&v->osc), idt0 = dt0 > 0.0f ? 1.0f / dt0 : 0.0f;
        dt1 = lx_dt(&v->mod), idt1 = dt1 > 0.0f ? 1.0f / dt1 : 0.0f;
        dt2 = lx_dt(&v->mod2), idt2 = dt2 > 0.0f ? 1.0f / dt2 : 0.0f;
        switch (v->kind) {
        case LV_DRUM: {
            const float fma = p->mamt / 127.f, dg = (v->gain - v->lastGain) * (1.0f / (LX_BLOCK - 1u));
            for (k = 0; k < m; k++) {
                int16_t mb = lx_osc(&v->mod, fma, 0, -1.0f, dt1, idt1, pwm), b;
                float x;
                if (p->mix) {                         /* the two oscillators mixed */
                    b = lx_osc(&v->osc, 1.f - fma, 0, -1.0f, dt0, idt0, pwm);
                    b = lx_sat16(b + mb);
                } else {                              /* the mod osc modulating the main one */
                    b = lx_osc(&v->osc, 1.0f, mb, v->fmMod, dt0, idt0, pwm);
                }
                b = lx_sat16(b + lx_transient(v, tw, tvol, tpitch));
                b = lx_svf(v, &c, b);
                b = lx_i16(b * (v->lastGain + (float)(v->pos + k) * dg));        /* bufferTool_addGainInterpolated */
                b = lx_i16(b * v->velo);
                x = b * (1.0f / 32767.f);             /* calcDistBlockFiltered */
                x = (1 + shape) * x / (1 + shape * lx_fabsf(x));
                b = lx_i16(x * 32767);
                out[i + k] = lx_i16(b * vol);
            }
            break;
        }
        case LV_SNARE: {
            const float mix = p->mix / 127.f, cv = vol * v->velo * v->gain;
            for (k = 0; k < m; k++) {
                const int16_t nz = lx_svf(v, &c, lx_osc(&v->mod, 0.9f, 0, -1.0f, dt1, idt1, pwm));
                const int16_t o = lx_osc(&v->osc, 1.f - mix, 0, -1.0f, dt0, idt0, pwm);
                float tmp = nz * mix + o, x;
                int16_t b;
                tmp += lx_transient(v, tw, tvol, tpitch);
                b = lx_i16(cv * tmp);
                x = b * (1.0f / 32767.f);             /* calcDistBlock */
                x = (1 + shape) * x / (1 + shape * lx_fabsf(x));
                out[i + k] = lx_i16(x * 32767);
            }
            break;
        }
        default: {                                    /* CYM, HAT */
            const float a1 = p->mamt / 127.f, a2 = p->m2amt / 127.f, cv = vol * v->velo * v->gain;
            const float og = v->kind == LV_HAT ? 0.5f : 1.f;
            for (k = 0; k < m; k++) {
                const int16_t m1 = lx_osc(&v->mod, a1, 0, -1.0f, dt1, idt1, pwm);
                const int16_t m2 = lx_osc(&v->mod2, a2, 0, -1.0f, dt2, idt2, pwm);
                int16_t b = lx_osc(&v->osc, og, lx_sat16(m1 + m2), 1.0f, dt0, idt0, pwm);
                float x;
                b = lx_svf(v, &c, b);
                b = lx_sat16(b + lx_transient(v, tw, tvol, tpitch));
                b = lx_i16(b * cv);
                x = b * (1.0f / 32767.f);
                x = (1 + shape) * x / (1 + shape * lx_fabsf(x));
                out[i + k] = lx_i16(x * 32767);
            }
            break;
        }
        }
        if (drate >= 1.0f) {                          /* (no decimation: every sample held as it is) */
            v->dheld = out[i + m - 1u];
            v->dcnt = 0.0f;
        } else for (k = 0; k < m; k++) {              /* mixer_decimateBlock */
            v->dcnt += drate;
            if (v->dcnt >= 1.f) {
                v->dcnt -= 1.f;
                v->dheld = out[i + k];
            }
            out[i + k] = v->dheld;
        }
        v->pos = (uint8_t)((v->pos + m) & (LX_BLOCK - 1u));
        i += m;
    }
    v->s1 = v->s1 > -1e-20f && v->s1 < 1e-20f ? 0.0f : v->s1;   /* (no denormals) */
    v->s2 = v->s2 > -1e-20f && v->s2 < 1e-20f ? 0.0f : v->s2;
    return !(v->astate == LE_D && v->aval <= 0.0f && v->gain == 0.0f && v->lastGain == 0.0f && v->dheld == 0);
}
