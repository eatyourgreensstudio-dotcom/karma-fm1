// SPDX-License-Identifier: GPL-3.0-only
// PLX: Plaits' synthesis models (Emilie Gillet, MIT: firmware/mi/plaits, unchanged) behind a C interface, for the
// PLAITS engine (firmware/src/eng_prism.c). One model instance in a block of memory the caller gives (the part's
// engine arena): the engine object, a limiter, then the engine's own buffers (stmlib BufferAllocator).
// Plaits runs at 48 kHz (its pitch tuned for 47872.34 Hz); this firmware at 44.1 kHz: plx_render adds the
// difference to the note (the decays run 8.5 % longer, unheard). Its blocks are at most 24 samples: the caller
// renders in chunks of up to PLX_MAX_BLOCK.
#include "plx.h"

#include "stmlib/utils/buffer_allocator.h"
#include "stmlib/dsp/limiter.h"
#include "plaits/resources.h"
#include "plaits/dsp/engine/virtual_analog_engine.h"
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine/grain_engine.h"
#include "plaits/dsp/engine/noise_engine.h"
#include "plaits/dsp/engine/string_engine.h"
#include "plaits/dsp/engine/modal_engine.h"
#include "plaits/dsp/engine/bass_drum_engine.h"
#include "plaits/dsp/engine/snare_drum_engine.h"
#include "plaits/dsp/engine/hi_hat_engine.h"
#include "plaits/dsp/engine/speech_engine.h"
#include "plaits/dsp/engine/particle_engine.h"
#include "plaits/dsp/engine2/chiptune_engine.h"
#include "plaits/dsp/engine2/phase_distortion_engine.h"
#include "plaits/dsp/engine2/six_op_engine.h"
#include "plaits/dsp/engine2/string_machine_engine.h"
#include "plaits/dsp/engine2/virtual_analog_vcf_engine.h"
#include "plaits/dsp/engine2/wave_terrain_engine.h"

#ifdef PLX_TARGET
inline void *operator new(size_t, void *p) { return p; }   // (placement new: no <new> on the target)
#else
#include <new>
#endif

using namespace plaits;

namespace {

const float kTuneOffset = 1.42071f;     // 12 log2(47872.34 / 44100): Plaits' note on a 44.1 kHz clock

// each model: its engine type, the buffer its Init takes (measured: tests/plx_test.cc checks it), its user data
struct Base {
  stmlib::Limiter limiter;
  Engine *engine;
};
template <class E> struct Inst : Base {
  E e;
};

template <class E> unsigned need(unsigned buffer) { return (unsigned)((sizeof(Inst<E>) + 7u) & ~7u) + buffer + 16u; }
template <class E> Base *make(void *mem, unsigned n, const uint8_t *user, float gain, bool env)
{
  Inst<E> *in = new (mem) Inst<E>();
  unsigned head = (unsigned)((sizeof(Inst<E>) + 7u) & ~7u);
  stmlib::BufferAllocator a((char *)mem + head, n - head);
  in->engine = &in->e;
  in->limiter.Init();
  in->e.Init(&a);
  in->e.LoadUserData(user);
  in->e.Reset();
  in->e.post_processing_settings.out_gain = gain;
  in->e.post_processing_settings.aux_gain = gain;
  in->e.post_processing_settings.already_enveloped = env;
  return in;
}

// (id, engine, the buffer its Init takes, its user data, Plaits' out gain (< 0: the limiter after it), its own
// envelopes: dsp/voice.cc RegisterInstance)
#define MODELS(X) \
  X(PLX_VA, VirtualAnalogEngine, 96, 0, 0.8f, 0) X(PLX_VCF, VirtualAnalogVCFEngine, 0, 0, 1.0f, 0) \
  X(PLX_PD, PhaseDistortionEngine, 384, 0, 0.7f, 0) X(PLX_FM, FMEngine, 0, 0, 0.6f, 0) \
  X(PLX_6OP_A, SixOpEngine, 5568, fm_patches_table[0], 1.0f, 1) X(PLX_6OP_B, SixOpEngine, 5568, fm_patches_table[1], 1.0f, 1) \
  X(PLX_6OP_C, SixOpEngine, 5568, fm_patches_table[2], 1.0f, 1) X(PLX_GRAIN, GrainEngine, 0, 0, 0.7f, 0) \
  X(PLX_TERRAIN, WaveTerrainEngine, 384, 0, 0.7f, 0) X(PLX_STRMACH, StringMachineEngine, 4332, 0, 0.8f, 0) \
  X(PLX_CHIP, ChiptuneEngine, 236, 0, 0.5f, 0) X(PLX_STRING, StringEngine, 15520, 0, -1.0f, 1) \
  X(PLX_MODAL, ModalEngine, 96, 0, -1.0f, 1) X(PLX_NOISE, NoiseEngine, 96, 0, -1.0f, 0) \
  X(PLX_SPEECH, SpeechEngine, 14656, 0, -0.7f, 0) X(PLX_PARTICLE, ParticleEngine, 16384, 0, -2.0f, 0) \
  X(PLX_BD, BassDrumEngine, 0, 0, 0.8f, 1) X(PLX_SD, SnareDrumEngine, 0, 0, 0.8f, 1) X(PLX_HH, HiHatEngine, 192, 0, 0.8f, 1)

}  // namespace

extern "C" {

unsigned plx_size(int model)
{
  switch (model) {
#define X(id, E, buf, user, gain, env) case id: return need<E>(buf);
    MODELS(X)
#undef X
  }
  return 0;
}

int plx_init(int model, void *mem, unsigned n)
{
  if (n < plx_size(model) || !mem)
    return 1;
  switch (model) {
#define X(id, E, buf, user, gain, env) case id: make<E>(mem, n, user, gain, env); return 0;
    MODELS(X)
#undef X
  }
  return 1;
}

void plx_render(void *mem, const plx_params_t *p, float *out, float *aux, unsigned n)
{
  Base *b = (Base *)mem;
  EngineParameters e;
  bool enveloped = b->engine->post_processing_settings.already_enveloped;
  float g = b->engine->post_processing_settings.out_gain;
  e.trigger = p->trigger;
  e.note = p->note + kTuneOffset;
  e.timbre = p->timbre;
  e.morph = p->morph;
  e.harmonics = p->harmonics;
  e.accent = p->accent;
  if (n > PLX_MAX_BLOCK)
    n = PLX_MAX_BLOCK;
  b->engine->Render(e, out, aux, n, &enveloped);
  if (g < 0.0f) {                                     // (as Plaits' post-processor: a limiter after some models)
    b->limiter.Process(-g, out, n);
  } else {
    for (unsigned i = 0; i < n; ++i)
      out[i] *= g;
  }
}

#ifdef PLX_TARGET   // what the target's C library (none: firmware/src/libc.c) lacks for these sources
float sqrtf(float x)
{
  if (x <= 0.0f)
    return 0.0f;
  union { float f; uint32_t i; } u = {x};
  u.i = 0x1FBD1DF5u + (u.i >> 1);                   // a first guess, then Newton
  u.f = 0.5f * (u.f + x / u.f);
  u.f = 0.5f * (u.f + x / u.f);
  return 0.5f * (u.f + x / u.f);
}
#endif

}  // extern "C"
