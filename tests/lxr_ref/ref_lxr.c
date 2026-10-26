/* SPDX-License-Identifier: GPL-3.0-only */
/* The LXR firmware's own drum voices (Erica Synths, GPL-3.0-only; their checkout, built by tests/lxr_ref/build.sh with
 * the stubs in tests/lxr_ref/stub), each sound of cases.h played as their mixer plays it: a block of 64 at a time,
 * SVF_recalcFreq, the voice's _calcAsync, its _calcSyncBlock, mixer_decimateBlock. The parameters are set the way
 * their MidiParser.c sets them (its cases, copied below). Writes OUTDIR/NN.raw (int16, mono) and OUTDIR/transients.bin
 * (their 12 x 2205 transient samples, for tests/lxr_test.c). The random numbers: the xorshift of KARMA's port, so that
 * the noise is the same. */
#include <stdio.h>
#include <stdlib.h>
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/DSPAudio/CymbalVoice.h"
#include "Soundengine/DSPAudio/HiHat.h"
#include "Soundengine/DSPAudio/transientTables.h"
#include "Soundengine/MIDI/valueShaper.h"
#include "Soundengine/globals.h"
#include "cases.h"

/* ---- what the voices call outside the DSP files */
RNG_HandleTypeDef hrng;
static uint32_t rng_state;
uint32_t HAL_RNG_GetRandomNumber(RNG_HandleTypeDef *h)
{
    uint32_t x = rng_state;
    (void)h;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return rng_state = x;
}
ModulationNode velocityModulators[6];
void modNode_updateValue(ModulationNode *m, float v) { (void)m, (void)v; }
float adc_getFaderVolume(uint8_t voice) { (void)voice; return 1.f; }
void lfo_init(Lfo *lfo) { (void)lfo; }
void lfo_retrigger(uint8_t voice) { (void)voice; }
/* MidiParser.c */
#define SEMITONE_UP 1.0594630943592952645618252949463f
float midiParser_calcDetune(uint8_t value)
{
    float frac = (value / 127.f - 0.5f);
    float cent = 1;
    cent += frac * (SEMITONE_UP - 1);
    return cent;
}
static inline float calcPitchModAmount(uint8_t data2)
{
    const float val = data2 / 127.f;
    return val * val * PITCH_AMOUNT_FACTOR;
}
static void set_coarse(OscInfo *o, uint8_t v) { o->midiFreq &= 0x00ff; o->midiFreq |= v << 8; osc_recalcFreq(o); }
static void set_fine(OscInfo *o, uint8_t v) { o->midiFreq &= 0xff00; o->midiFreq |= v; osc_recalcFreq(o); }
static void set_filter(ResonantFilter *f, uint8_t *type, const lxc_t *c)
{
    SVF_directSetFilterValue(f, valueShaperF2F(c->fcut / 127.f, FILTER_SHAPER));
    SVF_setReso(f, c->fres / 127.f);
    SVF_setDrive(f, c->fdrive / 127.f);
    *type = c->ftype;                                  /* (CC2_FILTER_TYPE: value + 1) */
}
static void set_trans(TransientGenerator *t, const lxc_t *c)
{
    transient_setWaveform(t, c->twave);
    t->volume = c->tvol / 127.f;
    t->pitch = 1.f + ((c->tfreq / 33.9f) - 0.75f);
}
/* mixer.c mixer_decimateBlock, for one voice */
static float dec_cnt;
static int16_t dec_held;
static void decimate(float rate, int16_t *buf)
{
    for (int i = 0; i < OUTPUT_DMA_SIZE; i++) {
        dec_cnt += rate;
        if (dec_cnt >= 1.f) {
            dec_cnt -= 1.f;
            dec_held = buf[i];
        }
        buf[i] = dec_held;
    }
}

static void render(const lxc_t *c, FILE *f)
{
    int16_t buf[OUTPUT_DMA_SIZE];
    const float rate = valueShaperF2F(c->decim / 127.f, -0.7f);
    uint32_t b, nb = (uint32_t)(LXC_SECONDS * 44100 / OUTPUT_DMA_SIZE);
    rng_state = 0x2545F491u;
    dec_cnt = 0;
    dec_held = 0;
    switch (c->kind) {
    case 0: {
        DrumVoice *v = &voiceArray[0];
        memset(voiceArray, 0, sizeof voiceArray);             /* (a fresh voice, as KARMA plays each hit) */
        initDrumVoice();
        v->osc.waveform = c->wave;
        v->modOsc.waveform = c->mwave;
        set_coarse(&v->osc, c->coarse);
        set_fine(&v->osc, c->fine);
        set_coarse(&v->modOsc, c->mcoarse);                       /* FMDTN */
        v->fmModAmount = c->mamt / 127.f;                         /* FMAMNT */
        v->mixOscs = c->mix != 0;                                 /* CC2_MIX_MOD */
        slopeEg2_setAttack(&v->oscVolEg, c->atk, AMP_EG_SYNC);
        slopeEg2_setDecay(&v->oscVolEg, c->dec, AMP_EG_SYNC);
        slopeEg2_setSlope(&v->oscVolEg, c->slope);
        DecayEg_setDecay(&v->oscPitchEg, c->pdec);
        DecayEg_setSlope(&v->oscPitchEg, c->pslope);
        v->egPitchModAmount = calcPitchModAmount(c->pamt);
        set_trans(&v->transGen, c);
        set_filter(&v->filter, &v->filterType, c);
        setDistortionShape(&v->distortion, c->dist / 127.f);     /* OSC1_DIST */
        v->vol = c->vol / 127.f;
        v->pwm = c->pwm / 127.f;
        v->MixerPageFilter[0].b = v->vol;                         /* (the smoothers settled) */
        v->FaderVolumeFilter.b = 1;
        v->distortion.DriveParamFilter.b = v->distortion.shape;
        Drum_trigger(0, c->vel, 60);
        for (b = 0; b < nb; b++) {
            SVF_recalcFreq(&v->filter);
            calcDrumVoiceAsync(0);
            calcDrumVoiceSyncBlock(0, buf, OUTPUT_DMA_SIZE);
            decimate(rate, buf);
            fwrite(buf, 2, OUTPUT_DMA_SIZE, f);
        }
        break;
    }
    case 1: {
        SnareVoice *v = &snareVoice;
        memset(&snareVoice, 0, sizeof snareVoice);
        Snare_init();
        v->osc.waveform = c->wave;
        set_coarse(&v->osc, c->coarse);
        set_fine(&v->osc, c->fine);
        v->noiseOsc.freq = c->noisef / 127.f * 22000;             /* SNARE_NOISE_F */
        v->mix = c->mix / 127.f;
        slopeEg2_setAttack(&v->oscVolEg, c->atk, false);
        slopeEg2_setDecay(&v->oscVolEg, c->dec, false);
        slopeEg2_setSlope(&v->oscVolEg, c->slope);
        v->oscVolEg.repeat = c->repeat;
        DecayEg_setDecay(&v->oscPitchEg, c->pdec);
        DecayEg_setSlope(&v->oscPitchEg, c->pslope);
        v->egPitchModAmount = calcPitchModAmount(c->pamt);
        set_trans(&v->transGen, c);
        set_filter(&v->filter, &v->filterType, c);
        setDistortionShape(&v->distortion, c->dist / 127.f);
        v->vol = c->vol / 127.f;
        v->pwm = c->pwm / 127.f;
        v->FaderVolumeFilter.b = 1;
        Snare_trigger(c->vel, 60);
        for (b = 0; b < nb; b++) {
            SVF_recalcFreq(&v->filter);
            Snare_calcAsync();
            Snare_calcSyncBlock(buf, OUTPUT_DMA_SIZE);
            decimate(rate, buf);
            fwrite(buf, 2, OUTPUT_DMA_SIZE, f);
        }
        break;
    }
    case 2: {
        CymbalVoice *v = &cymbalVoice;
        memset(&cymbalVoice, 0, sizeof cymbalVoice);
        Cymbal_init();
        v->osc.waveform = c->wave;
        v->modOsc.waveform = c->mwave;
        v->modOsc2.waveform = c->m2wave;
        set_coarse(&v->osc, c->coarse);
        set_fine(&v->osc, c->fine);
        set_coarse(&v->modOsc, c->mcoarse);
        set_coarse(&v->modOsc2, c->m2coarse);
        v->fmModAmount1 = c->mamt / 127.f;
        v->fmModAmount2 = c->m2amt / 127.f;
        slopeEg2_setAttack(&v->oscVolEg, c->atk, false);
        slopeEg2_setDecay(&v->oscVolEg, c->dec, false);
        slopeEg2_setSlope(&v->oscVolEg, c->slope);
        v->oscVolEg.repeat = c->repeat;
        set_trans(&v->transGen, c);
        set_filter(&v->filter, &v->filterType, c);
        setDistortionShape(&v->distortion, c->dist / 127.f);
        v->vol = c->vol / 127.f;
        v->pwm = c->pwm / 127.f;
        v->FaderVolumeFilter.b = 1;
        Cymbal_trigger(c->vel, 60);
        for (b = 0; b < nb; b++) {
            SVF_recalcFreq(&v->filter);
            Cymbal_calcAsync();
            Cymbal_calcSyncBlock(buf, OUTPUT_DMA_SIZE);
            decimate(rate, buf);
            fwrite(buf, 2, OUTPUT_DMA_SIZE, f);
        }
        break;
    }
    default: {
        HiHatVoice *v = &hatVoice;
        memset(&hatVoice, 0, sizeof hatVoice);
        HiHat_init();
        v->osc.waveform = c->wave;
        v->modOsc.waveform = c->mwave;
        v->modOsc2.waveform = c->m2wave;
        set_coarse(&v->osc, c->coarse);
        set_fine(&v->osc, c->fine);
        set_coarse(&v->modOsc, c->mcoarse);
        set_coarse(&v->modOsc2, c->m2coarse);
        v->fmModAmount1 = c->mamt / 127.f;
        v->fmModAmount2 = c->m2amt / 127.f;
        slopeEg2_setAttack(&v->oscVolEg, c->atk, false);
        v->decayClosed = slopeEg2_calcDecay(c->dec);               /* VELOD6 */
        slopeEg2_setSlope(&v->oscVolEg, c->slope);
        set_trans(&v->transGen, c);
        set_filter(&v->filter, &v->filterType, c);
        setDistortionShape(&v->distortion, c->dist / 127.f);
        v->vol = c->vol / 127.f;
        v->pwm = c->pwm / 127.f;
        v->FaderVolumeFilter.b = 1;
        HiHat_trigger(c->vel, 0, 60);
        for (b = 0; b < nb; b++) {
            SVF_recalcFreq(&v->filter);
            HiHat_calcAsync();
            HiHat_calcSyncBlock(buf, OUTPUT_DMA_SIZE);
            decimate(rate, buf);
            fwrite(buf, 2, OUTPUT_DMA_SIZE, f);
        }
        break;
    }
    }
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    char path[512];
    uint32_t i;
    FILE *f;
    for (i = 0; i < LXC_N; i++) {
        snprintf(path, sizeof path, "%s/%02u.raw", dir, i);
        if (!(f = fopen(path, "wb")))
            return 1;
        render(&LXC[i], f);
        fclose(f);
    }
    snprintf(path, sizeof path, "%s/transients.bin", dir);
    if (!(f = fopen(path, "wb")))
        return 1;
    fwrite(transientData, 1, sizeof transientData, f);
    fclose(f);
    printf("lxr_ref: %u sounds of the LXR's voices -> %s\n", (unsigned)LXC_N, dir);
    return 0;
}
