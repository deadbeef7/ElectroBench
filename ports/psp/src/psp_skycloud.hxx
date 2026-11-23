// ElectroBench PSP -- the cloud deck of scene 4's baked sky.
//
// The GE has no shaders, so the sky is a 128x64 equirect panorama baked on the
// CPU from shaders/pole/sky_frag.glsl's atmosphere() integral and its cloud
// deck, one row per frame (psp_scene4.cxx owns the pacing and the packing).
// This header is the cloud half of that bake.
#pragma once

#include <cmath>

namespace pspsky {

constexpr int kNoiseN = 64;

// ---------------------------------------------------------------- fast math
inline float Clamp(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float Saturate(float v) { return Clamp(v, 0.0f, 1.0f); }
inline float SmoothStep(float a, float b, float x) {
  float t = Saturate((x - a) / (b - a));
  return t * t * (3.0f - 2.0f * t);
}
inline float Mix(float a, float b, float t) { return a + (b - a) * t; }

// ------------------------------------------------------------------- noise
inline float *NoiseTable() {
  static float table[kNoiseN * kNoiseN];
  return table;
}

inline void BuildNoiseTable() {
  float *table = NoiseTable();
  unsigned int s = 0x7F4A7C15u;
  for (int i = 0; i < kNoiseN * kNoiseN; i++) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    table[i] = (float)((s >> 8) & 0xFFFF) * (1.0f / 65535.0f);
  }
}

// Bilinear, wrapping sample of the 64x64 table. The table spans u,v in [0,1).
inline float NoiseLut(float u, float v) {
  u *= (float)kNoiseN; v *= (float)kNoiseN;
  int ui = (int)std::floor(u), vi = (int)std::floor(v);
  float uf = u - (float)ui, vf = v - (float)vi;
  int u0 = ui & (kNoiseN - 1), v0 = vi & (kNoiseN - 1);
  int u1 = (u0 + 1) & (kNoiseN - 1), v1 = (v0 + 1) & (kNoiseN - 1);
  const float *t = NoiseTable();
  float a = t[v0 * kNoiseN + u0], b = t[v0 * kNoiseN + u1];
  float c = t[v1 * kNoiseN + u0], d = t[v1 * kNoiseN + u1];
  return Mix(Mix(a, b, uf), Mix(c, d, uf), vf);
}

inline float OctaveWeight(float freq, float footprint) {
  if (footprint <= 0.0f) return 1.0f;   // no footprint known: leave it alone
  float texelsPerFeature =
      1.0f / (freq * (float)kNoiseN * footprint);
  return SmoothStep(0.55f, 1.35f, texelsPerFeature);
}
inline float Fbm2(float x, float y, float footprint) {
  float n1 = NoiseLut(x, y);
  float n2 = NoiseLut(x * 2.17f + 19.3f, y * 2.17f + 19.3f);
  return 0.66f * (0.5f + (n1 - 0.5f) * OctaveWeight(1.00f, footprint)) +
         0.34f * (0.5f + (n2 - 0.5f) * OctaveWeight(2.17f, footprint));
}

inline float Fbm3(float x, float y, float footprint) {
  float n1 = NoiseLut(x, y);
  float n2 = NoiseLut(x * 2.13f + 11.7f, y * 2.13f + 11.7f);
  float n3 = NoiseLut(x * 4.31f + 41.2f, y * 4.31f + 41.2f);
  return 0.54f * (0.5f + (n1 - 0.5f) * OctaveWeight(1.00f, footprint)) +
         0.29f * (0.5f + (n2 - 0.5f) * OctaveWeight(2.13f, footprint)) +
         0.17f * (0.5f + (n3 - 0.5f) * OctaveWeight(4.31f, footprint));
}
inline void LayerCoords(float dx, float dy, float dz, float floorH, float scale,
                        float *u, float *v) {
  float h = dy > floorH ? dy : floorH;
  *u = dx / h * 0.30f * scale;
  *v = dz / h * 0.30f * scale;
}

inline void DirFromAngles(float azim, float elev, float *dx, float *dy, float *dz) {
  float ce = std::cos(elev);
  *dx = ce * std::cos(azim);
  *dy = std::sin(elev);
  *dz = ce * std::sin(azim);
}
inline float LayerFootprint(float azim, float elev, float dazim, float delev,
                            float floorH, float scale) {
  float ax, ay, az, bx, by, bz, cx, cy, cz;
  DirFromAngles(azim, elev, &ax, &ay, &az);
  DirFromAngles(azim + dazim, elev, &bx, &by, &bz);
  DirFromAngles(azim, elev + delev, &cx, &cy, &cz);
  float u0, v0, u1, v1, u2, v2;
  LayerCoords(ax, ay, az, floorH, scale, &u0, &v0);
  LayerCoords(bx, by, bz, floorH, scale, &u1, &v1);
  LayerCoords(cx, cy, cz, floorH, scale, &u2, &v2);
  float du = std::fabs(u1 - u0) > std::fabs(u2 - u0) ? std::fabs(u1 - u0)
                                                     : std::fabs(u2 - u0);
  float dv = std::fabs(v1 - v0) > std::fabs(v2 - v0) ? std::fabs(v1 - v0)
                                                     : std::fabs(v2 - v0);
  return du > dv ? du : dv;
}

constexpr float kCloudCoordScale = 0.12f;

inline float CloudCoverage(float dx, float dy, float dz, float driftx,
                           float driftz, float footprint, float *detOut,
                           float coordScale = kCloudCoordScale) {
  float h = dy;
  if (detOut) *detOut = 0.5f;
  if (h <= 0.030f) return 0.0f;

  float u, v;
  LayerCoords(dx, dy, dz, 0.075f, 1.0f, &u, &v);
  u *= coordScale;
  v *= coordScale;
  float fp = footprint * coordScale;
  // The coordinates are scaled below, so the footprint is scaled with them.
  float base = Fbm3(u * 1.75f + driftx, v * 1.75f + driftz, fp * 1.75f);
  float det = Fbm2(u * 6.50f - driftx * 2.4f + 31.0f,
                   v * 6.50f - driftz * 2.4f + 31.0f, fp * 6.50f);
  if (detOut) *detOut = det;

  float dens = base - 0.32f * (det - 0.5f);
  float cover = SmoothStep(0.470f, 0.660f, dens) * 0.94f;
  cover *= 0.80f + 0.20f * SmoothStep(0.40f, 0.66f, det);
  cover *= SmoothStep(0.035f, 0.26f, h);
  cover *= 1.0f - SmoothStep(0.62f, 0.99f, h);
  return cover;
}
inline float CirrusVeil(float dx, float dy, float dz, float timeSec,
                        float footprint) {
  float h = dy;
  if (h <= 0.035f) return 0.0f;
  float baseU, baseV;
  LayerCoords(dx, dy, dz, 0.055f, 1.0f, &baseU, &baseV);
  float n = Fbm2(baseU * 9.0f - timeSec * 0.010f,
                 baseV * 4.5f + timeSec * 0.004f, footprint);
  float veil = SmoothStep(0.48f, 0.82f, n);
  veil *= SmoothStep(0.035f, 0.20f, h) * (1.0f - SmoothStep(0.55f, 0.95f, h));
  return veil;
}

}  // namespace pspsky
