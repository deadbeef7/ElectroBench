#include <cmath>
#include <cstdio>

#include "psp_shadetail.hxx"

namespace {

// The palette entries and the sun the scene starts under, copied from
// psp_scene4.cxx / src/scene4.cxx (they are the same numbers there).
const float kGravel[3] = {0.330f, 0.272f, 0.205f};
const float kRoad[3] = {0.115f, 0.120f, 0.132f};
const float kKerb[3] = {0.245f, 0.238f, 0.225f};
const float kPaint[3] = {0.520f, 0.480f, 0.420f};
const float kWall[3] = {0.235f, 0.208f, 0.178f};
const float kRoof[3] = {0.125f, 0.118f, 0.115f};
const float kCable[3] = {0.055f, 0.050f, 0.055f};
const float kLens[3] = {0.880f, 0.680f, 0.420f};

const float kSun[3] = {-0.3008f, 0.2005f, 0.9324f};

struct Case {
  const char *name;
  const float *albedo;
  float nx, ny, nz;
  pspshade::Material m;
  int target[3];   // display 0-255 measured from the reference frame, or -1
  int tolerance;
};

int Rgb(const float c[3], int k) {
  const float v = c[k] * 255.0f + 0.5f;
  return v < 0.0f ? 0 : (v > 255.0f ? 255 : (int)v);
}

float Luma(const float c[3]) {
  return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
}

}  // namespace

int main() {
  const Case cases[] = {
      {"gravel, up", kGravel, 0.0f, 1.0f, 0.0f, pspshade::kMatGround, {132, 106, 85}, 18},
      {"carriageway, up", kRoad, 0.0f, 1.0f, 0.0f, pspshade::kMatRoad, {-1, -1, -1}, 0},
      {"kerb, top", kKerb, 0.0f, 1.0f, 0.0f, pspshade::kMatKerb, {-1, -1, -1}, 0},
      {"marking, up", kPaint, 0.0f, 1.0f, 0.0f, pspshade::kMatLine, {-1, -1, -1}, 0},
      // A wall turned away from the sun: the ambient-only case that used to
      // come out at (7, 6, 5).
      {"wall, away from sun", kWall, 0.3008f, 0.0f, -0.9324f, pspshade::kMatWall,
       {100, 79, 59}, 28},
      {"wall, toward sun", kWall, -0.3008f, 0.0f, 0.9324f, pspshade::kMatWall,
       {-1, -1, -1}, 0},
      {"roof, up", kRoof, 0.0f, 1.0f, 0.0f, pspshade::kMatRoof, {-1, -1, -1}, 0},
      {"cable, side", kCable, 1.0f, 0.0f, 0.0f, pspshade::kMatCable, {-1, -1, -1}, 0},
      {"lamp lens", kLens, 0.0f, 1.0f, 0.0f, pspshade::kMatGlow, {-1, -1, -1}, 0},
  };

  int failures = 0;
  float darkest = 1.0f;
  std::printf("PSP baked object shading (display 0-255), against the desktop frame\n");
  std::printf("  %-20s %-16s %-16s %s\n", "surface", "baked", "reference", "");

  for (const Case &c : cases) {
    float out[3];
    pspshade::Surface(c.albedo, c.nx, c.ny, c.nz, kSun, c.m, out);
    const int r = Rgb(out, 0), g = Rgb(out, 1), b = Rgb(out, 2);
    const float luma = Luma(out);
    if (c.m != pspshade::kMatGlow && luma < darkest) darkest = luma;

    std::printf("  %-20s (%3d,%3d,%3d)%5s ", c.name, r, g, b, "");
    if (c.target[0] < 0) {
      std::printf("%-16s\n", "-");
    } else {
      std::printf("(%3d,%3d,%3d)      ", c.target[0], c.target[1], c.target[2]);
      const int dr = r - c.target[0], dg = g - c.target[1], db = b - c.target[2];
      const bool ok = std::abs(dr) <= c.tolerance && std::abs(dg) <= c.tolerance &&
                      std::abs(db) <= c.tolerance;
      std::printf("%s (%+d,%+d,%+d)\n", ok ? "ok  " : "FAIL", dr, dg, db);
      if (!ok) failures++;
    }
  }

  // The carriageway has no reference pixel on purpose: matching the desktop
  // frame's road sample (163,110,77 - the sun's side of the frame) made the
  // surface read as red tarmac at 480x272. What it has to be is asphalt, i.e.
  // near-neutral and mid-grey, so that is what is asserted.
  {
    float out[3];
    pspshade::Surface(kRoad, 0.0f, 1.0f, 0.0f, kSun, pspshade::kMatRoad, out);
    const float luma = Luma(out);
    const int r = Rgb(out, 0), g = Rgb(out, 1), b = Rgb(out, 2);
    const int spread = (r > g ? (r > b ? r - (g < b ? g : b) : b - (r < g ? r : g))
                              : (g > b ? g - (r < b ? r : b) : b - (r < g ? r : g)));
    std::printf("  %-20s (%3d,%3d,%3d)  spread %d, luma %.2f %s\n", "asphalt check", r, g, b,
                spread, (double)luma,
                (spread <= 22 && luma > 0.42f && luma < 0.62f) ? "ok" : "FAIL");
    if (!(spread <= 22 && luma > 0.42f && luma < 0.62f)) failures++;
  }

  float fog[3];
  pspshade::FogColour(fog);
  std::printf("  %-20s (%3d,%3d,%3d)  haze the objects converge to; "
              "darkest baked surface luma %.2f\n",
              "GE fog colour", Rgb(fog, 0), Rgb(fog, 1), Rgb(fog, 2),
              (double)darkest);

  // Nothing may crush to black: the darkest surface in the reference frame is
  // luma 0.31, and the whole complaint about the old bake was black buildings.
  if (darkest < 0.22f) {
    std::printf("FAIL the darkest baked surface is %.2f (black on the console)\n",
                (double)darkest);
    failures++;
  }
  // The fog has to be the frame's haze, not a dark brown: the reference's
  // distant houses sit at luma 0.44-0.48 and the sky behind them at 0.60-0.66.
  const float fogLuma = Luma(fog);
  if (fogLuma < 0.45f || fogLuma > 0.75f) {
    std::printf("FAIL the fog colour is luma %.2f, the frame's haze is 0.45-0.75\n",
                (double)fogLuma);
    failures++;
  }
  if (!(fog[0] > fog[1] && fog[1] > fog[2])) {
    std::printf("FAIL the fog is not warm (r %.2f g %.2f b %.2f)\n",
                (double)fog[0], (double)fog[1], (double)fog[2]);
    failures++;
  }

  std::printf("%s\n", failures == 0 ? "PSP_SHADETAIL_EXIT=0" : "PSP_SHADETAIL_FAILED");
  return failures == 0 ? 0 : 1;
}
