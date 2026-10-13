// SPDX-License-Identifier: GPL-3.0-only
// The reference for tests/prism_test.c: the float original of the wavefolder model (plaits/dsp/engine/
// waveshaping_engine.cc of pichenettes/eurorack, MIT, Emilie Gillet), built on the host with her stmlib, renders
// the cases of the test to raw float32 files. Needs a checkout of the repository (EURORACK=path, its stmlib
// submodule too):  tests/prism_ref/build.sh
#include <cstdio>
#include <cstdlib>
#include "plaits/dsp/engine/waveshaping_engine.h"
using namespace plaits;
int main(int argc, char** argv) {
  // f0 (cycles a sample), harmonics, timbre, morph: as tests/prism_test.c CASES
  static const float C[][4] = {
    {0.002f, 0.5f, 0.0f, 0.5f}, {0.002f, 0.5f, 0.5f, 0.5f}, {0.002f, 0.5f, 1.0f, 0.5f}, {0.005f, 0.0f, 0.6f, 0.5f},
    {0.005f, 1.0f, 0.6f, 0.5f}, {0.005f, 0.25f, 0.8f, 0.0f}, {0.005f, 0.75f, 0.8f, 1.0f}, {0.02f, 0.5f, 0.7f, 0.3f},
    {0.0005f, 0.9f, 1.0f, 0.9f}, {0.05f, 0.3f, 0.9f, 0.6f}};
  const char* dir = argc > 1 ? argv[1] : ".";
  for (size_t c = 0; c < sizeof(C) / sizeof(C[0]); ++c) {
    char ram[4096];
    stmlib::BufferAllocator allocator(ram, sizeof ram);
    WaveshapingEngine e;
    e.Init(&allocator);
    e.Reset();
    EngineParameters p;
    p.trigger = TRIGGER_UNPATCHED;
    // NoteToFrequency(note) = a0 / 4 * 2^((note - 9) / 12): the note that gives f0
    p.note = 9.0f + 12.0f * log2f(C[c][0] / (a0 * 0.25f));
    p.harmonics = C[c][1]; p.timbre = C[c][2]; p.morph = C[c][3]; p.accent = 0.8f;
    char path[512];
    snprintf(path, sizeof path, "%s/fold-%zu.f32", dir, c);
    FILE* f = fopen(path, "wb");
    if (!f) return 2;
    for (int b = 0; b < 44100 / 32; ++b) {       // a second, in blocks of 32 as the firmware's
      float out[32], aux[32];
      bool env = false;
      e.Render(p, out, aux, 32, &env);
      fwrite(out, sizeof(float), 32, f);
    }
    fclose(f);
    // the frequency she plays for that note (her pitch table is a little off the exact one): the test plays the same
    snprintf(path, sizeof path, "%s/fold-%zu.f0", dir, c);
    f = fopen(path, "w");
    fprintf(f, "%.12g\n", (double)NoteToFrequency(p.note));
    fclose(f);
  }
  return 0;
}
