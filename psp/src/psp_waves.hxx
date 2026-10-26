// ElectroBench PSP -- the sea's six-sine wave field, four waves at a time.
//
// This is the part of the port that uses the VFPU's *vector* pipeline, not just
// its scalar one. The wave field is
//
//     h(x,z,t)  = sum_i amp_i * sin((x*dx_i + z*dz_i) * k_i + t*w_i)
//     dh/dx     = -sum_i dx_i*amp_i*k_i * cos(phase_i)
//     dh/dz     = -sum_i dz_i*amp_i*k_i * cos(phase_i)
//
// with six waves, so the eight lanes of two VFPU quads hold all six plus two
// zeroed lanes. One `vsin.q`/`vcos.q` pair evaluates four waves at once; three
// `vdot.q` reduce the lanes; the gradient coefficients are pre-multiplied so the
// kernel needs no per-vertex multiplies for them.
//
// Register safety: the asm uses matrices 4-7 (S400-S733) only. psp-gcc keeps
// its scalar floats in matrix 0 (S000-S031), and sceGum's own matrices live in
// the low sets, so nothing the compiler or the GU library owns is touched. The
// matrices are reserved with pspvfpu_use_matrices() in Init().
//
// Angle units: VFPU vsin/vcos do not take radians. Their input is scaled so
// that 1.0 is a quarter turn and 4.0 is a full turn -- i.e. sin(pi/2 * x) -- so
// phases are converted with 2/pi in WrapScaled() and the asm consumes those
// scaled values directly. (See the Allegrex VFPU instruction reference and
// PPSSPP's VFPU math notes; this is the one detail that silently mirrors the
// wave shape if it is wrong, hence the explicit name.)
//
// The portable PackedScalar() computes exactly what the asm does.
#pragma once

#include <cmath>

#if defined(__psp__)
#include <pspvfpu.h>
#endif

namespace pspwaves {

constexpr float kPi = 3.14159265358979f;
constexpr float kHalfPi = 1.57079632679490f;
constexpr float kTwoPi = 6.28318530717959f;
constexpr float kScaledPerRadian = 2.0f / kPi;
constexpr int kLanes = 8;
struct Wave {
  float dx, dz, k, w, amp;
};

inline constexpr Wave kWaves[6] = {
    {0.98f, 0.20f, 0.170f, 1.30f, 1.55f},
    {-0.64f, 0.77f, 0.240f, 1.60f, 1.00f},
    {0.36f, -0.93f, 0.380f, 2.10f, 0.55f},
    {-0.91f, -0.42f, 0.540f, 2.70f, 0.34f},
    {0.59f, 0.81f, 0.860f, 3.40f, 0.20f},
    {-0.20f, 0.98f, 1.450f, 4.40f, 0.11f},
};

struct Field {
  float height;
  float gx;
  float gz;
};
inline Field Reference(float x, float z, float t) {
  float h = 0.0f, sx = 0.0f, sz = 0.0f;
  for (int i = 0; i < 6; i++) {
    const Wave &w = kWaves[i];
    const float phase = (x * w.dx + z * w.dz) * w.k + t * w.w;
    h += std::sin(phase) * w.amp;
    const float c = std::cos(phase) * w.amp * w.k;
    sx += w.dx * c;
    sz += w.dz * c;
  }
  return {h, -sx, -sz};
}
struct Packed {
  alignas(16) float kdx[kLanes];
  alignas(16) float kdz[kLanes];
  alignas(16) float w[kLanes];
  alignas(16) float amp[kLanes];
  alignas(16) float coefX[kLanes];
  alignas(16) float coefZ[kLanes];
};


inline constexpr Packed kPacked = {
    {0.1666f, -0.1536f, 0.1368f, -0.4914f, 0.5074f, -0.29f, 0.0f, 0.0f},
    {0.034f, 0.1848f, -0.3534f, -0.2268f, 0.6966f, 1.421f, 0.0f, 0.0f},
    {1.3f, 1.6f, 2.1f, 2.7f, 3.4f, 4.4f, 0.0f, 0.0f},
    {1.55f, 1.0f, 0.55f, 0.34f, 0.2f, 0.11f, 0.0f, 0.0f},
    {0.25823f, -0.1536f, 0.07524f, -0.167076f, 0.10148f, -0.0319f, 0.0f, 0.0f},
    {0.0527f, 0.1848f, -0.19437f, -0.077112f, 0.13932f, 0.15631f, 0.0f, 0.0f},
};

inline constexpr const Packed &Constants() { return kPacked; }

// Radians -> VFPU angle units, wrapped to [-2, 2) (one full turn). floorf is a
// hardware instruction on Allegrex (floor.w.s), not a libm call.
inline float WrapScaled(float radians) {
  float s = radians * kScaledPerRadian;
  s -= 4.0f * std::floor(s * 0.25f + 0.5f);
  return s;
}

inline Field PackedScalar(float x, float z, float t) {
  const Packed &c = Constants();
  float ph[kLanes];
  for (int i = 0; i < kLanes; i++) {
    ph[i] = WrapScaled(x * c.kdx[i] + z * c.kdz[i] + t * c.w[i]);
  }
  float h = 0.0f, gx = 0.0f, gz = 0.0f;
  for (int i = 0; i < kLanes; i++) {
    const float rad = ph[i] * kHalfPi;
    h += std::sin(rad) * c.amp[i];
    gx += std::cos(rad) * c.coefX[i];
    gz += std::cos(rad) * c.coefZ[i];
  }
  return {h, -gx, -gz};
}

#if defined(__psp__)

// Reserve the high matrix sets for this kernel. Safe to call more than once.
inline void Init() { pspvfpu_use_matrices(nullptr, 0, VMAT4 | VMAT5 | VMAT6 | VMAT7); }

inline Field Vfpu(float x, float z, float t) {
  const Packed &c = Constants();
  alignas(16) float ph[kLanes];
  for (int i = 0; i < kLanes; i++) {
    ph[i] = WrapScaled(x * c.kdx[i] + z * c.kdz[i] + t * c.w[i]);
  }

  // sv.s stores through an address register, so the results live in one
  // aligned array rather than in three "=m" operands (whose expansions are
  // memory references, not registers).
  alignas(16) float out[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  asm volatile(
      // phases, amplitudes and the two gradient coefficient halves
      "lv.q C700, 0(%[ph])\n\t"
      "lv.q C710, 16(%[ph])\n\t"
      "lv.q C500, 0(%[amp])\n\t"
      "lv.q C510, 16(%[amp])\n\t"
      "vone.q C430\n\t"
      // h = dot(sin(phase), amp)
      "vsin.q C720, C700\n\t"
      "vsin.q C730, C710\n\t"
      "vmul.q C720, C720, C500\n\t"
      "vmul.q C730, C730, C510\n\t"
      "vadd.q C720, C720, C730\n\t"
      "vdot.q S400, C720, C430\n\t"
      "sv.s S400, 0(%[out])\n\t"
      // one cosine pass, used by both gradient components
      "vcos.q C720, C700\n\t"
      "vcos.q C730, C710\n\t"
      "lv.q C520, 0(%[cx])\n\t"
      "lv.q C530, 16(%[cx])\n\t"
      "vmul.q C620, C720, C520\n\t"
      "vmul.q C630, C730, C530\n\t"
      "vadd.q C620, C620, C630\n\t"
      "vdot.q S401, C620, C430\n\t"
      "sv.s S401, 4(%[out])\n\t"
      "lv.q C600, 0(%[cz])\n\t"
      "lv.q C610, 16(%[cz])\n\t"
      "vmul.q C620, C720, C600\n\t"
      "vmul.q C630, C730, C610\n\t"
      "vadd.q C620, C620, C630\n\t"
      "vdot.q S402, C620, C430\n\t"
      "sv.s S402, 8(%[out])\n\t"
      :
      : [out] "r"(out), [ph] "r"(ph), [amp] "r"(c.amp), [cx] "r"(c.coefX),
        [cz] "r"(c.coefZ)
      : "memory");
  // The kernel accumulates the positive cosine sums; the shader's convention is
  // the negated height gradient.
  return {out[0], -out[1], -out[2]};
}

inline Field FieldAt(float x, float z, float t) { return Vfpu(x, z, t); }

#else

inline void Init() {}
inline Field FieldAt(float x, float z, float t) { return PackedScalar(x, z, t); }

#endif

}  // namespace pspwaves
