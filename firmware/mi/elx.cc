// SPDX-License-Identifier: GPL-3.0-only
// ELX: the Ominous Voice of Emilie Gillet's Elements (MIT: firmware/mi/elements, unchanged), the module's hidden
// mode: two FM oscillators (8 x oversampled) through two filters, circling in space. Behind a C interface for the
// ELEMENTS engine's OMIN model (firmware/src/eng_elem.c). Elements runs at 32 kHz; here the voice runs at HALF the
// firmware's rate, 22.05 kHz, and elx_render brings it up to 44.1 kHz (a 4-point midpoint interpolation): on the
// FM-1 it took ~60 % of the CPU at 44.1 kHz (2026-10-28, measured: the mix broke up), half that now. The note is
// moved by the rates' difference (12 log2(32000 / 22050); before 2026-10-28 the 44.1 kHz offset was added with the
// wrong sign: every note 11.1 semitones sharp) and the cutoff by the octave the rate lost. Her envelope and filter
// times run at 22.05 / 32 of hers. Her patch fields that this firmware has no knob for keep the values below.
#include "elx.h"
#include "elements/dsp/ominous_voice.h"
#include "elements/dsp/patch.h"

#ifdef PLX_TARGET
inline void *operator new(size_t, void *p) { return p; }
#else
#include <new>
#endif

namespace {
const float kTuneOffset = 6.4493f;   // 12 log2(32000 / 22050): the half rate's note, in tune
const float kCutoffLift = 11.55f / 140.0f;   // the cutoff's octave lost to the half rate (less what the note adds)
struct Omin {
  elements::OminousVoice voice;
  elements::Patch patch;
  float h[3];                        // the last three half-rate samples (the interpolation's)
  float carry;                       // a 44.1 kHz sample made but not yet given (an odd block)
  int has_carry;
};
}  // namespace

extern "C" {

unsigned elx_size(void) { return (unsigned)((sizeof(Omin) + 7u) & ~7u); }

void elx_init(void *mem)
{
  Omin *o = new (mem) Omin();
  o->voice.Init();
  o->h[0] = o->h[1] = o->h[2] = o->carry = 0.0f;
  o->has_carry = 0;
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
  float raw[elements::kMaxBlockSize], sides[elements::kMaxBlockSize], half[elements::kMaxBlockSize];
  Omin *o = (Omin *)mem;
  elements::Patch &p = o->patch;
  unsigned i = 0, need, j;
  if (n > ELX_MAX_BLOCK)
    n = ELX_MAX_BLOCK;
  if (o->has_carry && n) {
    out[i++] = o->carry;
    o->has_carry = 0;
  }
  need = (n - i + 1u) / 2u;          // half-rate samples: two out each
  if (!need)
    return;
  p.exciter_blow_meta = q->ratio;
  p.exciter_strike_meta = q->ratio * 0.9f + 0.05f;   // (the second a little off the first: it beats)
  p.exciter_blow_timbre = q->fm;
  p.exciter_strike_timbre = q->fm;
  p.exciter_bow_timbre = q->feedback;
  p.exciter_bow_level = q->detune;
  p.resonator_brightness = q->brightness + kCutoffLift > 1.0f ? 1.0f : q->brightness + kCutoffLift;
  p.resonator_geometry = q->shape;
  p.resonator_position = q->space;
  o->voice.Process(p, q->note + kTuneOffset, q->strength, gate != 0, zero, zero, raw, half, sides, need);
  for (j = 0; j < need; j++) {      // up to 44.1 kHz: the midpoint between h[1] and h[2], then h[2]
    float x = half[j], a = (9.0f * (o->h[1] + o->h[2]) - o->h[0] - x) * (1.0f / 16.0f), b = o->h[2];
    o->h[0] = o->h[1];
    o->h[1] = o->h[2];
    o->h[2] = x;
    out[i++] = a;
    if (i < n) {
      out[i++] = b;
    } else {
      o->carry = b;
      o->has_carry = 1;
    }
  }
}

}  // extern "C"
