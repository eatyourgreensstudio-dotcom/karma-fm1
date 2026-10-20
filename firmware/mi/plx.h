/* SPDX-License-Identifier: GPL-3.0-only */
/* PLX: Plaits' models (firmware/mi/plx.cc, C++) for the C firmware. A model instance lives in memory the caller
 * gives: plx_size bytes, set up by plx_init; plx_render renders up to PLX_MAX_BLOCK samples of it. */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
enum {
    PLX_VA, PLX_VCF, PLX_PD, PLX_FM, PLX_6OP_A, PLX_6OP_B, PLX_6OP_C, PLX_GRAIN, PLX_TERRAIN, PLX_STRMACH,
    PLX_CHIP, PLX_STRING, PLX_MODAL, PLX_NOISE, PLX_SPEECH, PLX_PARTICLE, PLX_BD, PLX_SD, PLX_HH, PLX_COUNT
};
#define PLX_MAX_BLOCK 24u
enum { PLX_TRIG_LOW = 0, PLX_TRIG_RISE = 1, PLX_TRIG_UNPATCHED = 2, PLX_TRIG_HIGH = 4 };   /* (Plaits' TriggerState) */
typedef struct {
    int trigger;                 /* PLX_TRIG_*: a note starts (RISE | HIGH), is held (HIGH), let go (LOW) */
    float note;                  /* MIDI note, fractional */
    float timbre, morph, harmonics, accent;   /* 0..1 */
} plx_params_t;
unsigned plx_size(int model);
int plx_init(int model, void *mem, unsigned n);
void plx_render(void *mem, const plx_params_t *p, float *out, float *aux, unsigned n);
#ifdef __cplusplus
}
#endif
