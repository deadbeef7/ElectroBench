// Host-side check for the PSP port's CPU math.
//
// The PSP itself cannot be run from this build machine, so the two pieces that
// decide whether the sea looks right are tested here instead:
//
//   1. pspfast::Atan2/Asin/Acos against libm over the domain the shader uses;
//      these replaced the per-vertex atan2f/asinf calls.
//   2. pspwaves::kPacked and PackedScalar() against kWaves/Reference().
//      PackedScalar is the lane-for-lane twin of the VFPU asm in
//      psp_waves.hxx, so a packing or sign mistake is caught here even though
//      the vsin.q/vdot.q instructions themselves only run on Allegrex.
//
// Build and run:
//   g++ -O2 -std=c++17 -Ipsp/src tools/test_psp_math.cxx -o build/test_psp_math
//   ./build/test_psp_math
#include <cmath>
#include <cstdio>

#include "psp_fastmath.hxx"
#include "psp_waves.hxx"

namespace {

int gFailures = 0;

void Check(bool ok, const char *what, double worst, double limit) {
  std::printf("%s %-46s worst=%.6g limit=%.6g\n", ok ? "OK  " : "FAIL", what, worst, limit);
  if (!ok) gFailures++;
}

}  // namespace

int main() {
  // ---- 1. trig -------------------------------------------------------------
  double atanErr = 0.0;
  for (int yi = -40; yi <= 40; yi++) {
    for (int xi = -40; xi <= 40; xi++) {
      const float x = (float)xi * 0.37f;
      const float y = (float)yi * 0.41f;
      if (x == 0.0f && y == 0.0f) continue;
      const double got = pspfast::Atan2(y, x);
      const double want = std::atan2((double)y, (double)x);
      const double err = std::fabs(got - want);
      if (err > atanErr) atanErr = err;
    }
  }
  Check(atanErr < 2.5e-3, "fast atan2 vs std::atan2", atanErr, 2.5e-3);

  double asinErr = 0.0, acosErr = 0.0;
  for (int i = -999; i <= 999; i++) {
    const float x = (float)i / 1000.0f;
    const double ai = std::fabs((double)pspfast::Asin(x) - std::asin((double)x));
    const double ac = std::fabs((double)pspfast::Acos(x) - std::acos((double)x));
    if (ai > asinErr) asinErr = ai;
    if (ac > acosErr) acosErr = ac;
  }
  Check(asinErr < 4.0e-3, "fast asin vs std::asin (|x| <= 0.999)", asinErr, 4.0e-3);
  Check(acosErr < 4.0e-3, "fast acos vs std::acos (|x| <= 0.999)", acosErr, 4.0e-3);

  // ---- 2. packed constants -------------------------------------------------
  const pspwaves::Packed &c = pspwaves::Constants();
  double packErr = 0.0;
  for (int i = 0; i < pspwaves::kLanes; i++) {
    float kdx = 0, kdz = 0, w = 0, amp = 0, cx = 0, cz = 0;
    if (i < 6) {
      const pspwaves::Wave &wv = pspwaves::kWaves[i];
      kdx = wv.dx * wv.k;
      kdz = wv.dz * wv.k;
      w = wv.w;
      amp = wv.amp;
      cx = wv.dx * wv.amp * wv.k;
      cz = wv.dz * wv.amp * wv.k;
    }
    const double errs[] = {
        std::fabs(c.kdx[i] - kdx), std::fabs(c.kdz[i] - kdz), std::fabs(c.w[i] - w),
        std::fabs(c.amp[i] - amp), std::fabs(c.coefX[i] - cx), std::fabs(c.coefZ[i] - cz),
    };
    for (double e : errs)
      if (e > packErr) packErr = e;
  }
  Check(packErr < 5.0e-5, "packed constants match kWaves", packErr, 5.0e-5);

  // ---- 3. packed kernel vs direct sum -------------------------------------
  double waveErr = 0.0;
  for (int t = 0; t < 4; t++) {
    for (int zi = -8; zi <= 8; zi++) {
      for (int xi = -8; xi <= 8; xi++) {
        const float x = (float)xi * 137.3f;
        const float z = (float)zi * 191.7f;
        const float tt = (float)t * 11.9f;
        const pspwaves::Field ref = pspwaves::Reference(x, z, tt);
        const pspwaves::Field got = pspwaves::PackedScalar(x, z, tt);
        const double e[] = {std::fabs(got.height - ref.height), std::fabs(got.gx - ref.gx),
                            std::fabs(got.gz - ref.gz)};
        for (double v : e)
          if (v > waveErr) waveErr = v;
      }
    }
  }
  Check(waveErr < 2.0e-4, "packed wave kernel vs direct sum", waveErr, 2.0e-4);

  std::printf("%s\n", gFailures ? "PSP_MATH_CHECK_FAIL" : "PSP_MATH_CHECK_OK");
  return gFailures ? 1 : 0;
}
