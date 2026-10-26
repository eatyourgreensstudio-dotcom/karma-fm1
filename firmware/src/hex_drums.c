/* SPDX-License-Identifier: GPL-3.0-only */
/* HEX: the drum track's synthesised kits. Each of a kit's 16 sounds is one of the drum voices of the LXR-02 drum
 * machine's open-source firmware by Erica Synths (GPL-3.0-only), ported in lxr_dsp.c: a hex_snd_t (22 bytes, the
 * layout of the MY HEX kits and of the editor's DRUM SYNTH commands) holds the voice's parameters, as the LXR's
 * 0..127 values; hex_par() gives them to the voice. Its MODEL is the voice:
 *   DRUM    the LXR's three drum voices: an oscillator, frequency-modulated by a second one (or mixed with it),
 *           a pitch envelope, a transient, the filter, the amplitude envelope, the distortion: kicks, toms, percussion
 *   SNARE   its snare voice: an oscillator with a pitch envelope, a noise (sample-and-hold at NOISE FREQ) through the
 *           filter, MIX between them, the transient
 *   CLAP    the snare voice too (the LXR has no clap: one makes it from the snare with REPEAT, the attack phase
 *           played again as quick bursts before the tail, and the noise alone)
 *   CYM     its cymbal voice: an oscillator frequency-modulated by two others (one often noise), the filter
 *   HAT     its hi-hat voice: the same at half the level (the open and the closed hat: two sounds, two decays)
 * The bytes (what each model reads; the LXR's own parameter in [ ]):
 *   model   HM_*
 *   wave    the oscillator: SIN TRI SAW SQR NOIS, or its PWM wave at a width (P12 P25 P37)          [OSC_WAVE, PWM]
 *   coarse  its note, 0..127 (the lane's note offset added)                                         [COARSE]
 *   fine    DRUM, SNARE: the pitch envelope's SLOPE, -64..63 (0 straight)                           [PITCH_SLOPE]
 *           CYM, HAT: FINE, -64..63 (+-half a semitone)                                             [FINE]
 *   atk dec slope level   the amplitude envelope (and REPEAT's bursts: ATK), the level              [VELOA VELOD VOL_SLOPE VOL]
 *   pamt pdec             DRUM, SNARE: the pitch envelope's amount and decay                       [MODAMNT PITCHD]
 *                         CYM, HAT: the second modulator's level and note                          [MOD_OSC_GAIN2 MOD_OSC_F2]
 *   fmratio fmamt         DRUM: the mod oscillator's note, the FM depth (MIX: its level)           [FM_FREQ FMAMNT]
 *                         SNARE: the noise's rate, the noise's share                               [SNARE_NOISE_F SNARE_MIX]
 *                         CYM, HAT: the first modulator's note and level                           [MOD_OSC_F1 MOD_OSC_GAIN1]
 *   click clvl ctone      the transient: SNAP, OFFS(et), or a sample; its level; its pitch (SNAP: its speed)  [TRANS_*]
 *   ftype cut res drive   the filter: LP HP BP UBP NTCH PEAK LP2 OFF, cutoff, resonance, its drive [FILTER_*]
 *   aux1    DRUM: the mod oscillator's wave, FM or MIX (FMSIN .. MXPWM)                             [MOD_WAVE, MIX_MOD]
 *           SNARE, CLAP: REPEAT                                                                     [REPEAT]
 *           CYM, HAT: the two modulators' waves (SI/NO: a sine and a noise)                        [WAVE2, WAVE3]
 *   aux2    DIST, the distortion after the envelope                                                 [*_DIST]
 *   crush   the sample rate reduced, 0 off .. 126                                                   [VOICE_DECIMATION]
 * drums.c plays the kits HEX_KITS as kits 0..4. */
enum { HM_DRUM, HM_SNARE, HM_CLAP, HM_CYM, HM_HAT, HM_COUNT };
enum { HW_SINE, HW_TRI, HW_SAW, HW_SQR, HW_NOISE, HW_P12, HW_P25, HW_P37, HW_COUNT };
enum { HF_LP, HF_HP, HF_BP, HF_UBP, HF_NOTCH, HF_PEAK, HF_LP2, HF_OFF, HF_COUNT };
enum { HC_SNAP, HC_OFFS, HC_CLK, HC_CK2, HC_TICK, HC_KICK, HC_RIM, HC_DRIP, HC_HAT, HC_CLAP, HC_KK2, HC_SNR,
       HC_TOM, HC_SP2, HC_COUNT };
#define HX_MODW 6u                               /* aux1: the mod waves (SIN TRI SAW SQR NOIS PWM) */

typedef struct {
    uint8_t model, wave, coarse;                 /* HM_*, HW_*, the note */
    int8_t fine;                                 /* -64..63 */
    uint8_t atk, dec, slope, level;
    uint8_t pamt, pdec;
    uint8_t fmratio, fmamt;
    uint8_t click, clvl, ctone;                  /* HC_*, level, pitch */
    uint8_t ftype, cut, res, drive;              /* HF_*, .. */
    uint8_t aux1, aux2;
    uint8_t crush;                               /* 0..126 */
} hex_snd_t;
#define HEX_LANES 16
typedef struct {
    const char *name;
    hex_snd_t s[HEX_LANES];
} hex_kit_t;

#include "lxr_dsp.c"

typedef struct {
    const hex_snd_t *d;
    lx_voice_t lx;
    uint8_t note;                                /* 60 + the lane's note offset */
} hex_v_t;

static uint32_t hex_aux_max(uint32_t model)      /* the largest aux1 of a model */
{
    return model == HM_DRUM ? 2u * HX_MODW - 1u : model >= HM_CYM ? HX_MODW * HX_MODW - 1u : 127u;
}
/* a sound's bytes -> the LXR's parameters */
static void hex_par(const hex_snd_t *d, lx_par_t *p)
{
    static const uint8_t PWM[3] = {32, 64, 95};
    uint32_t m = d->model < HM_COUNT ? d->model : HM_DRUM, a1 = d->aux1 <= hex_aux_max(m) ? d->aux1 : 0u;
    int32_t f = d->fine + 64;
    memset(p, 0, sizeof *p);
    p->kind = m == HM_DRUM ? LV_DRUM : m <= HM_CLAP ? LV_SNARE : m == HM_CYM ? LV_CYM : LV_HAT;
    p->wave = d->wave < HW_P12 ? d->wave : d->wave < HW_COUNT ? LW_PWM : LW_SINE;
    p->pwm = d->wave >= HW_P12 && d->wave < HW_COUNT ? PWM[d->wave - HW_P12] : 64u;
    p->coarse = d->coarse & 127u;
    p->atk = d->atk & 127u;
    p->dec = d->dec & 127u;
    p->slope = d->slope & 127u;
    p->vol = d->level & 127u;
    p->twave = d->click < HC_COUNT ? d->click : HC_SNAP;
    p->tvol = d->clvl & 127u;
    p->tfreq = d->ctone & 127u;
    p->ftype = (uint8_t)((d->ftype < HF_COUNT ? d->ftype : HF_OFF) + 1u);
    p->fcut = d->cut & 127u;
    p->fres = d->res & 127u;
    p->fdrive = d->drive & 127u;
    p->dist = d->aux2 & 127u;
    p->decim = (uint8_t)(127u - (d->crush > 126u ? 126u : d->crush));
    f = f < 0 ? 0 : f > 126 ? 126 : f;                       /* (127: an infinite slope) */
    if (p->kind <= LV_SNARE) {
        p->fine = 64;
        p->pslope = (uint8_t)f;
        p->pamt = d->pamt & 127u;
        p->pdec = d->pdec & 127u;
        if (p->kind == LV_DRUM) {
            p->mcoarse = d->fmratio & 127u;
            p->mamt = d->fmamt & 127u;
            p->mwave = (uint8_t)(a1 % HX_MODW);
            p->mix = (uint8_t)(a1 >= HX_MODW);
        } else {
            p->noisef = d->fmratio & 127u;
            p->mix = d->fmamt & 127u;
            p->repeat = (uint8_t)a1;
        }
    } else {
        p->fine = (uint8_t)f;
        p->m2amt = d->pamt & 127u;
        p->m2coarse = d->pdec & 127u;
        p->mcoarse = d->fmratio & 127u;
        p->mamt = d->fmamt & 127u;
        p->mwave = (uint8_t)(a1 / HX_MODW);
        p->m2wave = (uint8_t)(a1 % HX_MODW);
    }
}

static void hex_on(hex_v_t *v, const hex_snd_t *kit, uint32_t note, uint32_t vel)
{
    int32_t semi;
    uint32_t lane = ds_lane(note, &semi);
    lx_par_t p;
    memset(v, 0, sizeof *v);
    v->d = &kit[lane % HEX_LANES];
    v->note = (uint8_t)(60 + semi);
    hex_par(v->d, &p);
    lx_trigger(&v->lx, &p, v->note, vel ? vel : 1u);
}

/* n samples of a voice into out[] (the drums' scale: +-32767 at full); 0 when it has rung out */
static int hex_render(hex_v_t *v, int32_t *out, uint32_t n)
{
    int16_t b[32];
    uint32_t i, j, k;
    int alive = 1;
    lx_par_t p;
    hex_par(v->d, &p);
    for (i = 0; i < n; i += k) {
        k = n - i < 32u ? n - i : 32u;
        alive = lx_render(&v->lx, &p, b, k, v->note);
        for (j = 0; j < k; j++)
            out[i + j] = b[j];
    }
    return alive;
}

/* ---- the kits: {MODEL, WAVE, COARSE, FINE, ATK, DEC, SLOPE, LEVEL, PAMT, PDEC, RATIO, FM, CLICK, CLVL, CTONE,
 * FILTER, CUT, RES, DRIVE, AUX1, AUX2, CRUSH}, in the LXR's units (lxr_dsp.c). DEC: 5 ~60 ms, 10 ~0.13 s, 20 ~0.27 s,
 * 30 ~0.45 s, 40 ~0.7 s, 60 ~1.3 s (to silence; SLOPE under 64 the faster the start of the fall); PDEC: 10 ~13 ms,
 * 20 ~28 ms, 40 ~70 ms; PAMT: 20 +10 semitones, 30 +18, 40 +25, 50 +31; CUT: 40 ~470 Hz, 64 ~1 kHz, 90 ~2.3 kHz, 100
 * ~3.2 kHz, 110 ~5 kHz, 120 ~9.4 kHz; DRIVE 40: unity into the filter; CTONE 25: the transient at its pitch. Lanes:
 * kick snare clap hat open-hat tom-lo tom-hi crash ride shaker conga rim cowbell clave kick-2 snare-2 */
#define HS(...) {__VA_ARGS__}
static const hex_kit_t HEX_KITS[5] = {
    {"HEX BOOM", {
        HS(HM_DRUM, HW_SINE, 31, -30, 0, 34, 40, 120, 40, 32, 43, 0, HC_KICK, 70, 25, HF_LP, 110, 10, 40, 0, 10, 0),
        HS(HM_SNARE, HW_TRI, 52, -20, 0, 18, 30, 113, 22, 22, 120, 85, HC_SNR, 60, 25, HF_HP, 100, 20, 52, 0, 10, 0),
        HS(HM_CLAP, HW_SINE, 60, 0, 8, 16, 30, 127, 0, 0, 115, 127, HC_CLAP, 50, 25, HF_BP, 84, 40, 50, 3, 10, 0),
        HS(HM_HAT, HW_SQR, 82, 0, 0, 4, 30, 96, 70, 120, 91, 60, HC_HAT, 40, 30, HF_HP, 112, 10, 40, 22, 10, 0),
        HS(HM_HAT, HW_SQR, 82, 0, 0, 20, 40, 52, 70, 120, 91, 60, HC_HAT, 40, 30, HF_HP, 112, 10, 40, 22, 10, 0),
        HS(HM_DRUM, HW_SINE, 43, -20, 0, 24, 40, 78, 20, 30, 50, 0, HC_TOM, 50, 25, HF_LP, 112, 0, 40, 0, 10, 0),
        HS(HM_DRUM, HW_SINE, 50, -20, 0, 22, 40, 74, 20, 28, 57, 0, HC_TOM, 50, 30, HF_LP, 114, 0, 40, 0, 10, 0),
        HS(HM_CYM, HW_SQR, 70, 0, 0, 55, 50, 28, 60, 118, 83, 80, HC_HAT, 40, 20, HF_HP, 106, 10, 40, 22, 10, 0),
        HS(HM_CYM, HW_TRI, 82, 0, 0, 46, 50, 49, 20, 120, 89, 50, HC_TICK, 30, 30, HF_BP, 112, 20, 40, 21, 10, 0),
        HS(HM_HAT, HW_NOISE, 120, 0, 10, 8, 40, 97, 0, 0, 60, 0, HC_OFFS, 0, 25, HF_HP, 115, 10, 40, 0, 10, 0),
        HS(HM_DRUM, HW_SINE, 57, -20, 0, 16, 40, 66, 10, 20, 64, 0, HC_TICK, 50, 25, HF_LP, 116, 0, 40, 0, 10, 0),
        HS(HM_DRUM, HW_TRI, 76, 0, 0, 5, 36, 127, 10, 10, 88, 40, HC_RIM, 110, 25, HF_BP, 108, 30, 72, 0, 20, 0),
        HS(HM_DRUM, HW_SQR, 73, 0, 0, 12, 34, 81, 0, 0, 80, 50, HC_OFFS, 0, 25, HF_BP, 82, 30, 66, 9, 10, 0),
        HS(HM_DRUM, HW_SINE, 98, 0, 0, 4, 40, 52, 4, 10, 98, 0, HC_CLK, 40, 25, HF_OFF, 127, 0, 40, 0, 10, 0),
        HS(HM_DRUM, HW_SINE, 29, -40, 0, 46, 50, 84, 50, 40, 41, 20, HC_KK2, 80, 25, HF_LP, 104, 20, 50, 0, 30, 0),
        HS(HM_SNARE, HW_SINE, 55, -20, 0, 14, 30, 127, 20, 18, 127, 100, HC_CLK, 70, 25, HF_BP, 108, 20, 52, 0, 20, 0)}},
    {"HEX ACID", {                                /* bright, tight, techno */
        HS(HM_DRUM, HW_SINE, 33, -40, 0, 28, 30, 124, 46, 26, 45, 10, HC_CLK, 90, 30, HF_LP, 106, 20, 60, 0, 30, 0),
        HS(HM_SNARE, HW_TRI, 55, -20, 0, 14, 30, 127, 26, 18, 124, 90, HC_CK2, 70, 30, HF_HP, 104, 30, 52, 0, 30, 0),
        HS(HM_CLAP, HW_SINE, 60, 0, 6, 14, 30, 127, 0, 0, 120, 127, HC_CLAP, 60, 30, HF_BP, 90, 50, 60, 4, 30, 0),
        HS(HM_HAT, HW_SQR, 86, 0, 0, 3, 30, 104, 80, 124, 95, 70, HC_CLK, 40, 40, HF_HP, 116, 20, 50, 22, 20, 0),
        HS(HM_HAT, HW_SQR, 86, 0, 0, 16, 40, 54, 80, 124, 95, 70, HC_CLK, 40, 40, HF_HP, 116, 20, 50, 22, 20, 0),
        HS(HM_DRUM, HW_SINE, 45, -30, 0, 20, 40, 92, 26, 24, 52, 10, HC_CLK, 60, 30, HF_LP, 110, 10, 50, 0, 20, 0),
        HS(HM_DRUM, HW_SINE, 52, -30, 0, 18, 40, 87, 26, 22, 59, 10, HC_CLK, 60, 30, HF_LP, 112, 10, 50, 0, 20, 0),
        HS(HM_CYM, HW_SQR, 74, 0, 0, 50, 50, 30, 80, 124, 87, 90, HC_HAT, 50, 30, HF_HP, 110, 20, 50, 22, 20, 0),
        HS(HM_CYM, HW_SQR, 84, 0, 0, 40, 50, 24, 30, 120, 93, 60, HC_TICK, 40, 40, HF_BP, 116, 30, 50, 21, 20, 0),
        HS(HM_HAT, HW_NOISE, 124, 0, 6, 6, 40, 101, 0, 0, 60, 0, HC_OFFS, 0, 25, HF_HP, 118, 20, 50, 0, 20, 0),
        HS(HM_DRUM, HW_SINE, 60, -30, 0, 12, 40, 85, 16, 16, 67, 10, HC_TICK, 60, 30, HF_LP, 118, 0, 50, 0, 20, 0),
        HS(HM_DRUM, HW_SQR, 78, 0, 0, 5, 36, 127, 12, 8, 90, 50, HC_RIM, 110, 30, HF_BP, 108, 40, 72, 0, 30, 0),
        HS(HM_DRUM, HW_SQR, 73, 0, 0, 12, 34, 105, 0, 0, 80, 60, HC_OFFS, 0, 25, HF_BP, 82, 30, 66, 9, 20, 0),
        HS(HM_DRUM, HW_SINE, 100, 0, 0, 3, 40, 79, 6, 8, 100, 0, HC_CLK, 50, 30, HF_OFF, 127, 0, 40, 0, 20, 0),
        HS(HM_DRUM, HW_SINE, 31, -40, 0, 40, 40, 98, 60, 30, 43, 30, HC_KICK, 90, 30, HF_LP, 100, 40, 70, 0, 50, 0),
        HS(HM_SNARE, HW_SQR, 58, -20, 0, 12, 30, 127, 26, 14, 127, 104, HC_SNR, 80, 30, HF_BP, 110, 30, 52, 0, 40, 0)}},
    {"HEX METAL", {                               /* FM, drive, industrial */
        HS(HM_DRUM, HW_SINE, 29, -30, 0, 36, 40, 120, 56, 28, 41, 40, HC_KICK, 80, 25, HF_LP2, 100, 40, 70, 0, 60, 0),
        HS(HM_SNARE, HW_SINE, 53, -20, 0, 18, 30, 127, 30, 20, 110, 80, HC_RIM, 80, 30, HF_BP, 100, 50, 52, 0, 50, 0),
        HS(HM_CLAP, HW_SINE, 60, 0, 8, 18, 30, 98, 0, 0, 100, 127, HC_CLAP, 70, 25, HF_BP, 82, 70, 70, 4, 50, 0),
        HS(HM_HAT, HW_SQR, 78, 0, 0, 4, 30, 54, 90, 110, 97, 90, HC_HAT, 50, 25, HF_HP, 110, 40, 60, 19, 40, 0),
        HS(HM_HAT, HW_SQR, 78, 0, 0, 22, 40, 26, 90, 110, 97, 90, HC_HAT, 50, 25, HF_HP, 110, 40, 60, 19, 40, 0),
        HS(HM_DRUM, HW_SINE, 40, -20, 0, 26, 40, 92, 30, 30, 59, 40, HC_TOM, 60, 25, HF_LP, 108, 20, 60, 0, 40, 0),
        HS(HM_DRUM, HW_SINE, 47, -20, 0, 24, 40, 81, 30, 28, 66, 40, HC_TOM, 60, 30, HF_LP, 110, 20, 60, 0, 40, 0),
        HS(HM_CYM, HW_SQR, 66, 0, 0, 60, 50, 22, 100, 116, 85, 110, HC_HAT, 50, 20, HF_HP, 104, 30, 60, 19, 40, 0),
        HS(HM_CYM, HW_SINE, 78, 0, 0, 50, 50, 21, 40, 116, 91, 90, HC_TICK, 50, 30, HF_BP, 110, 50, 60, 3, 40, 0),
        HS(HM_HAT, HW_NOISE, 116, 0, 8, 8, 40, 65, 0, 0, 60, 0, HC_OFFS, 0, 25, HF_BP, 114, 40, 60, 0, 40, 0),
        HS(HM_DRUM, HW_SINE, 55, -20, 0, 14, 40, 79, 20, 18, 74, 50, HC_TICK, 60, 25, HF_LP, 116, 10, 60, 0, 40, 0),
        HS(HM_DRUM, HW_SQR, 80, 0, 0, 5, 36, 106, 14, 8, 99, 80, HC_RIM, 110, 30, HF_BP, 108, 60, 72, 0, 60, 0),
        HS(HM_DRUM, HW_SQR, 73, 0, 0, 12, 34, 107, 0, 0, 80, 80, HC_OFFS, 0, 25, HF_BP, 82, 30, 66, 3, 40, 0),
        HS(HM_DRUM, HW_SINE, 94, 0, 0, 4, 40, 82, 8, 8, 106, 60, HC_CLK, 60, 30, HF_OFF, 127, 0, 40, 0, 40, 0),
        HS(HM_DRUM, HW_SAW, 31, -40, 0, 40, 40, 107, 60, 34, 31, 20, HC_KK2, 90, 25, HF_LP2, 92, 60, 80, 0, 80, 0),
        HS(HM_SNARE, HW_SQR, 50, -20, 0, 16, 30, 72, 30, 20, 105, 100, HC_SNR, 80, 25, HF_HP, 100, 50, 52, 0, 70, 0)}},
    {"HEX GLITCH", {                              /* crushed, snapped, short */
        HS(HM_DRUM, HW_SINE, 33, -50, 0, 22, 30, 124, 64, 18, 45, 0, HC_SNAP, 60, 40, HF_LP, 110, 30, 50, 0, 40, 60),
        HS(HM_SNARE, HW_SQR, 57, -30, 0, 10, 30, 127, 40, 12, 90, 96, HC_CK2, 90, 40, HF_HP, 106, 30, 52, 0, 40, 70),
        HS(HM_CLAP, HW_SINE, 60, 0, 5, 10, 30, 82, 0, 0, 80, 127, HC_CLAP, 70, 40, HF_BP, 100, 60, 60, 5, 40, 60),
        HS(HM_HAT, HW_P25, 90, 0, 0, 2, 30, 127, 70, 100, 101, 80, HC_CLK, 50, 50, HF_HP, 116, 30, 66, 22, 30, 80),
        HS(HM_HAT, HW_P25, 90, 0, 0, 12, 40, 83, 70, 100, 101, 80, HC_CLK, 50, 50, HF_HP, 116, 30, 60, 22, 30, 80),
        HS(HM_DRUM, HW_SQR, 45, -40, 0, 14, 30, 73, 50, 12, 52, 20, HC_SNAP, 60, 40, HF_LP, 104, 40, 50, 0, 30, 60),
        HS(HM_DRUM, HW_SQR, 52, -40, 0, 12, 30, 70, 50, 10, 59, 20, HC_SNAP, 60, 40, HF_LP, 106, 40, 50, 0, 30, 60),
        HS(HM_CYM, HW_SQR, 76, 0, 0, 40, 50, 35, 80, 100, 89, 100, HC_HAT, 50, 50, HF_HP, 110, 30, 50, 22, 30, 90),
        HS(HM_CYM, HW_P12, 88, 0, 0, 30, 50, 31, 40, 110, 95, 70, HC_TICK, 50, 50, HF_HP, 112, 40, 50, 21, 30, 70),
        HS(HM_HAT, HW_NOISE, 110, 0, 0, 4, 40, 127, 0, 0, 60, 0, HC_OFFS, 0, 25, HF_HP, 116, 30, 66, 0, 30, 100),
        HS(HM_DRUM, HW_TRI, 62, -40, 0, 10, 30, 89, 36, 8, 69, 0, HC_SNAP, 50, 50, HF_OFF, 127, 0, 40, 0, 30, 60),
        HS(HM_DRUM, HW_SQR, 82, 0, 0, 5, 36, 120, 20, 4, 101, 70, HC_RIM, 110, 50, HF_BP, 108, 50, 72, 0, 40, 80),
        HS(HM_DRUM, HW_P25, 73, 0, 0, 12, 34, 100, 0, 0, 80, 60, HC_OFFS, 0, 25, HF_BP, 82, 30, 66, 11, 30, 70),
        HS(HM_DRUM, HW_SINE, 104, 0, 0, 2, 40, 78, 20, 4, 104, 40, HC_SNAP, 60, 60, HF_OFF, 127, 0, 40, 0, 30, 50),
        HS(HM_DRUM, HW_SAW, 31, -50, 0, 30, 30, 103, 70, 16, 43, 30, HC_SNAP, 70, 30, HF_LP, 100, 50, 60, 0, 60, 70),
        HS(HM_SNARE, HW_NOISE, 100, 0, 0, 8, 30, 127, 0, 0, 70, 127, HC_CK2, 90, 50, HF_BP, 108, 60, 52, 0, 50, 100)}},
    {"HEX ROOM", {                                /* soft, round, warm */
        HS(HM_DRUM, HW_SINE, 29, -20, 0, 40, 50, 124, 30, 40, 41, 0, HC_KICK, 50, 20, HF_LP, 96, 0, 40, 0, 0, 0),
        HS(HM_SNARE, HW_SINE, 50, -10, 0, 20, 40, 127, 14, 24, 100, 70, HC_SNR, 50, 20, HF_LP, 104, 10, 52, 0, 0, 0),
        HS(HM_CLAP, HW_SINE, 60, 0, 10, 20, 40, 119, 0, 0, 90, 127, HC_CLAP, 40, 20, HF_BP, 80, 30, 60, 3, 0, 0),
        HS(HM_HAT, HW_SQR, 80, 0, 2, 5, 40, 83, 50, 116, 89, 40, HC_OFFS, 0, 25, HF_HP, 108, 0, 40, 22, 0, 0),
        HS(HM_HAT, HW_SQR, 80, 0, 2, 24, 50, 53, 50, 116, 89, 40, HC_OFFS, 0, 25, HF_HP, 108, 0, 40, 22, 0, 0),
        HS(HM_DRUM, HW_SINE, 41, -10, 0, 30, 50, 77, 14, 36, 48, 0, HC_TOM, 40, 20, HF_LP, 104, 0, 40, 0, 0, 0),
        HS(HM_DRUM, HW_SINE, 48, -10, 0, 28, 50, 69, 14, 34, 55, 0, HC_TOM, 40, 25, HF_LP, 106, 0, 40, 0, 0, 0),
        HS(HM_CYM, HW_TRI, 68, 0, 4, 64, 60, 50, 40, 114, 81, 60, HC_OFFS, 0, 25, HF_HP, 102, 0, 40, 22, 0, 0),
        HS(HM_CYM, HW_SINE, 80, 0, 0, 54, 60, 40, 20, 114, 87, 40, HC_TICK, 20, 25, HF_BP, 108, 10, 40, 3, 0, 0),
        HS(HM_HAT, HW_NOISE, 118, 0, 16, 10, 50, 87, 0, 0, 60, 0, HC_OFFS, 0, 25, HF_BP, 112, 10, 40, 0, 0, 0),
        HS(HM_DRUM, HW_SINE, 55, -10, 0, 20, 50, 62, 8, 24, 62, 0, HC_TICK, 30, 20, HF_LP, 110, 0, 40, 0, 0, 0),
        HS(HM_DRUM, HW_TRI, 72, 0, 0, 5, 36, 127, 8, 12, 84, 30, HC_RIM, 110, 20, HF_BP, 108, 20, 72, 0, 0, 0),
        HS(HM_DRUM, HW_SINE, 73, 0, 0, 12, 34, 127, 0, 0, 80, 40, HC_OFFS, 0, 25, HF_BP, 82, 30, 66, 6, 0, 0),
        HS(HM_DRUM, HW_SINE, 92, 0, 0, 6, 40, 51, 4, 10, 92, 0, HC_TICK, 20, 20, HF_OFF, 127, 0, 40, 0, 0, 0),
        HS(HM_DRUM, HW_TRI, 26, -20, 4, 50, 60, 127, 24, 50, 38, 0, HC_KICK, 40, 15, HF_LP, 90, 10, 40, 0, 0, 0),
        HS(HM_SNARE, HW_TRI, 52, -10, 0, 24, 40, 118, 10, 24, 84, 60, HC_OFFS, 0, 25, HF_LP, 106, 10, 52, 0, 0, 0)}},
};
