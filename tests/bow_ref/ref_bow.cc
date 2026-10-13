// SPDX-License-Identifier: GPL-3.0-only
// The reference for tests/bow_test.c: the float originals of Elements' bow and blow (elements/dsp of
// pichenettes/eurorack, MIT, Emilie Gillet), a piece a file:
//   bowtable.txt   her friction curve: x, velocity, the result
//   flow-N.txt     ProcessFlow (the bow's noise), a sample a line, before her low-pass
//   wg-N.txt       her Resonator bowed, nothing else exciting it: the banded waveguides alone. First the
//                  waveguides (g, 1 / q, the delay, the amplitude in the sum), then a sample a line
//   tube-N.txt     her Tube, a sample a line: what it adds to its input
//   diff.txt       her Diffuser on a click and then noise
// The noise the tube and the diffuser are fed is ref_noise() below; the test makes the same.
// tests/bow_ref/build.sh
#define private public
#include <cstdio>
#include <cmath>
#include "elements/dsp/exciter.h"
#include "elements/dsp/resonator.h"
#include "elements/dsp/tube.h"
#include "elements/dsp/fx/diffuser.h"
#include "stmlib/utils/random.h"
#include "stmlib/dsp/cosine_oscillator.h"
using namespace elements;

static uint32_t ref_seed = 12345;
static float ref_noise() {               // -0.5 .. 0.5, 16 bits
  ref_seed = ref_seed * 1664525u + 1013904223u;
  return (float)(ref_seed >> 16) / 65536.0f - 0.5f;
}

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : ".";
  char path[512];
  FILE* f;
  {
    Resonator r;
    r.Init();
    snprintf(path, sizeof path, "%s/bowtable.txt", dir);
    f = fopen(path, "w");
    for (int v = 0; v <= 4; ++v)
      for (int x = -40; x <= 40; ++x)
        fprintf(f, "%.6f %.6f %.8f\n", x * 0.025f, v * 0.25f, r.BowTable(x * 0.025f, v * 0.25f));
    fclose(f);
  }
  static const float P[] = {0.7f, 0.3f, 0.95f};
  for (int c = 0; c < 3; ++c) {
    stmlib::Random::Seed(0x21 + c);
    Exciter e;
    e.Init();
    e.set_model(EXCITER_MODEL_FLOW);
    e.set_parameter(P[c]);
    snprintf(path, sizeof path, "%s/flow-%d.txt", dir, c);
    f = fopen(path, "w");
    for (int b = 0; b < 500; ++b) {
      float out[16];
      e.ProcessFlow(b == 0 ? (EXCITER_FLAG_RISING_EDGE | EXCITER_FLAG_GATE) : EXCITER_FLAG_GATE, out, 16);
      for (int i = 0; i < 16; ++i) fprintf(f, "%.7f\n", out[i]);
    }
    fclose(f);
  }
  // the waveguides: note (cycles a sample), geometry, position, bow strength
  static const float W[][4] = {
    {220.0f / 32000.0f, 0.28f, 0.0f, 1.0f}, {110.0f / 32000.0f, 0.28f, 0.3f, 0.6f},
    {440.0f / 32000.0f, 0.5f, 0.1f, 0.3f},  {65.4f / 32000.0f, 0.1f, 0.2f, 1.0f},
    {880.0f / 32000.0f, 0.7f, 0.05f, 0.8f}, {50.0f / 32000.0f, 0.28f, 0.0f, 1.0f},
  };
  for (int c = 0; c < 6; ++c) {
    static Resonator r;
    r.Init();
    r.set_frequency(W[c][0]);
    r.set_geometry(W[c][1]);
    r.set_position(W[c][2]);
    r.previous_position_ = W[c][2];
    r.set_brightness(0.5f);
    r.set_damping(0.5f);
    r.set_modulation_frequency(0.0f);
    r.set_modulation_offset(0.0f);
    r.lfo_phase_ = 0.0f;
    r.clock_divider_ = 0;
    r.set_resolution(52);
    snprintf(path, sizeof path, "%s/wg-%d.txt", dir, c);
    f = fopen(path, "w");
    float peak = 0.0f, peak_s = 0.0f;
    for (int b = 0; b < 20000; ++b) {      // 10 s (the bowing takes seconds to settle); the first second is written
      float in[16], bow[16], center[16], sides[16];
      for (int i = 0; i < 16; ++i) { in[i] = 0.0f; bow[i] = W[c][3]; }
      if (b == 0) {
        size_t n = r.ComputeFilters();
        r.clock_divider_ = 0;
        if (n > kMaxBowedModes) n = kMaxBowedModes;
        stmlib::CosineOscillator a;
        a.Init<stmlib::COSINE_OSCILLATOR_APPROXIMATE>(W[c][2]);
        a.Start();
        fprintf(f, "%d %.3f\n", (int)n, W[c][3]);
        for (size_t i = 0; i < n; ++i)
          fprintf(f, "%.9g %.9g %d %.7f\n", r.f_bow_[i].g(), r.f_bow_[i].r(), (int)r.d_bow_[i].delay_, a.Next());
      }
      r.Process(bow, in, center, sides, 16);
      for (int i = 0; i < 16; ++i) {
        if (b < 2000) fprintf(f, "%.7f\n", center[i]);
        if (fabsf(center[i]) > peak) peak = fabsf(center[i]);
      }
      if (b % 2000 == 1999) { fprintf(f, "p %d %.6f\n", b + 1, peak); peak = 0.0f; }
      for (size_t k = 0; k < kMaxBowedModes; ++k)
        for (size_t j = 0; j < kMaxDelayLineSize; ++j)
          if (fabsf(r.d_bow_[k].line_[j]) > peak_s) peak_s = fabsf(r.d_bow_[k].line_[j]);
    }
    fclose(f);
    printf("wg-%d: peak of the sum %.3f, in a waveguide %.3f, the friction %.4f\n", c, peak, peak_s, r.bow_signal_);
  }
  // the tube: note, damping, timbre (tube level), gain
  static const float T[][4] = {
    {220.0f / 32000.0f, 0.5f, 0.5f, 0.25f}, {110.0f / 32000.0f, 0.9f, 1.0f, 0.5f},
    {523.0f / 32000.0f, 0.1f, 0.2f, 0.1f},  {40.0f / 32000.0f, 0.7f, 0.8f, 0.4f},
    {10.0f / 32000.0f, 0.5f, 0.6f, 0.3f},
  };
  for (int c = 0; c < 5; ++c) {
    static Tube t;
    t.Init();
    for (size_t i = 0; i < kTubeDelaySize; ++i) t.delay_line_[i] = 0.0f;
    ref_seed = 12345 + c;
    snprintf(path, sizeof path, "%s/tube-%d.txt", dir, c);
    f = fopen(path, "w");
    float peak = 0.0f;
    for (int b = 0; b < 1000; ++b) {     // the envelope: up in 200 blocks, held, down from block 700
      float io[16], in[16];
      float env = b < 200 ? b / 200.0f : b < 700 ? 1.0f : (1000 - b) / 300.0f;
      for (int i = 0; i < 16; ++i) in[i] = io[i] = ref_noise() * 0.5f;
      t.Process(T[c][0], env, T[c][1], T[c][2], io, T[c][3], 16);
      for (int i = 0; i < 16; ++i) {
        fprintf(f, "%.7f\n", io[i] - in[i]);
        if (fabsf(io[i] - in[i]) > peak) peak = fabsf(io[i] - in[i]);
      }
    }
    fclose(f);
    printf("tube-%d: peak %.3f\n", c, peak);
  }
  {
    static float buffer[1024];
    static Diffuser d;
    for (int i = 0; i < 1024; ++i) buffer[i] = 0.0f;
    d.Init(buffer);
    ref_seed = 777;
    snprintf(path, sizeof path, "%s/diff.txt", dir);
    f = fopen(path, "w");
    float peak = 0.0f;
    for (int b = 0; b < 500; ++b) {
      float io[16];
      for (int i = 0; i < 16; ++i) io[i] = b < 200 ? (b == 0 && i == 0 ? 1.0f : 0.0f) : ref_noise();
      d.Process(io, 16);
      for (int i = 0; i < 16; ++i) { fprintf(f, "%.7f\n", io[i]); if (fabsf(io[i]) > peak) peak = fabsf(io[i]); }
    }
    fclose(f);
    printf("diff: peak %.3f\n", peak);
  }
  return 0;
}
