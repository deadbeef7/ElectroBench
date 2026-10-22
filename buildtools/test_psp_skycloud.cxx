// Host-side check for the PSP port's baked cloud deck.
//
// The PSP cannot be executed on this build machine and the sky is a CPU bake,
// so the cloud field is measured here instead of on the console: for every
// texel of the 128x64 panorama this evaluates pspsky::CloudCoverage with the
// measured texel footprint and again with no filtering (footprint 0, the
// previous behaviour), then reports
//
//   * coverage mean          -- the deck must not be filtered away,
//   * isolated dots          -- covered texels whose four neighbours are clear,
//                               which is what "the clouds look like dots" is,
//   * total variation        -- per-texel change, i.e. how mottled the deck is.
//
// It also writes both fields as P5 PGMs (arg 1 and arg 1 + ".raw.pgm").
//
// Build and run:
//   g++ -O2 -std=c++17 -Ipsp/src buildtools/test_psp_skycloud.cxx -o build/test_psp_skycloud
//   ./build/test_psp_skycloud /tmp/sky_clouds.pgm
// (make -C psp check-sky does both.)
#include <cmath>
#include <cstdio>
#include <vector>

#include "psp_skycloud.hxx"

namespace {

constexpr int kSkyW = 128, kSkyH = 64;
constexpr float kPi = 3.14159265358979f;

struct Metrics {
  double coverageMean = 0.0;
  double covered = 0.0;      // texels with coverage > 0.05
  double dots = 0.0;         // covered texels with no covered 4-neighbour
  double variation = 0.0;    // mean |d/dx| + |d/dy|
  double blobs = 0.0;        // connected covered runs
  double blobArea = 0.0;     // mean area of those runs, in texels
};

bool Covered(const std::vector<float> &cover, int x, int y, float t = 0.05f) {
  return cover[(y % kSkyH) * kSkyW + (x % kSkyW)] > t;
}

Metrics Measure(const std::vector<float> &cover) {
  Metrics m;
  double sum = 0.0, var = 0.0;
  for (int y = 0; y < kSkyH; y++) {
    for (int x = 0; x < kSkyW; x++) {
      const float c = cover[y * kSkyW + x];
      sum += c;
      if (c > 0.05f) m.covered += 1.0;
      var += std::fabs(cover[y * kSkyW + (x + 1) % kSkyW] - c) +
             std::fabs(cover[((y + 1) % kSkyH) * kSkyW + x] - c);
    }
  }
  m.coverageMean = sum / (kSkyW * kSkyH);
  m.variation = var / (kSkyW * kSkyH);

  // Connected runs (4-neighbour) of covered texels: "dots" are the runs of
  // one texel, and the mean area says how many texels a cloud mass spans.
  std::vector<char> seen((size_t)kSkyW * kSkyH, 0);
  std::vector<int> stack;
  for (int y = 0; y < kSkyH; y++) {
    for (int x = 0; x < kSkyW; x++) {
      const size_t i = (size_t)y * kSkyW + x;
      if (seen[i] || !Covered(cover, x, y)) continue;
      m.blobs += 1.0;
      int area = 0;
      stack.clear();
      stack.push_back((int)i);
      seen[i] = 1;
      while (!stack.empty()) {
        const int p = stack.back(); stack.pop_back();
        area++;
        const int px = p % kSkyW, py = p / kSkyW;
        const int nb[4][2] = {{px + 1, py}, {px - 1, py}, {px, py + 1}, {px, py - 1}};
        for (int k = 0; k < 4; k++) {
          const int nx = (nb[k][0] + kSkyW) % kSkyW;
          const int ny = (nb[k][1] + kSkyH) % kSkyH;
          const size_t ni = (size_t)ny * kSkyW + nx;
          if (!seen[ni] && Covered(cover, nx, ny)) {
            seen[ni] = 1;
            stack.push_back((int)ni);
          }
        }
      }
      if (area == 1) m.dots += 1.0;
      m.blobArea += area;
    }
  }
  m.blobArea = m.blobs > 0.0 ? m.covered / m.blobs : 0.0;
  return m;
}

void WritePgm(const char *path, const std::vector<float> &cover) {
  FILE *f = std::fopen(path, "wb");
  if (!f) {
    std::printf("note: cannot write %s\n", path);
    return;
  }
  std::fprintf(f, "P5\n%d %d\n255\n", kSkyW, kSkyH);
  for (int i = 0; i < kSkyW * kSkyH; i++) {
    unsigned char v = (unsigned char)(pspsky::Saturate(cover[i]) * 255.0f + 0.5f);
    std::fwrite(&v, 1, 1, f);
  }
  std::fclose(f);
}

}  // namespace

int main(int argc, char **argv) {
  pspsky::BuildNoiseTable();

  std::vector<float> filtered(kSkyW * kSkyH), unfiltered(kSkyW * kSkyH);
  const float dazim = 2.0f * kPi / (float)kSkyW;
  const float delev = kPi / (float)kSkyH;

  // The footprint of every texel, once: it depends only on the panorama grid.
  std::vector<float> footprint((size_t)kSkyW * kSkyH);
  std::vector<float> dirs((size_t)kSkyW * kSkyH * 3);
  for (int y = 0; y < kSkyH; y++) {
    const float elev = (0.5f - ((float)y + 0.5f) / (float)kSkyH) * kPi;
    for (int x = 0; x < kSkyW; x++) {
      const float azim = ((float)x + 0.5f) / (float)kSkyW * 2.0f * kPi;
      float *d = &dirs[((size_t)y * kSkyW + x) * 3];
      pspsky::DirFromAngles(azim, elev, &d[0], &d[1], &d[2]);
      footprint[(size_t)y * kSkyW + x] =
          pspsky::LayerFootprint(azim, elev, dazim, -delev, 0.075f, 1.0f);
    }
  }

  auto bake = [&](std::vector<float> &out, float scale, bool useFootprint) {
    for (int y = 0; y < kSkyH; y++) {
      for (int x = 0; x < kSkyW; x++) {
        const size_t i = (size_t)y * kSkyW + x;
        const float *d = &dirs[i * 3];
        out[i] = pspsky::CloudCoverage(d[0], d[1], d[2], 0.0f, 0.0f,
                                       useFootprint ? footprint[i] : 0.0f,
                                       nullptr, scale);
      }
    }
  };

  std::printf("PSP cloud deck, %dx%d panorama (dots = covered texels with no\n"
              "covered neighbour; area = mean texels per connected cloud)\n",
              kSkyW, kSkyH);
  std::printf("  %6s %8s %8s %6s %8s %9s\n", "scale", "coverage", "covered",
              "dots", "variation", "area");

  // 1.0 is the shader's own coordinate scale; the smaller values are the
  // panorama-fitting candidates (see psp_skycloud.hxx).
  const float kScales[] = {1.0f, 0.6f, 0.34f, 0.22f, 0.16f, 0.12f, 0.09f, 0.07f};
  int failures = 0;
  for (float s : kScales) {
    bake(filtered, s, true);
    const Metrics m = Measure(filtered);
    std::printf("  %6.2f %8.4f %8.0f %6.0f %8.4f %9.1f\n", s, m.coverageMean,
                m.covered, m.dots, m.variation, m.blobArea);
  }

  // The chosen scale must actually read as a deck: almost no one-texel dots,
  // masses of several texels, and clouds still present. The A/B below isolates
  // the octave filter at that scale (footprint 0 = the filter switched off).
  bake(filtered, pspsky::kCloudCoordScale, true);
  bake(unfiltered, pspsky::kCloudCoordScale, false);
  const Metrics before = Measure(unfiltered);
  const Metrics after = Measure(filtered);
  std::printf("\n  at kCloudCoordScale = %.2f, footprint filtering on/off:\n",
              (double)pspsky::kCloudCoordScale);
  std::printf("  %-22s %10s %10s\n", "", "unfiltered", "filtered");
  std::printf("  %-22s %10.4f %10.4f\n", "coverage mean", before.coverageMean,
              after.coverageMean);
  std::printf("  %-22s %10.0f %10.0f\n", "covered texels", before.covered,
              after.covered);
  std::printf("  %-22s %10.0f %10.0f\n", "one-texel blobs", before.dots, after.dots);
  std::printf("  %-22s %10.1f %10.1f\n", "texels per cloud", before.blobArea,
              after.blobArea);

  if (after.coverageMean < 0.004) {
    std::printf("FAIL the deck is gone (coverage %.4f)\n", after.coverageMean);
    failures++;
  }
  if (after.dots > 0.10 * after.covered) {
    std::printf("FAIL %.0f%% of covered texels are isolated dots\n",
                100.0 * after.dots / (after.covered + 1e-9));
    failures++;
  }
  if (after.blobArea < 6.0) {
    std::printf("FAIL cloud masses average only %.1f texels\n", after.blobArea);
    failures++;
  }


  if (argc > 1) {
    WritePgm(argv[1], filtered);
    char raw[512];
    std::snprintf(raw, sizeof(raw), "%s.raw.pgm", argv[1]);
    WritePgm(raw, unfiltered);
  }

  std::printf("%s\n", failures == 0 ? "PSP_SKYCLOUD_EXIT=0" : "PSP_SKYCLOUD_FAILED");
  return failures == 0 ? 0 : 1;
}
