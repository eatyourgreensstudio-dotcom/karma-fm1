// SPDX-License-Identifier: GPL-3.0-only
// The reference for tests/cloud_test.c: the float original of Clouds' granular player (clouds/dsp of
// pichenettes/eurorack, MIT, Emilie Gillet) on a 16-bit mono recording of 32768 samples, 12 grains at most, every
// grain at her best quality. A case a file, a sample a line: left, right. The recording is fed ref_in() below (the
// test makes the same): a few low tones (so that her Hermite reader and the port's straight line agree).
// tests/cloud_ref/build.sh
#define private public
#include <cstdio>
#include <cmath>
#include "clouds/dsp/granular_sample_player.h"
#include "stmlib/utils/random.h"
using namespace clouds;

static int16_t ref_in(int i) {
  double t = i / 32000.0;
  double v = 0.30 * sin(6.2831853 * 110.0 * t) + 0.22 * sin(6.2831853 * 277.0 * t + 1.0)
           + 0.15 * sin(6.2831853 * 523.0 * t + 2.0) * (0.5 + 0.5 * sin(6.2831853 * 1.7 * t));
  return (int16_t)lrint(v * 32767.0);
}

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : ".";
  // position, size (samples), pitch (semitones), overlap, window shape, spread, steady
  static const float C[][7] = {
    {0.25f, 2048, 0, 0.625f, 0.75f, 0.5f, 0},   {0.0f, 1024, 12, 0.875f, 0.25f, 1.0f, 0},
    {0.75f, 8192, -12, 0.5f, 1.0f, 0.25f, 0}, {0.5f, 4096, 7, 0.75f, 0.625f, 0.75f, 1},
    {0.75f, 1500, -5, 0.375f, 0.0f, 0.0f, 1},  {0.25f, 6000, 19, 1.0f, 0.875f, 1.0f, 0},
  };
  for (int c = 0; c < 6; ++c) {
    static int16_t mem[32768 + kInterpolationTail], tail[kCrossFadeSize];
    static AudioBuffer<RESOLUTION_16_BIT> buf;
    static GranularSamplePlayer player;
    buf.Init(mem, 32768 + kInterpolationTail, tail);
    player.Init(1, 12);
    player.num_midfi_grains_ = 0;        // every grain at her best
    player.grain_rate_phasor_ = 0.0f;
    stmlib::Random::Seed(0x21 + c);
    Parameters p;
    p.position = C[c][0];
    p.pitch = C[c][2];
    p.stereo_spread = C[c][5];
    p.trigger = false;
    p.granular.overlap = C[c][3];
    p.granular.window_shape = C[c][4];
    p.granular.use_deterministic_seed = C[c][6] != 0.0f;
    // the size: her table is indexed by the knob; find the knob for these samples
    p.size = log2f(C[c][1] / 1024.0f) / 4.0f;
    char path[512];
    snprintf(path, sizeof path, "%s/cloud-%d.txt", dir, c);
    FILE* f = fopen(path, "w");
    int grains = 0;
    for (int b = 0; b < 3000; ++b) {     // 3 s
      float out[64];
      for (int i = 0; i < 32; ++i) buf.Write(ref_in(b * 32 + i) / 32768.0f);
      player.Play(&buf, p, out, 32);
      for (int i = 0; i < 32; ++i) fprintf(f, "%.7f %.7f\n", out[2 * i], out[2 * i + 1]);
      for (int i = 0; i < 12; ++i) grains += player.grains_[i].active();
    }
    fclose(f);
    printf("cloud-%d: grain size %.0f, %.1f grains at once\n", c,
           stmlib::Interpolate(lut_grain_size, p.size, 256.0f), grains / 3000.0);
  }
  return 0;
}
