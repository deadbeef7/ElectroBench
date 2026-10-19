// ElectroBench — PlayStation Portable port, SCENE 2 (the dusk ocean).
//
// The PSP Graphics Engine has no programmable shaders, so this is not a
// translation of shaders/ps14/{sea,sky}_frag.glsl but a rebuild of the same
// scene on the fixed-function pipeline. What maps where:
//
//   * wave field, normals, cloud shadows, fresnel, body colour, foam
//     -> per-vertex VFPU float code here (the GE cannot shade per pixel)
//   * reflection + haze  -> additive pass sampled from the same sky texture
//     the backdrop uses (equirectangular), UV from the reflected ray
//   * sun glitter        -> additive pass: the smooth path gate is per-vertex,
//     the sparkle itself is a dot texture scrolled over it
//   * sky                -> one small equirect texture, regenerated every few
//     frames (cloud drift), drawn as a coarse ray-cast grid
//
// Everything is the same numbers as the GLSL: kSunDir, the six wave sines, the
// maroon horizon, the indigo water body, the nine-cloud deck from scene2.cxx.
#include "psp_scene2.hxx"

#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>

#include <cmath>
#include <cstring>

namespace {

// ---------------------------------------------------------------- constants
constexpr float PI = 3.14159265358979f;
constexpr float TWO_PI = 2.0f * PI;
constexpr int kSegments = 112;      // angular divisions around the camera
constexpr int kRings = 46;          // distance rings (exponential spacing)
constexpr float kRing0 = 0.35f;     // first ring radius, world units
constexpr float kRingGrowth = 1.16f;
constexpr int kMaxVerts = (kSegments + 1) * (kRings + 1);
constexpr int kMaxIndices = kSegments * kRings * 6;
constexpr int kSkyW = 256, kSkyH = 128;
constexpr int kSparkW = 128, kSparkH = 128;
constexpr int kChaosW = 128;
constexpr int kSkyGridX = 14, kSkyGridY = 9;
constexpr int kSkyGridVerts = (kSkyGridX + 1) * (kSkyGridY + 1);

const float kSunDir[3] = {0.824f, 0.287f, 0.489f};
const float kHorizonColor[3] = {0.10f, 0.052f, 0.062f};
const float kWaterColor[3] = {0.034f, 0.020f, 0.052f};

// The cloud deck (must match src/scene2.cxx).
constexpr int kClouds = 9;
const float kCloudAzim[kClouds] = {0.18f, 0.88f, 1.75f, 2.65f, 3.75f, 4.65f, 5.75f, 0.30f, 0.80f};
const float kCloudElev[kClouds] = {0.190f, 0.300f, 0.250f, 0.380f, 0.220f, 0.330f, 0.160f, 0.420f, 0.465f};
const float kCloudRad[kClouds] = {0.048f, 0.037f, 0.030f, 0.043f, 0.027f, 0.036f, 0.050f, 0.026f, 0.030f};
const float kCloudStretch[kClouds] = {3.3f, 2.4f, 2.1f, 2.8f, 2.2f, 2.3f, 3.6f, 4.6f, 4.2f};
const float kCloudDrift[kClouds] = {-0.0025f, 0.0032f, -0.0018f, 0.0022f, -0.0027f, 0.0015f, 0.0020f, -0.0015f, 0.0012f};

// ---------------------------------------------------------------- fast math
// The Allegrex has no hardware trig: a LUT with linear interpolation is what
// keeps ~5k vertices of shading inside the frame budget.
float gSin[4097];
float gExp[513];     // exp(-8..0)
float gChaos[kChaosW * kChaosW];

void BuildMathTables() {
  for (int i = 0; i <= 4096; i++) gSin[i] = std::sin(TWO_PI * (float)i / 4096.0f);
  for (int i = 0; i <= 512; i++) gExp[i] = std::exp(-8.0f * (float)i / 512.0f);
  unsigned int s = 0x9E3779B9u;
  for (int i = 0; i < kChaosW * kChaosW; i++) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    gChaos[i] = (float)((s >> 8) & 0xFFFF) * (1.0f / 65535.0f);
  }
}

inline float WrapAngle(float x) {
  x -= TWO_PI * std::floor(x * (1.0f / TWO_PI));
  return x;
}

inline float FastSin(float x) {
  x = WrapAngle(x) * (4096.0f / TWO_PI);
  int i = (int)x;
  float f = x - (float)i;
  return gSin[i] + (gSin[i + 1] - gSin[i]) * f;
}

inline float FastCos(float x) { return FastSin(x + PI * 0.5f); }

inline float FastExpNeg(float x) { // x >= 0 -> exp(-x)
  if (x >= 8.0f) return 0.0f;
  float t = x * 64.0f;
  int i = (int)t;
  float f = t - (float)i;
  return gExp[i] + (gExp[i + 1] - gExp[i]) * f;
}

inline float Clamp(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float Saturate(float v) { return Clamp(v, 0.0f, 1.0f); }
inline float SmoothStep(float a, float b, float x) {
  float t = Saturate((x - a) / (b - a));
  return t * t * (3.0f - 2.0f * t);
}
inline float Mix(float a, float b, float t) { return a + (b - a) * t; }

inline float Chaos(float x, float z) {
  float u = WrapAngle(x * 0.35f) * (kChaosW / TWO_PI);
  float v = WrapAngle(z * 0.35f) * (kChaosW / TWO_PI);
  int ui = (int)u, vi = (int)v;
  float uf = u - (float)ui, vf = v - (float)vi;
  int u0 = ui & (kChaosW - 1), v0 = vi & (kChaosW - 1);
  int u1 = (u0 + 1) & (kChaosW - 1), v1 = (v0 + 1) & (kChaosW - 1);
  float a = gChaos[v0 * kChaosW + u0], b = gChaos[v0 * kChaosW + u1];
  float c = gChaos[v1 * kChaosW + u0], d = gChaos[v1 * kChaosW + u1];
  return Mix(Mix(a, b, uf), Mix(c, d, uf), vf);
}

// --------------------------------------------------------------- wave field
// Same six sines as waveHeight() in shaders/ps14/sea_frag.glsl.
struct Wave { float dx, dz, k, w, amp; };
const Wave kWaves[6] = {
    {0.98f, 0.20f, 0.170f, 1.30f, 1.55f},
    {-0.64f, 0.77f, 0.240f, 1.60f, 1.00f},
    {0.36f, -0.93f, 0.380f, 2.10f, 0.55f},
    {-0.91f, -0.42f, 0.540f, 2.70f, 0.34f},
    {0.59f, 0.81f, 0.860f, 3.40f, 0.20f},
    {-0.20f, 0.98f, 1.450f, 4.40f, 0.11f},
};

inline float WaveHeight(float x, float z, float t) {
  float h = 0.0f;
  for (int i = 0; i < 6; i++) {
    const Wave &w = kWaves[i];
    h += FastSin((x * w.dx + z * w.dz) * w.k + t * w.w) * w.amp;
  }
  return h;
}

inline void WaveGradient(float x, float z, float t, float *gx, float *gz) {
  float sx = 0.0f, sz = 0.0f;
  for (int i = 0; i < 6; i++) {
    const Wave &w = kWaves[i];
    float c = FastCos((x * w.dx + z * w.dz) * w.k + t * w.w) * w.amp * w.k;
    sx += w.dx * c;
    sz += w.dz * c;
  }
  *gx = -sx; // height gradient -> normal slope (matches the GLSL's -dh/dx)
  *gz = -sz;
}

// Micro-chop the fragment shader adds near the camera (detailNormals()).
inline void DetailGradient(float x, float z, float t, float dist, float *gx, float *gz) {
  float fade = FastExpNeg(dist * 0.004f);
  if (fade < 0.02f) return;
  float w1 = FastSin((x * 0.86f + z * 0.51f) * 1.15f + t * 5.10f);
  float w2 = FastSin((x * -0.44f + z * 0.90f) * 2.30f + t * 6.80f);
  float w3 = FastSin((x * 0.22f + z * -0.97f) * 4.40f + t * 8.60f);
  *gx += (0.86f * 1.15f * w1 * 0.045f + -0.44f * 2.30f * w2 * 0.022f + 0.22f * 4.40f * w3 * 0.010f) * fade;
  *gz += (0.51f * 1.15f * w1 * 0.045f + 0.90f * 2.30f * w2 * 0.022f + -0.97f * 4.40f * w3 * 0.010f) * fade;
}

// ----------------------------------------------------------- cloud shadows
float gCloudC[kClouds][3];
float gCloudT1[kClouds][3];
float gCloudT2[kClouds][3];

void UpdateClouds(float t) {
  for (int i = 0; i < kClouds; i++) {
    float a = kCloudAzim[i] + kCloudDrift[i] * t;
    float e = kCloudElev[i];
    float ce = FastCos(e), se = FastSin(e);
    float ca = FastCos(a), sa = FastSin(a);
    float c[3] = {ce * ca, se, ce * sa};
    float t1[3] = {-sa, 0.0f, ca};
    float l1 = std::sqrt(t1[0] * t1[0] + t1[2] * t1[2]);
    if (l1 > 1e-5f) { t1[0] /= l1; t1[2] /= l1; }
    float t2[3] = {c[1] * t1[2] - c[2] * t1[1],
                   c[2] * t1[0] - c[0] * t1[2],
                   c[0] * t1[1] - c[1] * t1[0]};
    float l2 = std::sqrt(t2[0] * t2[0] + t2[1] * t2[1] + t2[2] * t2[2]);
    if (l2 > 1e-5f) { t2[0] /= l2; t2[1] /= l2; t2[2] /= l2; }
    for (int k = 0; k < 3; k++) { gCloudC[i][k] = c[k]; gCloudT1[i][k] = t1[k]; gCloudT2[i][k] = t2[k]; }
  }
}

inline float SmoothMax(float a, float b, float k) {
  float h = Saturate(0.5f + 0.5f * (a - b) / k);
  return Mix(b, a, h) + k * h * (1.0f - h);
}

float CloudShadow(float wx, float wy, float wz, float ex, float ey, float ez,
                  float sdx, float sdy, float sdz) {
  float travel = (620.0f - wy) / (sdy > 0.15f ? sdy : 0.15f);
  if (travel < 0.0f) travel = 0.0f;
  float hx = wx - ex + sdx * travel;
  float hy = wy - ey + sdy * travel;
  float hz = wz - ez + sdz * travel;
  float il = 1.0f / std::sqrt(hx * hx + hy * hy + hz * hz + 1e-6f);
  hx *= il; hy *= il; hz *= il;

  float density = 0.0f;
  for (int i = 0; i < kClouds; i++) {
    const float *c = gCloudC[i];
    float dx = hx - c[0], dy = hy - c[1], dz = hz - c[2];
    float r = kCloudRad[i];
    if (dx * dx + dy * dy + dz * dz > r * r * 12.0f) continue; // cheap reject
    const float *t1 = gCloudT1[i];
    const float *t2 = gCloudT2[i];
    float stretch = kCloudStretch[i] > 1.0f ? kCloudStretch[i] : 1.0f;
    float qx = (dx * t1[0] + dy * t1[1] + dz * t1[2]) / stretch / r;
    float qy = (dx * t2[0] + dy * t2[1] + dz * t2[2]) / r;
    float r2 = qx * qx + qy * qy;
    if (r2 > 3.1f) continue;
    float local = 0.55f * (1.0f - SmoothStep(0.35f, 1.25f, std::sqrt(r2)));
    density = SmoothMax(density, local, 0.07f);
  }
  return Clamp(FastExpNeg(density * 2.4f), 0.12f, 1.0f);
}

// ----------------------------------------------------------------- textures
// CPU-side images, sampled in place: sceGuTexImage takes the pointer and the
// GE reads the texels out of main RAM (Scene2Build writeback-flushes them).
unsigned short gSkyTex[kSkyW * kSkyH] __attribute__((aligned(16)));
unsigned short gSparkTex[kSparkW * kSparkH] __attribute__((aligned(16)));

inline unsigned short Pack5551(float r, float g, float b) {
  int ir = (int)(Saturate(r) * 31.0f + 0.5f);
  int ig = (int)(Saturate(g) * 31.0f + 0.5f);
  int ib = (int)(Saturate(b) * 31.0f + 0.5f);
  return (unsigned short)(ir | (ig << 5) | (ib << 10) | 0x8000);
}

// The sky palette: the same dusk the sky_frag tonemaps to - indigo overhead,
// a maroon band, gold sun with a wide glow, warm horizon bars, then the nine
// drifting clouds with warm undersides.
void BuildSkyTexture(float t) {
  (void)t;
  const float sd[3] = {kSunDir[0], kSunDir[1], kSunDir[2]};
  for (int y = 0; y < kSkyH; y++) {
    float elev = (0.5f - (float)y / (float)(kSkyH - 1)) * PI;
    float ce = FastCos(elev), se = FastSin(elev);
    for (int x = 0; x < kSkyW; x++) {
      float azim = ((float)x / (float)(kSkyW - 1)) * TWO_PI;
      float dx = ce * FastCos(azim), dy = se, dz = ce * FastSin(azim);

      // base gradient (indigo -> maroon -> warm horizon)
      float h = dy;
      float hn = Saturate(h);
      float r = Mix(0.86f, 0.085f, SmoothStep(0.0f, 0.55f, hn));
      float g = Mix(0.36f, 0.070f, SmoothStep(0.0f, 0.55f, hn));
      float b = Mix(0.16f, 0.20f, SmoothStep(0.0f, 0.55f, hn));
      if (h < 0.0f) { // below the horizon: dim, the sea covers it
        r *= 0.45f; g *= 0.45f; b *= 0.6f;
      }

      // sun: angular distance, glow + disc
      float cosA = dx * sd[0] + dy * sd[1] + dz * sd[2];
      float ang = std::acos(Clamp(cosA, -1.0f, 1.0f));
      float glow = FastExpNeg(ang * 7.5f);
      float halo = FastExpNeg(ang * 2.2f);
      r += 1.55f * glow + 0.55f * halo;
      g += 0.72f * glow + 0.20f * halo;
      b += 0.22f * glow + 0.06f * halo;
      float disc = SmoothStep(0.055f, 0.030f, ang);
      r += 4.2f * disc;
      g += 2.0f * disc;
      b += 0.65f * disc;

      // warm horizon bars
      if (h > -0.02f && h < 0.16f) {
        float band = SmoothStep(0.004f, 0.022f, h) * (1.0f - SmoothStep(0.050f, 0.150f, h));
        float bars = 0.55f + 0.45f * FastSin(azim * 9.0f + 1.3f) * FastSin(azim * 23.0f);
        float streak = band * Saturate(bars);
        r += 0.75f * streak;
        g += 0.30f * streak;
        b += 0.06f * streak;
      }

      // clouds: elliptical lobes, warm underside
      for (int i = 0; i < kClouds; i++) {
        const float *c = gCloudC[i];
        float ddx = dx - c[0], ddy = dy - c[1], ddz = dz - c[2];
        float rr = kCloudRad[i];
        if (ddx * ddx + ddy * ddy + ddz * ddz > rr * rr * 14.0f) continue;
        const float *t1 = gCloudT1[i];
        const float *t2 = gCloudT2[i];
        float stretch = kCloudStretch[i] > 1.0f ? kCloudStretch[i] : 1.0f;
        float qx = (ddx * t1[0] + ddy * t1[1] + ddz * t1[2]) / stretch / rr;
        float qy = (ddx * t2[0] + ddy * t2[1] + ddz * t2[2]) / rr;
        float wobble = 0.82f + 0.35f * Chaos((float)i * 7.0f + qx * 2.0f, qy * 2.0f);
        float m = std::sqrt(qx * qx + qy * qy) / wobble;
        float a = 1.0f - SmoothStep(0.55f, 1.15f, m);
        if (a <= 0.001f) continue;
        float lit = Saturate(0.35f + 0.65f * (1.0f - m)) * (0.5f + 0.5f * glow);
        r = Mix(r, 1.35f * lit + 0.10f, a * 0.85f);
        g = Mix(g, 0.72f * lit + 0.06f, a * 0.85f);
        b = Mix(b, 0.36f * lit + 0.09f, a * 0.85f);
      }

      r = r / (r + 1.0f); g = g / (g + 1.0f); b = b / (b + 1.0f);
      r = std::pow(r, 1.0f / 2.2f); g = std::pow(g, 1.0f / 2.2f); b = std::pow(b, 1.0f / 2.2f);
      gSkyTex[y * kSkyW + x] = Pack5551(r, g, b);
    }
  }

}

void BuildSparkTexture() {
  unsigned int s = 0x1234567u;
  for (int i = 0; i < kSparkW * kSparkH; i++) gSparkTex[i] = 0x0000;
  for (int d = 0; d < 900; d++) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    int x = (int)((s >> 8) % (unsigned)kSparkW);
    int y = (int)((s >> 16) % (unsigned)kSparkH);
    float v = 0.35f + 0.65f * (float)((s >> 24) & 0xFF) * (1.0f / 255.0f);
    int rr = 1 + (int)((s >> 4) & 1);
    for (int oy = -rr; oy <= rr; oy++)
      for (int ox = -rr; ox <= rr; ox++) {
        float w = 1.0f - 0.55f * (float)(ox * ox + oy * oy) / (float)(2 * rr * rr);
        int px = (x + ox + kSparkW) % kSparkW, py = (y + oy + kSparkH) % kSparkH;
        unsigned short c = Pack5551(v * w, v * w * 0.72f, v * w * 0.45f);
        unsigned short old = gSparkTex[py * kSparkW + px];
        int nr = (old & 31), ng = (old >> 5) & 31, nb = (old >> 10) & 31;
        int cr = (c & 31), cg = (c >> 5) & 31, cb = (c >> 10) & 31;
        int fr = nr + cr > 31 ? 31 : nr + cr;
        int fg = ng + cg > 31 ? 31 : ng + cg;
        int fb = nb + cb > 31 ? 31 : nb + cb;
        gSparkTex[py * kSparkW + px] = (unsigned short)(fr | (fg << 5) | (fb << 10) | 0x8000);
      }
  }

}

// ----------------------------------------------------------------- vertices
struct SeaVertex { float u, v; unsigned int color; float x, y, z; };
struct BodyVertex { unsigned int color; float x, y, z; };
struct SkyVertex { float u, v; float x, y, z; };

BodyVertex gBody[kMaxVerts] __attribute__((aligned(16)));
SeaVertex gRefl[kMaxVerts] __attribute__((aligned(16)));
SeaVertex gGlit[kMaxVerts] __attribute__((aligned(16)));
SkyVertex gSky[kSkyGridVerts] __attribute__((aligned(16)));
unsigned short gIndex[kMaxIndices] __attribute__((aligned(16)));
unsigned short gSkyIndex[kSkyGridX * kSkyGridY * 6] __attribute__((aligned(16)));

inline unsigned int Color8888(float r, float g, float b, float a) {
  int ir = (int)(Saturate(r) * 255.0f + 0.5f);
  int ig = (int)(Saturate(g) * 255.0f + 0.5f);
  int ib = (int)(Saturate(b) * 255.0f + 0.5f);
  int ia = (int)(Saturate(a) * 255.0f + 0.5f);
  return (unsigned int)(ir | (ig << 8) | (ib << 16) | (ia << 24));
}

// ------------------------------------------------------------------- state
float gRingRadius[kRings + 1];
float gCamEye[3];
float gCamYaw = 0.0f, gCamPitch = 0.0f;
const float gFovY = 55.0f;

void BuildGrid() {
  float r = kRing0;
  for (int i = 0; i <= kRings; i++) {
    gRingRadius[i] = r;
    r *= kRingGrowth;
  }
  int n = 0;
  for (int i = 0; i < kRings; i++) {
    for (int s = 0; s < kSegments; s++) {
      unsigned short a = (unsigned short)(i * (kSegments + 1) + s);
      unsigned short b = (unsigned short)(a + 1);
      unsigned short c = (unsigned short)(a + (kSegments + 1));
      unsigned short d = (unsigned short)(c + 1);
      gIndex[n++] = a; gIndex[n++] = b; gIndex[n++] = c;
      gIndex[n++] = b; gIndex[n++] = d; gIndex[n++] = c;
    }
  }
  int k = 0;
  for (int gy = 0; gy < kSkyGridY; gy++)
    for (int gx = 0; gx < kSkyGridX; gx++) {
      unsigned short a = (unsigned short)(gy * (kSkyGridX + 1) + gx);
      unsigned short b = (unsigned short)(a + 1);
      unsigned short c = (unsigned short)(a + kSkyGridX + 1);
      unsigned short d = (unsigned short)(c + 1);
      gSkyIndex[k++] = a; gSkyIndex[k++] = c; gSkyIndex[k++] = b;
      gSkyIndex[k++] = b; gSkyIndex[k++] = c; gSkyIndex[k++] = d;
    }
}

void BuildSkyGrid() {
  float cy = FastCos(gCamYaw), sy = FastSin(gCamYaw);
  float cp = FastCos(gCamPitch), sp = FastSin(gCamPitch);
  float fwd[3] = {sy * cp, sp, cy * cp};
  float right[3] = {cy, 0.0f, -sy};
  float up[3] = {-sp * sy, cp, -sp * cy};
  float tanHalf = std::tan(gFovY * 0.5f * PI / 180.0f);
  float aspect = 480.0f / 272.0f;
  int n = 0;
  for (int gy = 0; gy <= kSkyGridY; gy++) {
    float ny = 1.0f - 2.0f * (float)gy / (float)kSkyGridY;
    for (int gx = 0; gx <= kSkyGridX; gx++, n++) {
      float nx = 2.0f * (float)gx / (float)kSkyGridX - 1.0f;
      float dx = fwd[0] + right[0] * nx * tanHalf * aspect + up[0] * ny * tanHalf;
      float dy = fwd[1] + right[1] * nx * tanHalf * aspect + up[1] * ny * tanHalf;
      float dz = fwd[2] + right[2] * nx * tanHalf * aspect + up[2] * ny * tanHalf;
      float il = 1.0f / std::sqrt(dx * dx + dy * dy + dz * dz);
      dx *= il; dy *= il; dz *= il;
      float azim = std::atan2(dz, dx);
      float elev = std::asin(Clamp(dy, -1.0f, 1.0f));
      gSky[n].u = azim / TWO_PI;
      gSky[n].v = 0.5f - elev / PI;
      gSky[n].x = nx;
      gSky[n].y = ny;
      gSky[n].z = 0.0f;
    }
  }
}

} // namespace

// --------------------------------------------------------------------- API
void Scene2Init() {
  BuildMathTables();
  BuildGrid();
  BuildSparkTexture();
  UpdateClouds(0.0f);
  BuildSkyTexture(0.0f);
}

void Scene2UpdateCamera(double t) {
  float ft = (float)t;
  // same auto camera as src/scene2.cxx
  float a = ft * 0.16f;
  float radius = 42.0f + FastSin(ft * 0.042f) * 10.0f;
  gCamEye[0] = FastCos(a) * radius;
  gCamEye[2] = FastSin(a) * radius * 0.7f;
  gCamEye[1] = 7.5f + FastSin(ft * 0.086f) * 2.2f;
  float yawSun = std::atan2(kSunDir[0], kSunDir[2]);
  gCamYaw = yawSun + 0.14f * FastSin(ft * 0.026f) + 0.04f * FastSin(ft * 0.082f);
  gCamPitch = 0.030f + 0.022f * FastSin(ft * 0.034f);
}

void Scene2Build(double timeSeconds, int skyRefresh) {
  const float t = (float)timeSeconds;
  UpdateClouds(t);
  if (skyRefresh) BuildSkyTexture(t);
  BuildSkyGrid();

  const float eye[3] = {gCamEye[0], gCamEye[1], gCamEye[2]};
  const float sdx = kSunDir[0], sdy = kSunDir[1], sdz = kSunDir[2];
  const float sunXZLen = std::sqrt(kSunDir[0] * kSunDir[0] + kSunDir[2] * kSunDir[2]);
  const float sunXZWX = kSunDir[0] / sunXZLen, sunXZWZ = kSunDir[2] / sunXZLen;

  int vCount = 0;
  for (int i = 0; i <= kRings; i++) {
    float rr = gRingRadius[i];
    for (int s = 0; s <= kSegments; s++) {
      float ang = TWO_PI * (float)s / (float)kSegments;
      float sx = eye[0] + FastCos(ang) * rr;
      float sz = eye[2] + FastSin(ang) * rr;
      float h = WaveHeight(sx, sz, t);
      float wy = h;

      float gx, gz;
      WaveGradient(sx, sz, t, &gx, &gz);
      float dxz = std::sqrt((sx - eye[0]) * (sx - eye[0]) + (sz - eye[2]) * (sz - eye[2]));
      DetailGradient(sx, sz, t, dxz, &gx, &gz);
      float nl = 1.0f / std::sqrt(gx * gx + 1.0f + gz * gz);
      float nx = gx * nl, ny = nl, nz = gz * nl;

      float vx = eye[0] - sx, vy = eye[1] - wy, vz = eye[2] - sz;
      float vl = 1.0f / std::sqrt(vx * vx + vy * vy + vz * vz + 1e-6f);
      vx *= vl; vy *= vl; vz *= vl;
      float ndv = nx * vx + ny * vy + nz * vz;
      if (ndv < 0.0f) ndv = 0.0f;
      float rx = -vx + 2.0f * ndv * nx;
      float ry = -vy + 2.0f * ndv * ny;
      float rz = -vz + 2.0f * ndv * nz;
      float rl = 1.0f / std::sqrt(rx * rx + ry * ry + rz * rz + 1e-6f);
      rx *= rl; ry *= rl; rz *= rl;
      ry = std::fabs(ry) * 0.30f + 0.45f;
      rl = 1.0f / std::sqrt(rx * rx + ry * ry + rz * rz + 1e-6f);
      rx *= rl; ry *= rl; rz *= rl;

      float dirXZx = (sx - eye[0]) / (dxz + 1e-4f);
      float dirXZz = (sz - eye[2]) / (dxz + 1e-4f);
      float sunAlign = dirXZx * sunXZWX + dirXZz * sunXZWZ;
      if (sunAlign < 0.0f) sunAlign = 0.0f;

      float s2 = sunAlign * sunAlign;
      float blendR = s2 * s2 * s2; // sunAlign^6
      float rdx = rx, rdy = std::fabs(ry) * 1.8f + 0.62f, rdz = rz;
      float rdl = 1.0f / std::sqrt(rdx * rdx + rdy * rdy + rdz * rdz + 1e-6f);
      rdx *= rdl; rdy *= rdl; rdz *= rdl;
      float mrx = Mix(rdx, rx, blendR), mry = Mix(rdy, ry, blendR), mrz = Mix(rdz, rz, blendR);

      float shadow = CloudShadow(sx, wy, sz, eye[0], eye[1], eye[2], sdx, sdy, sdz);
      float sunDiffuse = nx * sdx + ny * sdy + nz * sdz;
      if (sunDiffuse < 0.0f) sunDiffuse = 0.0f;
      float warmGate = sunAlign * sunAlign * sunAlign; // ^3
      float offSun = 1.0f - SmoothStep(0.08f, 0.45f, sunAlign);

      float bodyR = kWaterColor[0] * 0.55f + kHorizonColor[0] * 0.03f;
      float bodyG = kWaterColor[1] * 0.55f + kHorizonColor[1] * 0.03f;
      float bodyB = kWaterColor[2] * 0.55f + kHorizonColor[2] * 0.03f;
      float d1 = 0.44f + 0.40f * sunDiffuse * warmGate * shadow + 0.12f * sunDiffuse * (0.35f + 0.65f * shadow);
      bodyR *= d1; bodyG *= d1; bodyB *= d1;
      float d2 = Mix(0.40f, 1.0f, warmGate * shadow + (1.0f - warmGate) * 0.25f * shadow);
      bodyR *= d2; bodyG *= d2; bodyB *= d2;
      float d3 = Mix(0.52f, 1.0f, 1.0f - offSun);
      bodyR *= d3; bodyG *= d3; bodyB *= d3;
      float sd3 = sunDiffuse * sunDiffuse * sunDiffuse;
      bodyR += 1.05f * sd3 * warmGate * shadow * 0.42f;
      bodyG += 0.42f * sd3 * warmGate * shadow * 0.42f;
      bodyB += 0.20f * sd3 * warmGate * shadow * 0.42f;

      float crest = SmoothStep(0.55f, 1.25f, h) * Mix(0.25f, 1.0f, shadow);
      float slopeFacing = sunDiffuse * warmGate + 0.15f;
      float chaos = Chaos(sx, sz);
      float foam = crest * slopeFacing * (0.35f + 0.65f * chaos);
      bodyR += 0.55f * foam * 0.55f;
      bodyG += 0.48f * foam * 0.55f;
      bodyB += 0.58f * foam * 0.55f;
      bodyR += 0.05f * sunAlign * warmGate * shadow * crest * 0.55f;
      bodyG += 0.18f * sunAlign * warmGate * shadow * crest * 0.55f;
      bodyB += 0.14f * sunAlign * warmGate * shadow * crest * 0.55f;

      float fresnel = 0.022f + 0.978f * std::pow(1.0f - ndv, 5.0f);
      fresnel = Clamp(fresnel * 1.25f + 0.045f, 0.0f, 0.92f);
      float horizonMix = SmoothStep(1500.0f, 1900.0f, dxz);
      float haze = 1.0f - FastExpNeg(dxz * 0.00075f);
      float h1 = horizonMix * 0.85f;
      float h2 = haze * (0.30f + 0.70f * haze);
      float bodyW = (1.0f - fresnel) * (1.0f - h1) * (1.0f - h2);
      float reflW = 1.0f - bodyW;

      float horizonW = Saturate(h1 + h2);
      float azim = std::atan2(mrz, mrx);
      float elev = std::asin(Clamp(mry, -1.0f, 1.0f));
      float uR = azim / TWO_PI;
      float vR = Mix(0.5f - elev / PI, 0.5f, horizonW);
      float uH = std::atan2(dirXZz, dirXZx) / TWO_PI;
      uR = Mix(uR, uH, horizonW);

      // pass 1 has no texture, so its vertices are colour-first (the GE reads
      // attributes in the flag order texture, colour, normal, position).
      BodyVertex &b = gBody[vCount];
      b.color = Color8888(bodyR * bodyW, bodyG * bodyW, bodyB * bodyW, 1.0f);
      b.x = sx; b.y = wy; b.z = sz;

      SeaVertex &rf = gRefl[vCount];
      rf.u = uR; rf.v = vR;
      rf.color = Color8888(reflW, reflW, reflW, 1.0f);
      rf.x = sx; rf.y = wy; rf.z = sz;

      // glitter: smooth path gate per vertex x sparkle texture per pixel
      float pathGate = std::pow(sunAlign, 10.0f) * 0.96f + 0.04f;
      float nearField = 1.0f - SmoothStep(260.0f, 950.0f, dxz);
      float gate = Mix(1.0f, 0.55f + 0.90f * chaos, nearField);
      float glintGain = (0.55f + 0.22f * pathGate) * (0.25f + (sdy > 0.0f ? sdy : 0.0f) * 1.2f);
      glintGain *= pathGate * shadow * gate * (0.35f + 0.65f * fresnel);
      SeaVertex &gl = gGlit[vCount];
      gl.u = sx * 0.045f + t * 0.011f;
      gl.v = sz * 0.045f - t * 0.007f;
      gl.color = Color8888(1.0f * glintGain, 0.64f * glintGain, 0.38f * glintGain, 1.0f);
      gl.x = sx; gl.y = wy; gl.z = sz;

      vCount++;
    }
  }

  // The GE DMAs these arrays straight out of RAM: flush the data cache once,
  // before any draw command that reads them is submitted.
  sceKernelDcacheWritebackAll();
}

void Scene2DrawSky() {  // The sky grid is in clip-space coordinates (-1..1), so it needs its own
  // orthographic projection; the caller restores the perspective for the sea.
  sceGuDisable(GU_DEPTH_TEST);
  sceGuDepthMask(GU_TRUE);
  sceGuDisable(GU_BLEND);
  sceGuEnable(GU_TEXTURE_2D);
  sceGuTexMode(GU_PSM_5551, 0, 0, GU_FALSE);
  sceGuTexImage(0, kSkyW, kSkyH, kSkyW, (void *)gSkyTex);
  sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
  sceGuTexFilter(GU_LINEAR, GU_LINEAR);
  sceGuTexWrap(GU_REPEAT, GU_CLAMP);
  sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT,
                 kSkyGridX * kSkyGridY * 6, gSkyIndex, gSky);
  sceGuDepthMask(GU_FALSE);
  sceGuEnable(GU_DEPTH_TEST);
}

void Scene2Draw() {
  sceGuEnable(GU_DEPTH_TEST);
  sceGuDepthMask(GU_TRUE); // the opaque body may write depth
  sceGuDisable(GU_CULL_FACE);

  // pass 1: body colour (opaque)
  sceGuDisable(GU_BLEND);
  sceGuDisable(GU_TEXTURE_2D);
  sceGuDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_INDEX_16BIT,
                 kSegments * kRings * 6, gIndex, gBody);

  sceGuDepthMask(GU_FALSE); // the additive passes must not occlude each other
  sceGuEnable(GU_BLEND);
  sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xFFFFFFFF); // additive
  sceGuEnable(GU_TEXTURE_2D);

  // pass 2: reflection and haze from the same sky texture
  sceGuTexMode(GU_PSM_5551, 0, 0, GU_FALSE);
  sceGuTexImage(0, kSkyW, kSkyH, kSkyW, (void *)gSkyTex);
  sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
  sceGuTexFilter(GU_LINEAR, GU_LINEAR);
  sceGuTexWrap(GU_REPEAT, GU_CLAMP);
  sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_INDEX_16BIT,
                 kSegments * kRings * 6, gIndex, gRefl);

  // pass 3: sun glitter
  sceGuTexImage(0, kSparkW, kSparkH, kSparkW, (void *)gSparkTex);
  sceGuTexFilter(GU_LINEAR, GU_LINEAR);
  sceGuTexWrap(GU_REPEAT, GU_REPEAT);
  sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_INDEX_16BIT,
                 kSegments * kRings * 6, gIndex, gGlit);

  sceGuDisable(GU_BLEND);
  sceGuDisable(GU_TEXTURE_2D);
}

void Scene2Camera(float outEye[3], float *yaw, float *pitch) {
  outEye[0] = gCamEye[0]; outEye[1] = gCamEye[1]; outEye[2] = gCamEye[2];
  *yaw = gCamYaw;
  *pitch = gCamPitch;
}

float Scene2FovY() { return gFovY; }

