// ElectroBench PSP -- small, accurate-enough replacements for the libm calls
// in the per-vertex path.
//
// The Allegrex has no hardware atan2/asin/acos, so every one of those is a
// libm call. The sea shades 5,311 vertices every frame and used one atan2 and
// one asin per vertex (plus an acos per sky texel), which is the single largest
// avoidable cost in the port. These are the standard minimax-style polynomials:
// max absolute error is about 1.5 milliradians, i.e. 0.06 texels on the 256-wide
// sky panorama. tools/test_psp_math.cxx checks the error against libm, so the
// approximations are verified on the host rather than assumed.
#pragma once

#include <cmath>

namespace pspfast {

constexpr float kPi = 3.14159265358979f;
constexpr float kHalfPi = 1.57079632679490f;
constexpr float kTwoPi = 6.28318530717959f;

inline float Atan2(float y, float x) {
  const float ax = std::fabs(x), ay = std::fabs(y);
  const bool steep = ay > ax;
  const float t = steep ? (ax / (ay + 1e-20f)) : (ay / (ax + 1e-20f));
  const float t2 = t * t;
  float r = ((-0.0464964749f * t2 + 0.15931422f) * t2 - 0.327622764f) * t2 * t + t;
  if (steep) r = kHalfPi - r;
  if (x < 0.0f) r = kPi - r;
  return y < 0.0f ? -r : r;
}

inline float Asin(float x) {
  x = x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
  return Atan2(x, std::sqrt(1.0f - x * x));
}

inline float Acos(float x) { return kHalfPi - Asin(x); }
inline float Pow5(float x) {
  const float x2 = x * x;
  return x2 * x2 * x;
}

inline float Pow10(float x) {
  const float x2 = x * x;
  const float x4 = x2 * x2;
  return x4 * x4 * x2;
}

}  // namespace pspfast
