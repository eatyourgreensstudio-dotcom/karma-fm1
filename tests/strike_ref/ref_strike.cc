// SPDX-License-Identifier: GPL-3.0-only
// The reference for tests/strike_test.c: the float originals of Elements' strike exciters (elements/dsp/exciter.cc of
// pichenettes/eurorack, MIT, Emilie Gillet), before her low-pass: each hit's sample and level, a line a hit, the
// level divided by her pulse amplitude.   tests/strike_ref/build.sh
#define private public
#include <cstdio>
#include "elements/dsp/exciter.h"
#include "stmlib/utils/random.h"
using namespace elements;
int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : ".";
  static const float P[] = {0.1f, 0.5f, 0.8f, 0.95f};
  char path[512];
  for (int c = 0; c < 4; ++c) {            // bounce: 4 s of the key held, parameter P[c]
    stmlib::Random::Seed(0x21 + c);
    Exciter e;
    e.Init();
    e.set_model(EXCITER_MODEL_PARTICLES);
    e.set_parameter(P[c]);
    e.set_timbre(0.5f);
    float amp = e.GetPulseAmplitude(0.5f);
    snprintf(path, sizeof path, "%s/bounce-%d.txt", dir, c);
    FILE* f = fopen(path, "w");
    for (int b = 0; b < 4 * 32000 / 16; ++b) {
      float out[16];
      e.ProcessParticles(b == 0 ? (EXCITER_FLAG_RISING_EDGE | EXCITER_FLAG_GATE) : EXCITER_FLAG_GATE, out, 16);
      for (int i = 0; i < 16; ++i) if (out[i] != 0.0f) fprintf(f, "%d %.6f\n", b * 16 + i, out[i] / amp);
    }
    fclose(f);
  }
  for (int c = 0; c < 4; ++c) {            // pick: parameter P[c], signature 0.25 c
    Exciter e;
    e.Init();
    e.set_model(EXCITER_MODEL_PLECTRUM);
    e.set_parameter(P[c]);
    e.set_timbre(0.5f);
    e.set_signature(0.25f * c);
    float amp = e.GetPulseAmplitude(0.5f);
    snprintf(path, sizeof path, "%s/pick-%d.txt", dir, c);
    FILE* f = fopen(path, "w");
    for (int b = 0; b < 8000 / 16; ++b) {
      float out[16];
      e.ProcessPlectrum(b == 0 ? (EXCITER_FLAG_RISING_EDGE | EXCITER_FLAG_GATE) : EXCITER_FLAG_GATE, out, 16);
      for (int i = 0; i < 16; ++i) if (out[i] != 0.0f) fprintf(f, "%d %.6f\n", b * 16 + i, out[i] / amp);
      if (b % 50 == 49) fprintf(f, "d %d %.6f\n", b * 16 + 15, e.damp_state_);
    }
    fclose(f);
  }
  return 0;
}
