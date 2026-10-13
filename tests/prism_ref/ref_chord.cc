// SPDX-License-Identifier: GPL-3.0-only
// The reference for tests/prism_test.c: the float original of the chord model (plaits/dsp/engine/chord_engine.cc,
// MIT, Emilie Gillet), as ref_fold.cc; half a second played first (TIMBRE and MORPH come in from 0), then 1.5 s kept.
#include <cstdio>
#include <cstdlib>
#include "plaits/dsp/engine/chord_engine.h"
using namespace plaits;
int main(int argc, char** argv) {
  static const float C[][4] = {       // f0, harmonics, timbre, morph: as tests/prism_test.c CHORD
    {0.004f, 0.0f, 0.0f, 0.2f}, {0.004f, 0.3f, 0.0f, 0.0f}, {0.004f, 0.94f, 0.3f, 0.3f}, {0.004f, 0.4f, 0.55f, 0.45f},
    {0.004f, 0.4f, 0.5f, 0.7f}, {0.004f, 0.85f, 0.8f, 1.0f}, {0.002f, 0.5f, 0.2f, 0.56f}, {0.01f, 0.67f, 1.0f, 0.1f},
    {0.006f, 0.76f, 0.13f, 0.85f}, {0.02f, 0.13f, 0.6f, 0.25f}};
  const char* dir = argc > 1 ? argv[1] : ".";
  for (size_t c = 0; c < sizeof(C) / sizeof(C[0]); ++c) {
    static char ram[16384];
    stmlib::BufferAllocator allocator(ram, sizeof ram);
    ChordEngine e;
    e.Init(&allocator);
    e.Reset();
    EngineParameters p;
    p.trigger = TRIGGER_UNPATCHED;
    p.note = 9.0f + 12.0f * log2f(C[c][0] / (a0 * 0.25f));
    p.harmonics = C[c][1]; p.timbre = C[c][2]; p.morph = C[c][3]; p.accent = 0.8f;
    char path[512];
    snprintf(path, sizeof path, "%s/chord-%zu.f32", dir, c);
    FILE* f = fopen(path, "wb");
    if (!f) return 2;
    for (int b = 0; b < 689 + 2048; ++b) {
      float out[32], aux[32];
      bool env = false;
      e.Render(p, out, aux, 32, &env);
      if (b >= 689) fwrite(out, sizeof(float), 32, f);
    }
    fclose(f);
    snprintf(path, sizeof path, "%s/chord-%zu.f0", dir, c);
    f = fopen(path, "w");
    fprintf(f, "%.12g\n", (double)NoteToFrequency(p.note));
    fclose(f);
  }
  return 0;
}
