// ElectroBench PSP -- the tail of shaders/pole/object_frag.glsl, in C++.
//
// The GE has no fragment stage, so every surface's colour is baked per vertex
// (psp_scene4.cxx's Shade()). What that bake writes has to be the colour the
// fragment shader would have *displayed*, not the radiance it computed, or the
// two ports end up with different contrast: the shader's last steps are a
// hard/tone curve - the silhouette lift, col/(0.35col+0.72), the cool/warm
// split tone and a 1/2.2 gamma - and that curve multiplies small values by
// almost an order of magnitude (a 0.029 wall becomes 0.43) while barely
// touching mid-tones. Writing radiance straight into the framebuffer is what
// crushed every distant silhouette to black in the PSP port while the ground,
// which had been hand-tuned with an additive lift at exactly its own
// brightness, stayed put. Measured: an ambient-only wall came out at (7,6,5)
// against the desktop frame's (100,79,59).
//
// So the port keeps the shader's arithmetic verbatim here instead of per
// material fudge factors, and Shade() only has to produce the same radiance
// the shader feeds it. Split out of psp_scene4.cxx because it has no SDK
// dependency: buildtools/test_psp_shadetail.cxx (make -C psp check-shade) runs
// this header on the host and compares its output against the colours the
// desktop/Vita frame actually carries, which is the only way to *check* a
// GE-only bake.
#pragma once

#include <cmath>

namespace pspshade {

// --------------------------------------------------------------- materials
enum Material {
  kMatPaint, kMatWood, kMatGround, kMatRoad, kMatLine, kMatCable,
  kMatCeramic, kMatMetal, kMatWall, kMatRoof, kMatGlass, kMatLeaf,
  kMatGlow, kMatSteel, kMatKerb, kMatConcrete,
};

inline float Clamp(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float Saturate(float v) { return Clamp(v, 0.0f, 1.0f); }
inline float Mix(float a, float b, float t) { return a + (b - a) * t; }
inline float SmoothStep(float a, float b, float x) {
  float t = Saturate((x - a) / (b - a));
  return t * t * (3.0f - 2.0f * t);
}

// object_frag.glsl: `silh = clamp(1 - dot(base, vec3(0.333)) * 2.6, 0, 1)` then
// `col = mix(col, vec3(0.52, 0.30, 0.14), silh * 0.16)` -- a warm bounce that
// keeps dark materials from reading as holes in the frame.
inline float Silhouette(float albedoMean) {
  return Saturate(1.0f - albedoMean * 2.6f);
}

// object_frag.glsl, from `float silh = ...` to the closing
// `col = pow(max(col, vec3(0.0)), vec3(1.0 / 2.2))`.
inline void Display(float r, float g, float b, float albedoMean, float out[3]) {
  const float w = Silhouette(albedoMean) * 0.16f;
  r = Mix(r, 0.52f, w);
  g = Mix(g, 0.30f, w);
  b = Mix(b, 0.14f, w);

  r = Clamp(r, 0.0f, 4.0f);
  g = Clamp(g, 0.0f, 4.0f);
  b = Clamp(b, 0.0f, 4.0f);
  r = r / (r * 0.35f + 0.72f);
  g = g / (g * 0.35f + 0.72f);
  b = b / (b * 0.35f + 0.72f);

  // The split tone: shadowed pixels cool off, highlights warm up, both against
  // the tonemapped luma so the change is a tint and not a brightness shift.
  const float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
  const float cool = (1.0f - SmoothStep(0.02f, 0.34f, lum)) * 0.50f;
  r = Mix(r, r * 0.93f, cool);
  g = Mix(g, g * 0.98f, cool);
  b = Mix(b, b * 1.13f, cool);
  const float warm = SmoothStep(0.52f, 1.00f, lum) * 0.45f;
  r = Mix(r, r * 1.07f, warm);
  g = Mix(g, g * 1.01f, warm);
  b = Mix(b, b * 0.92f, warm);

  out[0] = std::pow(Clamp(Mix(lum, r, 1.12f), 0.0f, 1.0f), 1.0f / 2.2f);
  out[1] = std::pow(Clamp(Mix(lum, g, 1.12f), 0.0f, 1.0f), 1.0f / 2.2f);
  out[2] = std::pow(Clamp(Mix(lum, b, 1.12f), 0.0f, 1.0f), 1.0f / 2.2f);
}

// The haze the object shader converges to (hazeColFor() at toSun = 0, the
// average view direction of a corridor shot), pushed through the same tail.
// GE fog can only take one colour for the whole scene, so this is what
// distance has to converge to: the shader mixes 70% of the haze in at most.
const float kHazeLinear[3] = {0.70f, 0.52f, 0.37f};

// The GE fades a vertex to its fog colour by a factor that reaches 1 at the far
// plane, where the shader's own mix saturates at 0.70 of the haze and keeps the
// remaining 30% of the surface. Mixing the haze 70/30 with a mid-tone wall (a
// distant house, the thing the fog is for) and displaying that gives the
// constant the GE wants.
inline void FogColour(float out[3]) {
  const float kTypicalWall[3] = {0.120f, 0.100f, 0.082f};
  Display(Mix(kHazeLinear[0], kTypicalWall[0], 0.30f),
          Mix(kHazeLinear[1], kTypicalWall[1], 0.30f),
          Mix(kHazeLinear[2], kTypicalWall[2], 0.30f), 0.20f, out);
}

// ------------------------------------------------------------- the surface
// object_frag.glsl's light sum for one vertex, then its display tail. `n` is
// the face normal, `sun` the unit sun direction, `albedo` the palette entry.
// Returns display-space RGB (0..1), which is what the GE wants in a vertex
// colour.
inline void Surface(const float albedo[3], float nx, float ny, float nz,
                    const float sun[3], Material m, float out[3]) {
  const float mean = (albedo[0] + albedo[1] + albedo[2]) * (1.0f / 3.0f);

  if (m == kMatGlow) {   // street-lamp lenses, lit windows, vending fronts
    // object_frag.glsl's glow branch skips the tonemap and only applies gamma,
    // and those surfaces are meant to sit above the frame's own white point.
    out[0] = std::pow(Saturate(albedo[0] * 1.05f), 1.0f / 2.2f);
    out[1] = std::pow(Saturate(albedo[1] * 1.05f), 1.0f / 2.2f);
    out[2] = std::pow(Saturate(albedo[2] * 1.05f), 1.0f / 2.2f);
    return;
  }

  const float up = ny * 0.5f + 0.5f;
  const float ambR = Mix(0.125f, 0.395f, up);
  const float ambG = Mix(0.086f, 0.340f, up);
  const float ambB = Mix(0.064f, 0.360f, up);

  float wrap = 0.0f;
  if (m == kMatCable) wrap = 0.35f;
  else if (m == kMatSteel) wrap = 0.13f;

  const float ndl = nx * sun[0] + ny * sun[1] + nz * sun[2];
  const float diff = Saturate((ndl + wrap) / (1.0f + wrap));

  const float sunR = 1.00f, sunG = 0.66f, sunB = 0.34f;
  float r = albedo[0] * (ambR + sunR * diff * 1.15f);
  float g = albedo[1] * (ambG + sunG * diff * 1.15f);
  float b = albedo[2] * (ambB + sunB * diff * 1.15f);

  // A glint on the hard materials. The desktop BRDF needs the per-pixel view
  // vector; with a baked vertex colour only the sun angle is left, so this is
  // the top-face highlight that carries the galvanised steel silhouette.
  if (m == kMatSteel || m == kMatMetal || m == kMatCeramic ||
      m == kMatKerb || m == kMatRoof || m == kMatGlass) {
    const float sharp = (m == kMatGlass) ? 48.0f : (m == kMatCeramic ? 26.0f : 20.0f);
    const float glint = std::pow(Saturate(ndl), sharp) * 0.24f;
    r += glint * sunR;
    g += glint * sunG;
    b += glint * sunB;
  }
  if (m == kMatGlass && ny > 0.0f) {   // window glass mirrors the dusk sky
    r += 0.030f;
    g += 0.034f;
    b += 0.055f;
  }

  // The wet-surface reflection: object_frag.glsl adds skyRefl * wetF, a grazing
  // reflection of the (warm) sky that is what makes the carriageway read as wet
  // asphalt rather than a black strip. The view vector is not available in a
  // baked vertex colour, so each of those materials gets a constant radiance
  // with the reflection's colour instead of its angular falloff. These are
  // linear radiance additions, like the shader's: the display tail below is
  // what lifts them, so they must not carry a lift of their own.
  // Measured against the desktop frame (buildtools/test_psp_shadetail.cxx): the
  // road at (163,110,77), the gravel at (128,100,78), the kerb a pale grey.
  if (m == kMatRoad) {
    r += 0.196f; g += 0.039f; b += 0.000f;
  } else if (m == kMatGround) {
    r += 0.000f; g += 0.000f; b += 0.000f;
  } else if (m == kMatKerb) {
    r += 0.030f; g += 0.024f; b += 0.016f;
  } else if (m == kMatLine) {
    r += 0.055f; g += 0.045f; b += 0.030f;
  }

  Display(r, g, b, mean, out);
}

}  // namespace pspshade
