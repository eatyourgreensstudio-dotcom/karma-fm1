/* SPDX-License-Identifier: GPL-3.0-only */
/* ELX: Elements' Ominous Voice (firmware/mi/elx.cc, C++) for the C firmware: one instance in elx_size() bytes the
 * caller gives; elx_render renders up to ELX_MAX_BLOCK samples (mono: the centre of her stereo). */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#define ELX_MAX_BLOCK 16u
typedef struct {
    float note;                  /* MIDI note, fractional */
    float strength;              /* 0..1 (velocity): above 0.5 it lifts the envelope */
    float ratio;                 /* 0..1: the modulators' ratio (both oscillators) */
    float fm;                    /* 0..1: the FM amount */
    float feedback;              /* 0..1: the modulators' feedback */
    float detune;                /* 0..1: the second oscillator's detune */
    float brightness;            /* 0..1: the filters' cutoff */
    float shape;                 /* 0..1: the filters' mode, low-pass -> band-pass -> high-pass */
    float space;                 /* 0..1: how far the two sources circle (her spatialiser; mono here: the motion) */
} elx_params_t;
unsigned elx_size(void);
void elx_init(void *mem);
void elx_render(void *mem, const elx_params_t *p, int gate, float *out, unsigned n);
#ifdef __cplusplus
}
#endif
