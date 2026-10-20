// SPDX-License-Identifier: GPL-3.0-only
// ELX: the Ominous Voice of Emilie Gillet's Elements (MIT: firmware/mi/elements, unchanged), the module's hidden
// mode: two FM oscillators (8 x oversampled) through two filters, circling in space. Behind a C interface for the
// ELEMENTS engine's OMIN model (firmware/src/eng_elem.c). Elements runs at 32 kHz, this firmware at 44.1 kHz:
// elx_render adds the difference to the note (her envelope and filter times run faster, as the pitch would).
// Her patch fields that this firmware has no knob for keep the values below.
#include "elx.h"
#include "elements/dsp/ominous_voice.h"
#include "elements/dsp/patch.h"

#ifdef PLX_TARGET
inline void *operator new(size_t, void *p) { return p; }
#else
#include <new>
#endif

namespace {
const float kTuneOffset = 5.5507f;   // 12 log2(44100 / 32000)
struct Omin {
  elements::OminousVoice voice;
  elements::Patch patch;
};
}  // namespace

extern "C" {

unsigned elx_size(void) { return (unsigned)((sizeof(Omin) + 7u) & ~7u); }

void elx_init(void *mem)
{
  Omin *o = new (mem) Omin();
  o->voice.Init();
  elements::Patch &p = o->patch;
  p.exciter_envelope_shape = 0.8f;   // (held: attack 0, sustain 1; the track's ADSR shapes the note)
  p.exciter_bow_level = 0.0f;
  p.exciter_bow_timbre = 0.3f;
  p.exciter_blow_level = 1.0f;
  p.exciter_blow_meta = 0.5f;
  p.exciter_blow_timbre = 0.5f;
  p.exciter_strike_level = 0.8f;
  p.exciter_strike_meta = 0.5f;
  p.exciter_strike_timbre = 0.5f;
  p.exciter_signature = 0.5f;
  p.resonator_geometry = 0.3f;
  p.resonator_brightness = 0.5f;
  p.resonator_damping = 0.5f;
  p.resonator_position = 0.3f;
  p.resonator_modulation_frequency = 0.5f;
  p.resonator_modulation_offset = 0.1f;
  p.reverb_diffusion = 0.5f;
  p.reverb_lp = 0.5f;
  p.space = 0.0f;
  p.modulation_frequency = 0.5f;
}

void elx_render(void *mem, const elx_params_t *q, int gate, float *out, unsigned n)
{
  static const float zero[elements::kMaxBlockSize] = {0};
  float raw[elements::kMaxBlockSize], sides[elements::kMaxBlockSize];
  Omin *o = (Omin *)mem;
  elements::Patch &p = o->patch;
  if (n > ELX_MAX_BLOCK)
    n = ELX_MAX_BLOCK;
  p.exciter_blow_meta = q->ratio;
  p.exciter_strike_meta = q->ratio * 0.9f + 0.05f;   // (the second a little off the first: it beats)
  p.exciter_blow_timbre = q->fm;
  p.exciter_strike_timbre = q->fm;
  p.exciter_bow_timbre = q->feedback;
  p.exciter_bow_level = q->detune;
  p.resonator_brightness = q->brightness;
  p.resonator_geometry = q->shape;
  p.resonator_position = q->space;
  o->voice.Process(p, q->note + kTuneOffset, q->strength, gate != 0, zero, zero, raw, out, sides, n);
}

}  // extern "C"
