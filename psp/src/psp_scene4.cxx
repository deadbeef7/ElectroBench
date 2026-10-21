// ElectroBench — PlayStation Portable port, SCENE 4 (power lines / "lain").
//
// Same rebuild-not-translate approach as psp_scene2.cxx: the GE has no
// shaders, so scene 4 becomes a fixed-function scene. What maps where:
//
//   * geometry (poles, arms, insulators, transformers, houses, walls, trees,
//     pylons, road) -> one indexed static vertex array, built once at startup
//   * cables -> GU_LINES: at 480x272 a 3 cm cable is sub-pixel, and the tube
//     tessellation the desktop scene uses (4 sides x 10-14 samples per span)
//     would cost more vertices than the whole rest of the scene
//   * lighting -> baked per vertex at startup with the object_frag.brdf terms
//     (ambient sky/ground mix, wrap diffuse, a sun-facing highlight), because
//     the sun only drifts 0.18 rad over the 45 s run
//   * depth haze -> the GE's own linear fog, which unlike a baked per-vertex
//     haze keeps working while the camera drives along the corridor
//   * sky -> a 128x64 equirect panorama generated on the CPU from the
//     atmosphere() integral in shaders/pole/sky_frag.glsl, drawn as a
//     ray-cast grid; the 2.9-degree sun disc is a separate additive sprite,
//     because a grid that could resolve the disc would cost a full-screen
//     tessellation for every frame
//
// The camera, road layout, pole positions and palette are the same numbers as
// src/scene4.cxx, so the two scenes frame the same shot.
#include "psp_scene4.hxx"

#include <pspgu.h>
#include <pspkernel.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace {

// ---------------------------------------------------------------- constants
constexpr float PI = 3.14159265358979f;
constexpr float TWO_PI = 2.0f * PI;

constexpr float kGroundY = 0.0f;
constexpr float kRoadX = 2.6f;        // road centre (matches src/scene4.cxx)
constexpr float kRoadHalf = 2.7f;     // 5.4 m carriageway
constexpr float kRoadY = 0.030f;
constexpr float kPaintY = 0.050f;
constexpr float kShadowY = 0.075f;
constexpr float kKerbTopY = 0.190f;

constexpr int kScreenW = 480;
constexpr int kScreenH = 272;
constexpr float kFovY = 52.0f;        // src/scene4.cxx's projection

// ---------------------------------------------------------------- buffers
// The whole scene lands in one indexed array so the frame is two draw calls
// (opaque ground group, opaque detail group) plus the line pass. Index 16-bit
// is enough for 60k vertices and halves the index traffic the GE fetches.
struct SolidVertex { unsigned int color; float x, y, z; };
struct LineVertex { unsigned int color; float x, y, z; };

constexpr int kMaxSolidVerts = 60000;
constexpr int kMaxSolidIndices = 150000;
constexpr int kMaxLineVerts = 12000;

SolidVertex gSolid[kMaxSolidVerts] __attribute__((aligned(16)));
unsigned short gSolidIdx[kMaxSolidIndices] __attribute__((aligned(16)));
int gSolidVerts = 0;
int gSolidIdxCount = 0;
int gGroundIdxCount = 0;   // index count of the ground/road part of the array

LineVertex gLine[kMaxLineVerts] __attribute__((aligned(16)));
int gLineVerts = 0;

// Ground shadows live in their own small array: they are blended onto the
// ground after it is drawn and must not write depth.
SolidVertex gShadow[4000] __attribute__((aligned(16)));
unsigned short gShadowIdx[6000] __attribute__((aligned(16)));
int gShadowVerts = 0, gShadowIdxCount = 0;

// --------------------------------------------------------------- materials
enum Material {
  kMatPaint, kMatWood, kMatGround, kMatRoad, kMatLine, kMatCable,
  kMatCeramic, kMatMetal, kMatWall, kMatRoof, kMatGlass, kMatLeaf,
  kMatGlow, kMatSteel, kMatKerb, kMatConcrete,
};

// Palette (identical to src/scene4.cxx).
const float kConcreteGrey[3] = {0.212f, 0.216f, 0.218f};
const float kWoodDark[3] = {0.165f, 0.115f, 0.085f};
const float kWoodOld[3] = {0.230f, 0.180f, 0.140f};
const float kCeramic[3] = {0.780f, 0.760f, 0.700f};
const float kCable[3] = {0.055f, 0.050f, 0.055f};
const float kCableOld[3] = {0.085f, 0.075f, 0.070f};
const float kMetal[3] = {0.190f, 0.195f, 0.200f};
const float kSteelGalv[3] = {0.252f, 0.261f, 0.273f};
const float kSteelArm[3] = {0.265f, 0.272f, 0.282f};
const float kSteelPlate[3] = {0.560f, 0.545f, 0.505f};
const float kGravel[3] = {0.330f, 0.272f, 0.205f};

// Small one-off palette entries from src/scene4.cxx, named instead of written
// as compound literals at the call sites.
const float kPlinthDirt[3] = {0.300f, 0.240f, 0.180f};
const float kPlateInk[3] = {0.075f, 0.070f, 0.065f};
const float kAnchorBlock[3] = {0.230f, 0.222f, 0.208f};
const float kPylonFoot[3] = {0.255f, 0.250f, 0.238f};
const float kHouseBase[3] = {0.150f, 0.142f, 0.132f};
const float kHedge[3] = {0.048f, 0.078f, 0.040f};
const float kLampArm[3] = {0.140f, 0.138f, 0.135f};
const float kLampHead[3] = {0.150f, 0.148f, 0.145f};
const float kLampLens[3] = {0.880f, 0.680f, 0.420f};
const float kVendBody[3] = {0.185f, 0.150f, 0.120f};
const float kVendFront[3] = {0.880f, 0.930f, 1.000f};
const float kVendTray[3] = {0.300f, 0.260f, 0.220f};
const float kHillTint[3] = {0.215f, 0.185f, 0.175f};
const float kKerbTint[3] = {0.245f, 0.238f, 0.225f};

const float kWallTints[5][3] = {
    {0.235f, 0.208f, 0.178f}, {0.285f, 0.256f, 0.214f},
    {0.185f, 0.170f, 0.158f}, {0.330f, 0.300f, 0.246f},
    {0.160f, 0.152f, 0.148f}};
const float kRoofTints[3][3] = {
    {0.095f, 0.084f, 0.082f}, {0.215f, 0.112f, 0.066f},
    {0.125f, 0.118f, 0.115f}};

// ---------------------------------------------------------------- fast math
inline float Clamp(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float Saturate(float v) { return Clamp(v, 0.0f, 1.0f); }
inline float SmoothStep(float a, float b, float x) {
  float t = Saturate((x - a) / (b - a));
  return t * t * (3.0f - 2.0f * t);
}
inline float Mix(float a, float b, float t) { return a + (b - a) * t; }

inline unsigned int Color8888(float r, float g, float b, float a) {
  int ir = (int)(Saturate(r) * 255.0f + 0.5f);
  int ig = (int)(Saturate(g) * 255.0f + 0.5f);
  int ib = (int)(Saturate(b) * 255.0f + 0.5f);
  int ia = (int)(Saturate(a) * 255.0f + 0.5f);
  return (unsigned int)(ir | (ig << 8) | (ib << 16) | (ia << 24));
}
inline unsigned short Pack5551(float r, float g, float b) {
  int ir = (int)(Saturate(r) * 31.0f + 0.5f);
  int ig = (int)(Saturate(g) * 31.0f + 0.5f);
  int ib = (int)(Saturate(b) * 31.0f + 0.5f);
  return (unsigned short)(ir | (ig << 5) | (ib << 10) | 0x8000u);
}

// ------------------------------------------------------------------ shading
// object_frag.glsl's directional term, baked. The sun is the one the scene
// starts with: SunDirNow() only swings its azimuth by 0.004 rad/s and clamps
// its elevation at 0.15, so across one 45 s run the difference is under the
// 5-bit quantisation of a 5650 target.
float gBakeSun[3] = {0.0f, 0.0f, 0.0f};

unsigned int Shade(float nx, float ny, float nz, const float c[3], Material m,
                   float alpha = 1.0f) {
  if (m == kMatGlow) {   // street-lamp lenses, lit windows, vending fronts
    return Color8888(c[0] * 1.05f, c[1] * 1.05f, c[2] * 1.05f, alpha);
  }

  const float up = ny * 0.5f + 0.5f;
  const float ambR = Mix(0.125f, 0.395f, up);
  const float ambG = Mix(0.086f, 0.340f, up);
  const float ambB = Mix(0.064f, 0.360f, up);

  float wrap = 0.0f;
  if (m == kMatCable) wrap = 0.35f;
  else if (m == kMatSteel) wrap = 0.13f;

  float ndl = nx * gBakeSun[0] + ny * gBakeSun[1] + nz * gBakeSun[2];
  float diff = Saturate((ndl + wrap) / (1.0f + wrap));

  const float sunR = 1.00f, sunG = 0.66f, sunB = 0.34f;
  float r = c[0] * (ambR + sunR * diff * 1.15f);
  float g = c[1] * (ambG + sunG * diff * 1.15f);
  float b = c[2] * (ambB + sunB * diff * 1.15f);

  // A glint on the hard materials. The desktop BRDF needs the per-pixel view
  // vector; with a baked vertex colour only the sun angle is left, so this is
  // the top-face highlight that carries the galvanised steel silhouette.
  if (m == kMatSteel || m == kMatMetal || m == kMatCeramic ||
      m == kMatKerb || m == kMatRoof || m == kMatGlass) {
    float sharp = (m == kMatGlass) ? 48.0f : (m == kMatCeramic ? 26.0f : 20.0f);
    float glint = std::pow(Saturate(ndl), sharp) * 0.24f;
    r += glint * sunR;
    g += glint * sunG;
    b += glint * sunB;
  }
  if (m == kMatGlass && ny > 0.0f) {   // window glass mirrors the dusk sky
    r += 0.030f;
    g += 0.034f;
    b += 0.055f;
  }

  // The wet-surface reflection. object_frag.glsl adds skyRefl * wetF to the
  // road and kerb, and that grazing reflection is what makes the carriageway
  // read as warm grey rather than black; with the view vector unavailable in a
  // baked vertex colour, a constant warm term per surface carries the same
  // lift. The far field is left to the GE fog.
  if (m == kMatRoad) {
    r += 0.30f; g += 0.24f; b += 0.18f;
  } else if (m == kMatGround) {
    r += 0.23f; g += 0.19f; b += 0.14f;
  } else if (m == kMatKerb) {
    r += 0.19f; g += 0.16f; b += 0.12f;
  } else if (m == kMatLine) {
    r += 0.16f; g += 0.14f; b += 0.10f;
  }
  return Color8888(r, g, b, alpha);
}

// ---------------------------------------------------------------- geometry
inline void AddVert(float x, float y, float z, unsigned int color) {
  if (gSolidVerts >= kMaxSolidVerts) return;
  SolidVertex &v = gSolid[gSolidVerts++];
  v.color = color;
  v.x = x; v.y = y; v.z = z;
}

inline void AddIdx(int a, int b, int c) {
  if (gSolidIdxCount + 3 > kMaxSolidIndices) return;
  gSolidIdx[gSolidIdxCount++] = (unsigned short)a;
  gSolidIdx[gSolidIdxCount++] = (unsigned short)b;
  gSolidIdx[gSolidIdxCount++] = (unsigned short)c;
}

// Quad in the order the caller gives it; winding does not matter because the
// port runs with culling disabled (the GE's winding for the flipped-Y PSP
// viewport is the opposite of GL's).
void AddQuad(const float p[4][3], float nx, float ny, float nz,
             const float c[3], Material m) {
  if (gSolidVerts + 4 > kMaxSolidVerts ||
      gSolidIdxCount + 6 > kMaxSolidIndices) return;
  int base = gSolidVerts;
  unsigned int col = Shade(nx, ny, nz, c, m);
  for (int i = 0; i < 4; i++) AddVert(p[i][0], p[i][1], p[i][2], col);
  AddIdx(base, base + 1, base + 2);
  AddIdx(base, base + 2, base + 3);
}

void AddBox(const float cx, const float cy, const float cz,
            const float hx, const float hy, const float hz,
            const float c[3], Material m) {
  float x0 = cx - hx, x1 = cx + hx;
  float y0 = cy - hy, y1 = cy + hy;
  float z0 = cz - hz, z1 = cz + hz;
  // +Z, -Z, +X, -X, +Y, -Y. Each face gets its own shaded colour.
  float p0[4][3] = {{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}};
  AddQuad(p0, 0, 0, 1, c, m);
  float p1[4][3] = {{x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}};
  AddQuad(p1, 0, 0, -1, c, m);
  float p2[4][3] = {{x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}};
  AddQuad(p2, 1, 0, 0, c, m);
  float p3[4][3] = {{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}};
  AddQuad(p3, -1, 0, 0, c, m);
  float p4[4][3] = {{x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}};
  AddQuad(p4, 0, 1, 0, c, m);
  float p5[4][3] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}};
  AddQuad(p5, 0, -1, 0, c, m);
}

struct V3 { float x, y, z; };
inline V3 VAdd(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 VSub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 VScale(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float VDot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 VCross(V3 a, V3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float VLen(V3 a) { return std::sqrt(VDot(a, a)); }
inline V3 VNorm(V3 a) {
  float l = VLen(a);
  if (l < 1e-6f) return {0.0f, 1.0f, 0.0f};
  return VScale(a, 1.0f / l);
}

// Side wall of a (possibly tapered) tube. Caps are not emitted: every cylinder
// in this scene is seen from the side or above its open top, and the caps cost
// as much as a third of the pole geometry. `segs` sides means `segs` quads.
void AddCylinder(V3 base, V3 top, float rBase, float rTop, int segs,
                 const float c[3], Material m) {
  if (gSolidVerts + (segs + 1) * 2 > kMaxSolidVerts ||
      gSolidIdxCount + segs * 6 > kMaxSolidIndices) return;
  V3 axis = VNorm(VSub(top, base));
  V3 helper = (std::fabs(axis.y) > 0.9f) ? V3{1.0f, 0.0f, 0.0f}
                                        : V3{0.0f, 1.0f, 0.0f};
  V3 s = VNorm(VCross(axis, helper));
  V3 u = VCross(axis, s);
  int start = gSolidVerts;
  for (int i = 0; i <= segs; i++) {
    float a = (float)i / (float)segs * TWO_PI;
    float ca = std::cos(a), sa = std::sin(a);
    V3 dir{ s.x * ca + u.x * sa, s.y * ca + u.y * sa, s.z * ca + u.z * sa };
    unsigned int col = Shade(dir.x, dir.y, dir.z, c, m);
    V3 b = VAdd(base, VScale(dir, rBase));
    V3 t = VAdd(top, VScale(dir, rTop));
    AddVert(b.x, b.y, b.z, col);
    AddVert(t.x, t.y, t.z, col);
  }
  for (int i = 0; i < segs; i++) {
    int a = start + i * 2;
    AddIdx(a, a + 1, a + 2);
    AddIdx(a + 1, a + 3, a + 2);
  }
}

inline void AddLineSeg(V3 a, V3 b, unsigned int col) {
  if (gLineVerts + 2 > kMaxLineVerts) return;
  gLine[gLineVerts++] = {col, a.x, a.y, a.z};
  gLine[gLineVerts++] = {col, b.x, b.y, b.z};
}

V3 WirePoint(V3 a, V3 b, float sag, float t) {
  V3 delta = VSub(b, a);
  float len = VLen(delta);
  if (len < 1e-3f) return a;
  V3 dir = VScale(delta, 1.0f / len);
  // Same perpendicular choice as Vec3PerpTo() in src/scene4.cxx.
  V3 ref = (std::fabs(dir.y) < 0.9f) ? V3{0.0f, 1.0f, 0.0f} : V3{1.0f, 0.0f, 0.0f};
  V3 side = VNorm(VCross(dir, ref));
  V3 up = VNorm(VCross(side, dir));
  float halfSpan = len * 0.5f;
  V3 mid = VAdd(a, VScale(delta, 0.5f));
  float x = (t - 0.5f) * len;
  float y = sag * (1.0f - 4.0f * (t - 0.5f) * (t - 0.5f));
  float tail = sag * 0.06f * (std::cosh(x / (halfSpan * 0.72f)) - 1.0f)
             / (float)std::cosh(halfSpan / (halfSpan * 0.72f));
  return VAdd(VAdd(mid, VScale(dir, x)), VScale(up, -y - tail));
}

// A cable as a polyline. 4-7 samples per span is all a 1-pixel line needs; the
// sag curve is C1 so the polyline reads as a curve.
void AddWire(V3 a, V3 b, float sag, int samples, const float c[3]) {
  unsigned int col = Shade(0.0f, 1.0f, 0.0f, c, kMatCable);
  V3 prev = WirePoint(a, b, sag, 0.0f);
  for (int i = 1; i <= samples; i++) {
    V3 cur = WirePoint(a, b, sag, (float)i / (float)samples);
    AddLineSeg(prev, cur, col);
    prev = cur;
  }
}

// ------------------------------------------------------------------- poles
struct PoleSpec {
  float x, z, height;
  bool transformer, serviceSpool;
  float leanX, leanZ;
};

PoleSpec gLineA[15];
PoleSpec gLineB[6];

inline V3 PoleAxisAt(const PoleSpec &p, float h) {
  float f = h / p.height;
  return {p.x + p.leanX * f, h, p.z + p.leanZ * f};
}
constexpr float kShaftRBase = 0.132f;
constexpr float kShaftRTop = 0.086f;
inline float TrunkRadius(const PoleSpec &p, float h) {
  return kShaftRBase + (kShaftRTop - kShaftRBase) * (h / p.height);
}
inline V3 ArmInsulatorTop(const PoleSpec &p, float off) {
  V3 a = PoleAxisAt(p, p.height - 0.25f);
  return {a.x + off, p.height - 0.25f, a.z};
}
inline V3 PoleTopInsulatorTop(const PoleSpec &p) {
  V3 a = PoleAxisAt(p, p.height + 0.24f);
  return {a.x, p.height + 0.24f, a.z};
}
inline V3 Arm2InsulatorTop(const PoleSpec &p, float off) {
  V3 a = PoleAxisAt(p, p.height - 0.87f);
  return {a.x + off, p.height - 0.87f, a.z};
}
inline V3 TelecomBracketTop(const PoleSpec &p, float off) {
  float telY = p.height - 0.55f - 1.30f;
  V3 a = PoleAxisAt(p, telY);
  return {a.x + off, telY + 0.15f, a.z};
}

void AddInsulator(V3 ib) {
  AddCylinder(ib, VAdd(ib, {0.0f, 0.055f, 0.0f}), 0.044f, 0.040f, 5,
              kMetal, kMatMetal);
  AddCylinder(VAdd(ib, {0.0f, 0.055f, 0.0f}),
              VAdd(ib, {0.0f, 0.175f, 0.0f}), 0.050f, 0.061f, 6,
              kCeramic, kMatCeramic);
}

void AddGuyWire(const PoleSpec &p, float side, float zSign) {
  float hAtt = p.height - 0.95f;
  V3 att = PoleAxisAt(p, hAtt);
  float r = TrunkRadius(p, hAtt);
  att.x += side * (r + 0.11f);
  AddBox(att.x - side * 0.06f, att.y, att.z, 0.05f, 0.13f, 0.09f,
         kSteelArm, kMatSteel);
  V3 mid{att.x + side * 0.13f, att.y - 0.44f, att.z + zSign * 0.28f};
  AddCylinder(att, mid, 0.026f, 0.023f, 4, kSteelGalv, kMatSteel);
  V3 anc{p.x + side * 0.62f, 0.34f, p.z + zSign * 3.40f};
  AddWire(mid, anc, 0.03f, 3, kSteelGalv);
  AddCylinder({anc.x - side * 0.30f, -0.10f, anc.z}, anc, 0.042f, 0.032f, 5,
              kSteelGalv, kMatSteel);
  AddBox(anc.x - side * 0.13f, 0.14f, anc.z, 0.36f, 0.14f, 0.36f,
         kAnchorBlock, kMatPaint);
}

void AddPole(const PoleSpec &p, bool concrete) {
  V3 base = PoleAxisAt(p, kGroundY);
  V3 top = PoleAxisAt(p, p.height);
  const float side = p.x < kRoadX ? 1.0f : -1.0f;

  AddCylinder(base, top, kShaftRBase, kShaftRTop, 8,
              concrete ? kConcreteGrey : kSteelGalv,
              concrete ? kMatConcrete : kMatSteel);
  AddCylinder({base.x, kGroundY - 0.02f, base.z}, {base.x, 0.10f, base.z},
              0.52f, 0.34f, 8, kPlinthDirt, kMatGround);

  const float armY = p.height - 0.55f;
  V3 armC = PoleAxisAt(p, armY);
  AddBox(armC.x, armY, armC.z, 1.25f, 0.075f, 0.013f, kSteelArm, kMatSteel);
  AddBox(armC.x, armY + 0.072f, armC.z, 1.25f, 0.013f, 0.058f, kSteelArm,
         kMatSteel);
  AddBox(armC.x, armY - 0.072f, armC.z, 1.25f, 0.013f, 0.058f, kSteelArm,
         kMatSteel);

  const float arm2Y = armY - 0.62f;
  V3 arm2C = PoleAxisAt(p, arm2Y);
  AddBox(arm2C.x, arm2Y, arm2C.z, 0.85f, 0.075f, 0.013f, kSteelArm, kMatSteel);
  AddBox(arm2C.x, arm2Y + 0.072f, arm2C.z, 0.85f, 0.013f, 0.052f, kSteelArm,
         kMatSteel);
  AddBox(arm2C.x, arm2Y - 0.072f, arm2C.z, 0.85f, 0.013f, 0.052f, kSteelArm,
         kMatSteel);

  V3 brT = PoleAxisAt(p, armY - 0.05f);
  V3 brB = PoleAxisAt(p, armY - 0.57f);
  AddCylinder({brT.x - 0.34f, brT.y, brT.z + 0.05f},
              {brB.x - 0.94f, brB.y, brB.z + 0.05f}, 0.028f, 0.028f, 4,
              kSteelArm, kMatSteel);
  AddCylinder({brT.x + 0.34f, brT.y, brT.z + 0.05f},
              {brB.x + 0.94f, brB.y, brB.z + 0.05f}, 0.028f, 0.028f, 4,
              kSteelArm, kMatSteel);

  for (float by : {armY + 0.075f, arm2Y + 0.075f}) {
    V3 ba = PoleAxisAt(p, by);
    float br2 = TrunkRadius(p, by);
    AddCylinder({ba.x, by - 0.030f, ba.z}, {ba.x, by + 0.030f, ba.z},
                br2 + 0.035f, br2 + 0.035f, 6, kSteelArm, kMatSteel);
  }

  for (float off : {-1.05f, 0.0f, 1.05f})
    AddInsulator({armC.x + off, armY + 0.087f, armC.z});
  for (float off : {-0.55f, 0.55f})
    AddInsulator({arm2C.x + off, arm2Y + 0.087f, arm2C.z});

  AddCylinder(top, VAdd(top, {0.0f, 0.18f, 0.0f}), 0.05f, 0.058f, 6,
              kCeramic, kMatCeramic);
  AddCylinder(VAdd(top, {0.0f, 0.18f, 0.0f}), VAdd(top, {0.0f, 0.24f, 0.0f}),
              0.058f, 0.038f, 6, kCeramic, kMatCeramic);

  // Step bolts up the road face, then the number plate.
  int bolt = 0;
  for (float h = 3.15f; h <= 5.16f; h += 0.50f, bolt++) {
    float s = (bolt & 1) ? -side : side;
    V3 a = PoleAxisAt(p, h);
    float r = TrunkRadius(p, h);
    AddCylinder({a.x + s * (r - 0.02f), h, a.z}, {a.x + s * (r + 0.10f), h, a.z},
                0.024f, 0.019f, 4, kSteelArm, kMatSteel);
  }
  {
    V3 pa = PoleAxisAt(p, 2.35f);
    float pr = TrunkRadius(p, 2.35f);
    AddBox(pa.x + pr + 0.042f, 2.62f, pa.z, 0.012f, 0.085f, 0.062f,
           kSteelPlate, kMatMetal);
    AddBox(pa.x + pr + 0.058f, 2.62f, pa.z, 0.006f, 0.020f, 0.050f,
           kPlateInk, kMatMetal);
  }

  // Earth wire clipped down the shaft: a polyline, not a tube.
  {
    const float eside = -side;
    V3 prev = PoleAxisAt(p, 0.32f);
    prev.x += eside * (TrunkRadius(p, 0.32f) + 0.024f);
    AddWire(prev, {base.x + eside * 0.19f, 0.14f, base.z}, 0.01f, 3,
            kSteelGalv);
    for (float h = 1.4f; h <= 5.01f; h += 1.2f) {
      V3 cur = PoleAxisAt(p, h);
      cur.x += eside * (TrunkRadius(p, h) + 0.024f);
      AddWire(prev, cur, 0.012f, 2, kSteelGalv);
      prev = cur;
    }
  }

  if ((((int)(p.z * 0.4f)) % 3) == 0) {
    AddGuyWire(p, side, 1.0f);
    if ((((int)(p.z * 0.8f)) % 5) == 0) AddGuyWire(p, side, -1.0f);
  }

  if (p.serviceSpool) {
    V3 sa = PoleAxisAt(p, 5.4f);
    float sr = TrunkRadius(p, 5.4f);
    AddCylinder({sa.x - sr, 5.4f, sa.z}, {sa.x - sr - 0.10f, 5.4f, sa.z},
                0.035f, 0.035f, 4, kMetal, kMatMetal);
    AddCylinder({sa.x - sr - 0.10f, 5.4f, sa.z},
                {sa.x - sr - 0.24f, 5.4f, sa.z}, 0.135f, 0.135f, 8,
                kSteelArm, kMatSteel);
  }

  const float telY = armY - 1.30f;
  V3 telC = PoleAxisAt(p, telY);
  AddBox(telC.x, telY, telC.z, 0.95f, 0.05f, 0.014f, kSteelArm, kMatSteel);
  AddBox(telC.x, telY + 0.045f, telC.z, 0.95f, 0.012f, 0.048f, kSteelArm,
         kMatSteel);
  for (float off : {-0.70f, 0.0f, 0.70f})
    AddCylinder({telC.x + off, telY + 0.05f, telC.z},
                {telC.x + off, telY + 0.15f, telC.z}, 0.038f, 0.032f, 4,
                kMetal, kMatMetal);

  if (p.transformer) {
    V3 tc{armC.x + 0.62f, armY - 1.42f, armC.z + 0.44f};
    for (float bz : {-0.13f, 0.13f})
      AddBox(tc.x + bz, armY - 0.44f, (tc.z + armC.z) * 0.5f, 0.045f, 0.47f,
             0.24f, kSteelArm, kMatSteel);
    AddCylinder({tc.x - 0.10f, tc.y - 0.55f, tc.z},
                {tc.x + 0.10f, tc.y + 0.55f, tc.z}, 0.34f, 0.34f, 8,
                kMetal, kMatMetal);
    for (int fi = 0; fi < 3; fi++) {
      float fy = tc.y - 0.40f + 0.40f * fi;
      AddBox(tc.x, fy, tc.z + 0.34f, 0.30f, 0.028f, 0.085f, kSteelArm,
             kMatSteel);
      AddBox(tc.x, fy, tc.z - 0.34f, 0.30f, 0.028f, 0.085f, kSteelArm,
             kMatSteel);
    }
    AddBox(tc.x, tc.y + 0.30f, tc.z, 0.40f, 0.16f, 0.16f, kMetal, kMatMetal);
    for (float bz : {-0.12f, 0.12f}) {
      V3 bt{tc.x, tc.y + 0.46f, tc.z + bz};
      AddCylinder(bt, VAdd(bt, {0.0f, 0.16f, 0.0f}), 0.045f, 0.038f, 4,
                  kCeramic, kMatCeramic);
      AddWire(VAdd(bt, {0.0f, 0.17f, 0.0f}),
              ArmInsulatorTop(p, bz < 0.0f ? 0.0f : 1.05f), 0.05f, 2, kCable);
    }
  }
}

// ------------------------------------------------------------------- pylons
void AddPylon(float x, float z, float height) {
  const float steel[3] = {0.170f, 0.172f, 0.180f};
  const float galv[3] = {0.215f, 0.220f, 0.232f};
  const float footHalf = height * 0.175f;
  const float kneeY = height * 0.34f;
  const float kneeHalf = footHalf * 0.46f;
  const float topHalf = height * 0.048f;

  for (int c = 0; c < 4; c++) {
    float sx = (c == 0 || c == 3) ? -1.0f : 1.0f;
    float sz = (c < 2) ? -1.0f : 1.0f;
    AddBox(x + sx * footHalf, 0.16f, z + sz * footHalf, 0.78f, 0.16f, 0.78f,
           kPylonFoot, kMatConcrete);
    AddCylinder({x + sx * footHalf, 0.12f, z + sz * footHalf},
                {x + sx * kneeHalf, kneeY, z + sz * kneeHalf}, 0.195f, 0.145f,
                4, steel, kMatSteel);
    AddCylinder({x + sx * kneeHalf, kneeY, z + sz * kneeHalf},
                {x + sx * topHalf, height, z + sz * topHalf}, 0.145f, 0.095f,
                4, steel, kMatSteel);
  }
  for (float t = 0.0f; t < 0.94f; t += 0.235f) {
    float h = kneeY * (t + 0.118f);
    float hb = footHalf + (kneeHalf - footHalf) * (t + 0.118f);
    for (int s = 0; s < 4; s++) {
      int n = (s + 1) & 3;
      float a0x = (s == 0 || s == 3) ? -hb : hb;
      float a0z = (s < 2) ? -hb : hb;
      float a1x = (n == 0 || n == 3) ? -hb : hb;
      float a1z = (n < 2) ? -hb : hb;
      AddCylinder({x + a0x, h, z + a0z},
                  {x + a1x, h + kneeY * 0.235f, z + a1z}, 0.085f, 0.085f, 3,
                  steel, kMatSteel);
    }
  }
  const float upperY = height * 0.735f, lowerY = height * 0.545f;
  for (float y : {upperY, lowerY}) {
    float halfLen = (y == upperY) ? height * 0.345f : height * 0.265f;
    AddBox(x, y + 0.30f, z, halfLen, 0.30f, 0.34f, steel, kMatSteel);
    AddBox(x, y - 0.26f, z, halfLen, 0.26f, 0.30f, steel, kMatSteel);
  }
  const float shed[3] = {0.300f, 0.288f, 0.262f};
  for (float f : {0.94f, 0.66f, 0.38f}) {
    float ix = x + height * 0.345f * f;
    AddBox(ix, upperY + 0.15f, z, 0.24f, 0.19f, 0.24f, galv, kMatSteel);
    AddCylinder({ix, upperY - 0.05f, z}, {ix, upperY - 1.60f, z}, 0.055f,
                0.046f, 4, steel, kMatSteel);
    for (int i = 0; i < 3; i++) {
      float t = (float)(i + 1) / 4.0f;
      float yy = upperY - 0.05f - 1.55f * t;
      float rr = 0.150f - 0.052f * t;
      AddCylinder({ix, yy - 0.042f, z}, {ix, yy + 0.042f, z}, rr, rr, 5,
                  shed, kMatCeramic);
    }
  }
  for (float f : {0.90f, 0.58f}) {
    float ix = x - height * 0.265f * f;
    AddBox(ix, lowerY + 0.15f, z, 0.24f, 0.19f, 0.24f, galv, kMatSteel);
    AddCylinder({ix, lowerY - 0.05f, z}, {ix, lowerY - 1.20f, z}, 0.050f,
                0.042f, 4, steel, kMatSteel);
    for (int i = 0; i < 2; i++) {
      float t = (float)(i + 1) / 3.0f;
      float yy = lowerY - 0.05f - 1.15f * t;
      float rr = 0.140f - 0.050f * t;
      AddCylinder({ix, yy - 0.040f, z}, {ix, yy + 0.040f, z}, rr, rr, 5,
                  shed, kMatCeramic);
    }
  }
}

// ------------------------------------------------------------------ houses
void AddRoofQuad(const float p[4][3], const float normal[3], const float c[3]) {
  AddQuad(p, normal[0], normal[1], normal[2], c, kMatRoof);
}

void AddHouse(float x, float z, float w, float d, float h, float face) {
  int pick = (int)std::fmod(std::fabs(std::sin(x * 12.9898f + z * 78.233f)) *
                                43758.5453f,
                            5.0f);
  const float *wall = kWallTints[pick];
  const float *roof = kRoofTints[pick % 3];
  const float streetX = face * w * 0.5f;

  AddBox(x, 0.09f, z, w * 0.5f + 0.07f, 0.09f, d * 0.5f + 0.07f,
         kHouseBase, kMatPaint);
  AddBox(x, 0.10f + h * 0.5f, z, w * 0.5f, h * 0.5f, d * 0.5f, wall,
         kMatWall);

  const float ov = 0.42f;
  const float rh = 0.55f + 0.06f * (float)(pick % 3);
  const float yEave = 0.10f + h;
  const float yRidge = yEave + rh;
  const float x0 = x - w * 0.5f - ov, x1 = x + w * 0.5f + ov;
  const float zA = z - d * 0.5f - ov, zB = z + d * 0.5f + ov;
  float r0[3] = {x0, yRidge, z}, r1[3] = {x1, yRidge, z};
  float eA[3] = {x0, yEave, zA}, eB[3] = {x1, yEave, zA};
  float eC[3] = {x1, yEave, zB}, eD[3] = {x0, yEave, zB};
  float nA[3] = {0.0f, rh, zB - z}, nB[3] = {0.0f, rh, -(zB - z)};
  {
    float q[4][3];
    std::memcpy(q[0], r0, sizeof(q[0]));
    std::memcpy(q[1], r1, sizeof(q[1]));
    std::memcpy(q[2], eB, sizeof(q[2]));
    std::memcpy(q[3], eA, sizeof(q[3]));
    AddRoofQuad(q, nA, roof);
    std::memcpy(q[0], r1, sizeof(q[0]));
    std::memcpy(q[1], r0, sizeof(q[1]));
    std::memcpy(q[2], eD, sizeof(q[2]));
    std::memcpy(q[3], eC, sizeof(q[3]));
    AddRoofQuad(q, nB, roof);
    float under[4][3];
    std::memcpy(under[0], eA, sizeof(under[0]));
    std::memcpy(under[1], eB, sizeof(under[1]));
    std::memcpy(under[2], eC, sizeof(under[2]));
    std::memcpy(under[3], eD, sizeof(under[3]));
    const float eave[3] = {roof[0] + 0.10f, roof[1] + 0.10f,
                           roof[2] + 0.10f};
    AddQuad(under, 0.0f, -1.0f, 0.0f, eave, kMatRoof);
  }

  // Windows: three bays, alternating warm/lit. A frame box with the glass
  // inset is enough at this viewing distance.
  const float frame[3] = {0.320f, 0.300f, 0.270f};
  const float glass[3] = {0.060f, 0.075f, 0.095f};
  const float litCol[3] = {0.960f, 0.710f, 0.400f};
  for (int i = 0; i < 3; i++) {
    float wx = x + streetX + face * 0.03f;
    float wz = z + (float)(i - 1) * (d * 0.30f);
    float wy = 0.10f + h * 0.60f;
    AddBox(wx + face * 0.02f, wy, wz, 0.05f, 0.26f, 0.34f, frame, kMatPaint);
    bool lit = ((i + pick) % 3) == 0;
    AddBox(wx + face * 0.06f, wy, wz, 0.02f, 0.21f, 0.29f,
           lit ? litCol : glass, lit ? kMatGlow : kMatGlass);
  }
}

void AddPropertyLine(float x, float z0, float z1, float mirror) {
  const float wall[3] = {0.150f, 0.145f, 0.135f};
  const float cap[3] = {0.190f, 0.176f, 0.158f};
  float z = z0;
  int i = 0;
  while (z < z1) {
    float seg = 7.0f + 3.0f * (float)((i * 7) % 4) / 4.0f;
    float h = 1.50f + 0.26f * (float)((i * 5) % 3) / 3.0f;
    float cz = z + seg * 0.5f;
    AddBox(x, h * 0.5f, cz, 0.11f, h * 0.5f, seg * 0.5f, wall, kMatPaint);
    AddBox(x, h + 0.045f, cz, 0.17f, 0.045f, seg * 0.5f, cap, kMatRoof);
    if ((i % 3) == 1)
      AddBox(x, 1.05f, z, 0.17f, 1.05f, 0.17f, cap, kMatPaint);
    if ((i % 2) == 0) {
      float hx = x + mirror * (0.95f + 0.55f * (float)((i * 3) % 3) / 3.0f);
      AddBox(hx, 0.58f, cz, 0.46f, 0.58f, seg * 0.40f, kHedge,
             kMatLeaf);
    }
    z += seg + 0.35f;
    i++;
  }
}

void AddStreetLight(const PoleSpec &p, float side) {
  float armY = p.height - 2.35f;
  V3 a = PoleAxisAt(p, armY);
  float tipx = a.x + side * 2.15f;
  AddCylinder({a.x, armY, a.z}, {tipx, armY + 0.30f, a.z}, 0.055f, 0.045f, 4,
              kLampArm, kMatMetal);
  AddBox(tipx + side * 0.12f, armY + 0.26f, a.z, 0.26f, 0.07f, 0.15f,
         kLampHead, kMatMetal);
  AddBox(tipx + side * 0.12f, armY + 0.145f, a.z, 0.21f, 0.045f, 0.12f,
         kLampLens, kMatGlow);
}

void AddVendingMachine(float x, float z, float face) {
  AddBox(x, 0.78f, z, 0.42f, 0.78f, 0.32f, kVendBody, kMatMetal);
  AddBox(x + face * 0.46f, 0.80f, z, 0.03f, 0.62f, 0.26f, kVendFront,
         kMatGlow);
  AddBox(x + face * 0.45f, 0.18f, z, 0.03f, 0.12f, 0.24f, kVendTray,
         kMatMetal);
}

void AddTreeline() {
  const int kCount = 74;
  for (int i = 0; i < kCount; i++) {
    float a = (float)i * 2.3999632f;
    float rad = 205.0f + 95.0f * (float)((i * 7) % 5) / 5.0f;
    float cx = std::sin(a) * rad * 1.25f;
    float cz = 90.0f + std::cos(a) * rad;
    float th = 9.0f + 9.0f * (float)((i * 13) % 7) / 7.0f;
    float wd = 2.6f + 1.8f * (float)((i * 5) % 4) / 4.0f;
    float g = 0.55f + 0.45f * (float)((i * 11) % 5) / 5.0f;
    float leaf[3] = {0.088f * g + 0.045f, 0.130f * g + 0.050f,
                     0.070f * g + 0.035f};
    AddBox(cx, th * 0.5f, cz, wd, th * 0.5f, wd * 0.9f, leaf, kMatLeaf);
    AddCylinder({cx, 0.0f, cz}, {cx, th * 0.30f, cz}, 0.24f, 0.18f, 4,
                kWoodDark, kMatWood);
  }
  // Low ridge of hills behind the trees, so the skyline is not a hard line.
  for (int i = 0; i < 26; i++) {
    float a = (float)i * 0.2417f + 0.3f;
    float rad = 330.0f + 40.0f * (float)((i * 3) % 4);
    float cx = std::sin(a) * rad * 1.5f;
    float cz = 90.0f + std::cos(a) * rad;
    float hh = 26.0f + 16.0f * (float)((i * 9) % 5) / 5.0f;
    AddBox(cx, hh * 0.30f, cz, hh * 1.7f, hh * 0.30f, hh * 1.2f, kHillTint,
           kMatLeaf);
  }
}

// --------------------------------------------------------------- the ground
void AddGroundGeometry() {
  const float n[3] = {0.0f, 1.0f, 0.0f};
  // The ground is sliced along Z rather than drawn as one quad: the GE's fog
  // is per vertex, and over a single 1.2 km triangle the fog gradient would
  // be interpolated across the whole screen instead of following distance.
  const float kGroundZ0 = -300.0f, kGroundZ1 = 880.0f, kSlice = 60.0f;
  for (float z = kGroundZ0; z < kGroundZ1; z += kSlice) {
    float zz = z + kSlice > kGroundZ1 ? kGroundZ1 : z + kSlice;
    float q[4][3] = {{-430.0f, kGroundY, z}, {430.0f, kGroundY, z},
                     {430.0f, kGroundY, zz}, {-430.0f, kGroundY, zz}};
    AddQuad(q, n[0], n[1], n[2], kGravel, kMatGround);
  }
  // Carriageway, shoulders, kerbs, centre dashes, edge lines.
  {
    const float z0 = -200.0f, z1 = 520.0f;
    const float road[3] = {0.195f, 0.190f, 0.198f};
    const float edge[3] = {0.235f, 0.215f, 0.190f};
    const float paint[3] = {0.520f, 0.480f, 0.420f};
    for (float z = z0; z < z1; z += kSlice) {
      float zz = z + kSlice > z1 ? z1 : z + kSlice;
      float q[4][3] = {{kRoadX - kRoadHalf, kRoadY, z},
                       {kRoadX + kRoadHalf, kRoadY, z},
                       {kRoadX + kRoadHalf, kRoadY, zz},
                       {kRoadX - kRoadHalf, kRoadY, zz}};
      AddQuad(q, n[0], n[1], n[2], road, kMatRoad);
      for (int e = 0; e < 2; e++) {
        float xs = e ? kRoadX + kRoadHalf : kRoadX - kRoadHalf;
        float xe = e ? xs + 0.90f : xs - 0.90f;
        float s[4][3] = {{xs, kRoadY, z}, {xe, kRoadY, z}, {xe, kRoadY, zz},
                         {xs, kRoadY, zz}};
        AddQuad(s, n[0], n[1], n[2], edge, kMatGround);
      }
    }
    for (int e = 0; e < 2; e++) {
      float sgn = e ? 1.0f : -1.0f;
      float xc = (e ? kRoadX + kRoadHalf : kRoadX - kRoadHalf) + sgn * 0.16f;
      // Sliced along Z for the same reason as the ground: a single 720 m box
      // would give the kerb a fog gradient unrelated to its distance.
      for (float z = z0; z < z1; z += kSlice) {
        float zz = z + kSlice > z1 ? z1 : z + kSlice;
        AddBox(xc, kKerbTopY * 0.5f, (z + zz) * 0.5f, 0.16f,
               kKerbTopY * 0.5f, (zz - z) * 0.5f, kKerbTint, kMatKerb);
      }
    }
    for (float z = -60.0f; z < 400.0f; z += 8.0f) {
      float d[4][3] = {{kRoadX - 0.09f, kPaintY, z}, {kRoadX + 0.09f, kPaintY, z},
                       {kRoadX + 0.09f, kPaintY, z + 3.0f},
                       {kRoadX - 0.09f, kPaintY, z + 3.0f}};
      AddQuad(d, n[0], n[1], n[2], paint, kMatLine);
    }
    for (int e = 0; e < 2; e++) {
      float xl = e ? kRoadX + kRoadHalf - 0.40f : kRoadX - kRoadHalf + 0.26f;
      float l[4][3] = {{xl, kPaintY, z0}, {xl + 0.14f, kPaintY, z0},
                       {xl + 0.14f, kPaintY, z1}, {xl, kPaintY, z1}};
      AddQuad(l, n[0], n[1], n[2], paint, kMatLine);
    }
  }
}

// Ground shadows: five fading strips per pole, projected away from the sun so
// the corridor reads as lit from the horizon. Built once; the sun's azimuth
// drift over a run is small enough that the strips do not visibly detach.
void AddGroundShadows() {
  float sun[3] = {gBakeSun[0], 0.0f, gBakeSun[2]};
  float l = std::sqrt(sun[0] * sun[0] + sun[2] * sun[2]);
  if (l < 1e-4f) return;
  sun[0] /= l; sun[2] /= l;
  const float perp[2] = {-sun[2], sun[0]};
  const float kAlpha[6] = {0.46f, 0.39f, 0.31f, 0.23f, 0.15f, 0.07f};
  const int kStrips = 5;

  auto addShadowVert = [](float x, float z, float a) {
    if (gShadowVerts >= 4000) return;
    gShadow[gShadowVerts++] = {Color8888(0.0f, 0.0f, 0.0f, a), x, kShadowY, z};
  };

  for (int line = 0; line < 2; line++) {
    const PoleSpec *poles = line ? gLineB : gLineA;
    int count = line ? 6 : 15;
    for (int pi = 0; pi < count; pi++) {
      const PoleSpec &p = poles[pi];
      float reach = 16.0f * (1.0f + 0.05f * (float)((int(p.z) % 7)));
      for (int k = 0; k < kStrips; k++) {
        float t0 = (float)k / kStrips, t1 = (float)(k + 1) / kStrips;
        float c0x = p.x - sun[0] * reach * t0, c0z = p.z - sun[2] * reach * t0;
        float c1x = p.x - sun[0] * reach * t1, c1z = p.z - sun[2] * reach * t1;
        float w0 = 0.30f + 0.95f * t0, w1 = 0.30f + 0.95f * t1;
        int base = gShadowVerts;
        addShadowVert(c0x + perp[0] * w0, c0z + perp[1] * w0, kAlpha[k]);
        addShadowVert(c0x - perp[0] * w0, c0z - perp[1] * w0, kAlpha[k]);
        addShadowVert(c1x - perp[0] * w1, c1z - perp[1] * w1, kAlpha[k + 1]);
        addShadowVert(c1x + perp[0] * w1, c1z + perp[1] * w1, kAlpha[k + 1]);
        if (gShadowIdxCount + 6 <= 6000) {
          gShadowIdx[gShadowIdxCount++] = (unsigned short)base;
          gShadowIdx[gShadowIdxCount++] = (unsigned short)(base + 1);
          gShadowIdx[gShadowIdxCount++] = (unsigned short)(base + 2);
          gShadowIdx[gShadowIdxCount++] = (unsigned short)base;
          gShadowIdx[gShadowIdxCount++] = (unsigned short)(base + 2);
          gShadowIdx[gShadowIdxCount++] = (unsigned short)(base + 3);
        }
      }
    }
  }
}

// ----------------------------------------------------------------- camera
// Declared before the sky code: the panorama is built from the live sun
// direction and the grid from the live camera.
float gEye[3] = {0.0f, 0.0f, 4.0f};
float gYaw = 0.0f, gPitch = 0.0f;
float gSun[3] = {-0.30f, 0.20f, 0.93f};
float gSceneTime = 0.0f;
int gSkyRow = 0;   // next panorama row to rebuild

// --------------------------------------------------------------- sky panorama
constexpr int kSkyW = 128, kSkyH = 64;
constexpr int kSunSize = 64;
constexpr int kSkyGridX = 32, kSkyGridY = 18;
constexpr int kSkyGridVerts = (kSkyGridX + 1) * (kSkyGridY + 1);

unsigned short gSkyTex[kSkyW * kSkyH] __attribute__((aligned(16)));
unsigned int gSunTex[kSunSize * kSunSize] __attribute__((aligned(16)));
struct SkyVertex { float u, v; float x, y, z; };
struct SunVertex { float u, v; unsigned int color; float x, y, z; };
SkyVertex gSkyGrid[kSkyGridVerts] __attribute__((aligned(16)));
unsigned short gSkyIdx[kSkyGridX * kSkyGridY * 6] __attribute__((aligned(16)));
SunVertex gSunQuad[6] __attribute__((aligned(16)));

float gNoise[64 * 64];

void BuildNoiseTable() {
  unsigned int s = 0x7F4A7C15u;
  for (int i = 0; i < 64 * 64; i++) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    gNoise[i] = (float)((s >> 8) & 0xFFFF) * (1.0f / 65535.0f);
  }
}

inline float NoiseLut(float u, float v) {
  u *= 64.0f; v *= 64.0f;
  int ui = (int)std::floor(u), vi = (int)std::floor(v);
  float uf = u - (float)ui, vf = v - (float)vi;
  int u0 = ui & 63, v0 = vi & 63, u1 = (u0 + 1) & 63, v1 = (v0 + 1) & 63;
  float a = gNoise[v0 * 64 + u0], b = gNoise[v0 * 64 + u1];
  float c = gNoise[v1 * 64 + u0], d = gNoise[v1 * 64 + u1];
  return Mix(Mix(a, b, uf), Mix(c, d, uf), vf);
}
inline float Fbm2(float x, float y) {
  return NoiseLut(x, y) * 0.66f + NoiseLut(x * 2.17f + 19.3f, y * 2.17f + 19.3f) * 0.34f;
}
inline float Fbm3(float x, float y) {
  return NoiseLut(x, y) * 0.54f +
         NoiseLut(x * 2.13f + 11.7f, y * 2.13f + 11.7f) * 0.29f +
         NoiseLut(x * 4.31f + 41.2f, y * 4.31f + 41.2f) * 0.17f;
}

// shaders/pole/sky_frag.glsl's atmosphere(), term by term.
void Atmosphere(float dx, float dy, float dz, const float sun[3], float out[3]) {
  const float kBetaR[3] = {0.058f, 0.135f, 0.331f};
  const float kBetaM = 0.0092f;
  const float kSunI = 330.0f;
  const float kSunPath = 14.0f;
  const float kRayGain = 13.0f;

  float h = dy;
  float mu = dx * sun[0] + dy * sun[1] + dz * sun[2];
  float mView = 1.0f / (std::fmax(h, 0.0f) + 0.14f);
  float mSun = 1.0f / (std::fmax(sun[1], 0.0f) + 0.14f);

  float phR = 0.0596831f * (1.0f + mu * mu);
  const float g = 0.76f, gg = g * g;
  float ddx = dx - g * sun[0], ddy = dy - g * sun[1], ddz = dz - g * sun[2];
  float miesq = ddx * ddx + ddy * ddy + ddz * ddz;
  float phM = 0.0795775f * (1.0f - gg) /
              std::fmax(std::pow(miesq, 1.5f), 1e-3f);

  float multiK = 0.005f + 0.30f * SmoothStep(0.78f, 1.06f, h);
  for (int c = 0; c < 3; c++) {
    float tv = std::exp(-(kBetaR[c] + kBetaM) * mView * 0.92f);
    float ts = std::exp(-kBetaR[c] * mSun * kSunPath - kBetaM * mSun * 1.15f);
    float single = kBetaR[c] * phR * kRayGain * tv * ts;
    float mie = kBetaM * phM * 0.25f * tv * ts;
    float multi = kBetaR[c] * phR * 0.80f * tv * multiK;
    out[c] = (single + mie + multi) * kSunI;
  }
}

inline float EncodeSky(float x) {
  x = Clamp(x, 0.0f, 8.0f);
  float enc = Clamp((x * (2.51f * x + 0.03f)) /
                        (x * (2.43f * x + 0.59f) + 0.14f),
                    0.0f, 1.0f);
  return std::pow(enc, 1.0f / 2.2f);
}

// Only rows [y0, y1) are baked, so the panorama can be refreshed a row at a
// time. The integral is ~20 libm calls per texel and the sun moves 0.004 rad/s;
// baking all 8192 texels in one frame stalled the picture for the better part
// of a second every ten seconds, which read as the scene freezing and picking
// up again. One row per frame spreads that over ~3 s of drift that is under a
// texel wide.
void BuildSkyRows(float timeSec, int y0, int y1) {
  const float *sun = gSun;
  const float driftx = timeSec * 0.0060f;
  const float driftz = -timeSec * 0.0023f;

  for (int y = y0; y < y1; y++) {
    float elev = (0.5f - ((float)y + 0.5f) / (float)kSkyH) * PI;
    float ce = std::cos(elev), se = std::sin(elev);
    for (int x = 0; x < kSkyW; x++) {
      float azim = ((float)x + 0.5f) / (float)kSkyW * TWO_PI;
      float dx = ce * std::cos(azim), dy = se, dz = ce * std::sin(azim);
      float h = dy;
      float mu = dx * sun[0] + dy * sun[1] + dz * sun[2];
      float r, g, b;

      if (h >= -0.02f) {
        float dir[3] = {dx, std::fmax(h, 0.0f), dz};
        float il = 1.0f / std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        dir[0] *= il; dir[1] *= il; dir[2] *= il;
        float out[3];
        Atmosphere(dir[0], dir[1], dir[2], sun, out);
        r = out[0]; g = out[1]; b = out[2];
        float horizon = std::exp(-std::fmax(h, 0.0f) * 5.6f);
        float mu3 = std::pow(std::fmax(mu, 0.0f), 3.0f);
        r += (0.450f + 0.480f * mu3) * horizon;
        g += (0.121f + 0.147f * mu3) * horizon;
        b += (0.031f + 0.038f * mu3) * horizon;
      } else {
        float dir[3] = {dx, 0.015f, dz};
        float il = 1.0f / std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        dir[0] *= il; dir[1] *= il; dir[2] *= il;
        float out[3];
        Atmosphere(dir[0], dir[1], dir[2], sun, out);
        r = out[0] * 0.42f + 0.030f;
        g = out[1] * 0.42f + 0.018f;
        b = out[2] * 0.42f + 0.010f;
      }

      // Cloud deck (same thresholds as the fragment shader).
      if (h > 0.030f) {
        float hh = std::fmax(h, 0.075f);
        float cpx = dx / hh * 0.30f, cpz = dz / hh * 0.30f;
        float base = Fbm3(cpx * 1.75f + driftx, cpz * 1.75f + driftz);
        float det = Fbm2(cpx * 6.50f - driftx * 2.4f + 31.0f,
                         cpz * 6.50f - driftz * 2.4f + 31.0f);
        float dens = base - 0.32f * (det - 0.5f);
        float cover = SmoothStep(0.560f, 0.780f, dens) * 0.94f;
        cover *= 0.80f + 0.20f * SmoothStep(0.40f, 0.66f, det);
        cover *= SmoothStep(0.035f, 0.26f, h);
        cover *= 1.0f - SmoothStep(0.62f, 0.99f, h);
        float lift = std::pow(Clamp(cover, 0.0f, 1.0f), 0.55f);
        float cr = Mix(0.40f, 1.42f, lift);
        float cg = Mix(0.106f, 0.775f, lift);
        float cb = Mix(0.058f, 0.400f, lift);
        float silver = std::pow(std::fmax(mu, 0.0f), 14.0f) * (1.0f - cover) * 1.35f;
        cr += 1.00f * silver;
        cg += 0.62f * silver;
        cb += 0.30f * silver;
        cr += 0.22f * (1.0f - lift) * std::fmax(mu, 0.0f);
        cg += 0.105f * (1.0f - lift) * std::fmax(mu, 0.0f);
        cb += 0.062f * (1.0f - lift) * std::fmax(mu, 0.0f);
        float k = Clamp(cover, 0.0f, 1.0f);
        r = Mix(r, cr, k);
        g = Mix(g, cg, k);
        b = Mix(b, cb, k);

        // Cirrus veil.
        float ch = std::fmax(h, 0.055f);
        float vpx = dx / ch * 0.30f, vpz = dz / ch * 0.30f;
        float cn = Fbm2(vpx * 9.0f - timeSec * 0.010f,
                        vpz * 4.5f + timeSec * 0.004f);
        float veil = SmoothStep(0.48f, 0.82f, cn);
        veil *= SmoothStep(0.035f, 0.20f, h) * (1.0f - SmoothStep(0.55f, 0.95f, h));
        float toward = std::pow(std::fmax(mu, 0.0f), 2.2f);
        float mixT = Clamp(veil * 0.26f, 0.0f, 0.26f);
        r = Mix(r, Mix(0.145f, 0.78f, 0.24f + 0.76f * toward), mixT);
        g = Mix(g, Mix(0.070f, 0.32f, 0.24f + 0.76f * toward), mixT);
        b = Mix(b, Mix(0.058f, 0.19f, 0.24f + 0.76f * toward), mixT);
      }

      gSkyTex[y * kSkyW + x] = Pack5551(EncodeSky(r), EncodeSky(g), EncodeSky(b));
    }
  }
  // The GE samples the panorama straight out of RAM: publish only the band
  // that changed.
  sceKernelDcacheWritebackRange(&gSkyTex[y0 * kSkyW],
                                (y1 - y0) * kSkyW * (int)sizeof(unsigned short));
}

void BuildSkyTexture(float timeSec) { BuildSkyRows(timeSec, 0, kSkyH); }

void BuildSunTexture() {
  for (int y = 0; y < kSunSize; y++) {
    for (int x = 0; x < kSunSize; x++) {
      float px = ((float)x + 0.5f) / (float)kSunSize * 2.0f - 1.0f;
      float py = ((float)y + 0.5f) / (float)kSunSize * 2.0f - 1.0f;
      float d = std::sqrt(px * px + py * py);
      float core = 1.0f - SmoothStep(0.22f, 0.34f, d);
      float limb = 0.55f + 0.45f * std::sqrt(Saturate(1.0f - (d / 0.34f) * (d / 0.34f)));
      float halo = std::exp(-d * 3.0f);
      float a = Clamp(core * limb + halo * 0.35f, 0.0f, 1.0f);
      float r = core * 1.00f + halo * 0.55f;
      float g = core * 0.72f + halo * 0.28f;
      float b = core * 0.42f + halo * 0.14f;
      gSunTex[y * kSunSize + x] = Color8888(r, g, b, a);
    }
  }
  sceKernelDcacheWritebackRange(gSunTex, sizeof(gSunTex));
}

void BuildSkyIndices() {
  int k = 0;
  for (int gy = 0; gy < kSkyGridY; gy++)
    for (int gx = 0; gx < kSkyGridX; gx++) {
      unsigned short a = (unsigned short)(gy * (kSkyGridX + 1) + gx);
      unsigned short b = (unsigned short)(a + 1);
      unsigned short c = (unsigned short)(a + kSkyGridX + 1);
      unsigned short d = (unsigned short)(c + 1);
      gSkyIdx[k++] = a; gSkyIdx[k++] = c; gSkyIdx[k++] = b;
      gSkyIdx[k++] = b; gSkyIdx[k++] = c; gSkyIdx[k++] = d;
    }
}

// Base (yaw 0) camera basis; the matrix builder in psp_main.cxx uses the same
// formulas, so sky and geometry agree about where "forward" is.
void CameraBasis(float yaw, float pitch, float right[3], float up[3], float fwd[3]) {
  float cy = std::cos(yaw), sy = std::sin(yaw);
  float cp = std::cos(pitch), sp = std::sin(pitch);
  fwd[0] = sy * cp; fwd[1] = sp; fwd[2] = cy * cp;
  right[0] = cy; right[1] = 0.0f; right[2] = -sy;
  up[0] = -sp * sy; up[1] = cp; up[2] = -sp * cy;
}

void BuildSkyGrid() {
  float right[3], up[3], fwd[3];
  CameraBasis(gYaw, gPitch, right, up, fwd);
  float tanHalf = std::tan(kFovY * 0.5f * PI / 180.0f);
  float aspect = (float)kScreenW / (float)kScreenH;
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
      gSkyGrid[n].u = std::atan2(dz, dx) / TWO_PI;
      gSkyGrid[n].v = 0.5f - std::asin(Clamp(dy, -1.0f, 1.0f)) / PI;
      gSkyGrid[n].x = nx;
      gSkyGrid[n].y = ny;
      gSkyGrid[n].z = 0.0f;
    }
  }

  // Sun sprite: project the unit sun direction through the same basis and
  // place a clip-space quad there. The atmosphere's halo is in the panorama;
  // this carries the disc and its limb, which a 32x18 grid cannot resolve.
  float sunDot = gSun[0] * fwd[0] + gSun[1] * fwd[1] + gSun[2] * fwd[2];
  float sx = 0.0f, sy = 0.0f, halfW = 0.0f, halfH = 0.0f;
  bool visible = sunDot > 0.05f;
  if (visible) {
    float denom = sunDot * tanHalf;
    sx = (gSun[0] * right[0] + gSun[1] * right[1] + gSun[2] * right[2]) /
         (denom * aspect);
    sy = (gSun[0] * up[0] + gSun[1] * up[1] + gSun[2] * up[2]) / denom;
    // ~3.0 degrees of half-angle including the halo.
    halfH = std::tan(3.0f * PI / 180.0f) / tanHalf;
    halfW = halfH / aspect;
    visible = (sx > -1.4f && sx < 1.4f && sy > -1.4f && sy < 1.4f);
  }
  if (!visible) {
    for (int i = 0; i < 6; i++) gSunQuad[i] = {0.0f, 0.0f, 0, 0, 0, 0};
    return;
  }
  const float x0 = sx - halfW, x1 = sx + halfW;
  const float y0 = sy - halfH, y1 = sy + halfH;
  unsigned int warm = Color8888(1.0f, 0.86f, 0.66f, 1.0f);
  gSunQuad[0] = {0.0f, 0.0f, warm, x0, y1, 0.0f};
  gSunQuad[1] = {1.0f, 0.0f, warm, x1, y1, 0.0f};
  gSunQuad[2] = {1.0f, 1.0f, warm, x1, y0, 0.0f};
  gSunQuad[3] = {0.0f, 0.0f, warm, x0, y1, 0.0f};
  gSunQuad[4] = {1.0f, 1.0f, warm, x1, y0, 0.0f};
  gSunQuad[5] = {0.0f, 1.0f, warm, x0, y0, 0.0f};
}

}  // namespace

// --------------------------------------------------------------------- API
void Scene4Init() {
  // Bake with the sun the scene starts under.
  gBakeSun[0] = -0.30f; gBakeSun[1] = 0.20f; gBakeSun[2] = 0.93f;
  {
    float l = std::sqrt(gBakeSun[0] * gBakeSun[0] + gBakeSun[1] * gBakeSun[1] +
                        gBakeSun[2] * gBakeSun[2]);
    gBakeSun[0] /= l; gBakeSun[1] /= l; gBakeSun[2] /= l;
  }
  gSun[0] = gBakeSun[0]; gSun[1] = gBakeSun[1]; gSun[2] = gBakeSun[2];

  BuildNoiseTable();
  BuildSkyIndices();
  BuildSunTexture();
  BuildSkyTexture(0.0f);
  gSkyRow = 0;

  // Pole layout (matches src/scene4.cxx's BuildSceneGeometry).
  for (int i = 0; i < 15; i++) {
    gLineA[i] = {-3.4f, 2.0f + 12.4f * i, 8.6f + 0.35f * ((i * 5) % 3),
                 i == 1 || i == 6 || i == 11, i == 3 || i == 9,
                 0.10f * ((i * 7) % 3 - 1), 0.08f * ((i * 5) % 3 - 1)};
  }
  for (int i = 0; i < 6; i++) {
    gLineB[i] = {8.6f, 6.0f + 15.5f * i, 7.6f, false, i == 1,
                 0.09f * ((i * 11) % 3 - 1), 0.07f * ((i * 3) % 3 - 1)};
  }

  // ---- ground group: drawn first, writes depth, before the shadows
  AddGroundGeometry();
  gGroundIdxCount = gSolidIdxCount;

  // ---- opaque detail
  for (int i = 0; i < 15; i++) AddPole(gLineA[i], true);
  for (int i = 0; i < 6; i++) AddPole(gLineB[i], false);
  for (int i = 0; i < 15; i += 2) AddStreetLight(gLineA[i], 1.0f);

  // Overhead conductors of line A.
  for (int i = 0; i + 1 < 15; i++) {
    const PoleSpec &p = gLineA[i];
    const PoleSpec &q = gLineA[i + 1];
    float sag = 0.78f + 0.12f * ((i * 3) % 3);
    for (float off : {-1.05f, 0.0f, 1.05f})
      AddWire(ArmInsulatorTop(p, off), ArmInsulatorTop(q, off), sag, 6, kCable);
    AddWire(PoleTopInsulatorTop(p), PoleTopInsulatorTop(q), sag * 0.8f, 6,
            kCableOld);
    for (float off : {-0.55f, 0.55f})
      AddWire(Arm2InsulatorTop(p, off), Arm2InsulatorTop(q, off), sag * 0.74f,
              5, kCable);
    // Telecom binder pairs, a representative 8 of the desktop scene's 16.
    for (int k = 0; k < 8; k++) {
      float u0 = std::fmod((float)k * 0.37f, 1.0f);
      float u1 = std::fmod((float)k * 0.61f + 0.23f, 1.0f);
      AddWire(TelecomBracketTop(p, -0.66f + 1.32f * u0),
              TelecomBracketTop(q, -0.66f + 1.32f * u1),
              0.18f + 0.62f * std::fmod((float)k * 0.29f, 1.0f), 4, kCableOld);
    }
  }
  // Line B conductors, and the spans that cross to line A.
  for (int i = 0; i + 1 < 6; i++) {
    const PoleSpec &p = gLineB[i];
    const PoleSpec &q = gLineB[i + 1];
    for (float off : {-1.05f, 1.05f})
      AddWire(ArmInsulatorTop(p, off), ArmInsulatorTop(q, off), 0.88f, 5,
              kCableOld);
    AddWire(PoleTopInsulatorTop(p), PoleTopInsulatorTop(q), 0.70f, 5,
            kCableOld);
    for (float off : {-0.55f, 0.55f})
      AddWire(Arm2InsulatorTop(p, off), Arm2InsulatorTop(q, off), 0.62f, 5,
              kCableOld);
  }
  for (int i = 0; i < 4; i++)
    AddWire(ArmInsulatorTop(gLineB[i], 1.05f),
            ArmInsulatorTop(gLineA[i + 1], -1.05f), 1.30f, 6, kCable);

  AddPylon(48.0f, 140.0f, 26.0f);
  AddPylon(-60.0f, 172.0f, 31.0f);

  for (int i = 0; i < 8; i++) {
    float z = -4.0f + 19.0f * i + 3.0f * ((i * 7) % 3);
    AddHouse(-11.5f - 2.0f * (i % 3), z, 4.6f + 1.4f * ((i * 3) % 3),
             5.2f + 1.2f * ((i * 5) % 3), 3.2f + 0.9f * ((i * 7) % 3), 1.0f);
  }
  for (int i = 0; i < 6; i++) {
    float z = 8.0f + 21.0f * i + 2.5f * ((i * 5) % 3);
    AddHouse(15.0f + 2.5f * (i % 3), z, 4.8f + 1.5f * ((i * 3) % 3),
             5.4f + 1.1f * ((i * 7) % 3), 3.1f + 1.0f * ((i * 5) % 3), -1.0f);
  }
  AddPropertyLine(-9.0f, -14.0f, 168.0f, -1.0f);
  AddPropertyLine(13.0f, -8.0f, 176.0f, 1.0f);
  AddVendingMachine(-7.4f, 30.0f, 1.0f);
  AddVendingMachine(11.6f, 96.0f, -1.0f);
  AddTreeline();

  AddGroundShadows();

  // The GE DMAs the arrays straight out of RAM: one flush after the build.
  sceKernelDcacheWritebackAll();

  // Geometry counts are logged (PSP printf goes to the emulator/debug console)
  // so an over-budget build is visible without a screenshot.
  printf("scene4: %d/%d solid verts, %d/%d indices, %d line verts\n",
         gSolidVerts, kMaxSolidVerts, gSolidIdxCount, kMaxSolidIndices,
         gLineVerts);
}

void Scene4Update(double t) {
  float ft = (float)t;
  gSceneTime = ft;
  const float span = 161.0f;
  float z = 4.0f + std::fmod(ft * 2.6f, span);
  gEye[0] = 2.0f + std::sin(ft * 0.05f) * 0.30f;
  gEye[1] = 1.55f + 0.18f * std::sin(ft * 0.11f);
  gEye[2] = z;
  float look[3] = {-0.45f, 5.5f, z + 26.0f};
  float fx = look[0] - gEye[0], fy = look[1] - gEye[1], fz = look[2] - gEye[2];
  float il = 1.0f / std::sqrt(fx * fx + fy * fy + fz * fz);
  fx *= il; fy *= il; fz *= il;
  gYaw = std::atan2(fx, fz);
  gPitch = std::asin(Clamp(fy, -1.0f, 1.0f));

  // SunDirNow() from src/scene4.cxx.
  float az = -0.30f - 0.004f * ft;
  float el = 0.20f - 0.0011f * ft;
  if (el < 0.15f) el = 0.15f;
  float sx = az, sy = el, sz = 0.93f;
  float sl = 1.0f / std::sqrt(sx * sx + sy * sy + sz * sz);
  gSun[0] = sx * sl; gSun[1] = sy * sl; gSun[2] = sz * sl;
}

void Scene4Camera(float outEye[3], float *yaw, float *pitch) {
  outEye[0] = gEye[0]; outEye[1] = gEye[1]; outEye[2] = gEye[2];
  *yaw = gYaw;
  *pitch = gPitch;
}

float Scene4FovY() { return kFovY; }

void Scene4DrawSky() {
  // One row of the panorama per frame; see BuildSkyRows().
  BuildSkyRows(gSceneTime, gSkyRow, gSkyRow + 1);
  gSkyRow = (gSkyRow + 1) % kSkyH;
  BuildSkyGrid();
  sceKernelDcacheWritebackRange(gSkyGrid, sizeof(gSkyGrid));
  sceKernelDcacheWritebackRange(gSunQuad, sizeof(gSunQuad));

  sceGuDisable(GU_DEPTH_TEST);
  sceGuDepthMask(GU_TRUE);
  sceGuDisable(GU_BLEND);
  sceGuDisable(GU_FOG);
  sceGuEnable(GU_TEXTURE_2D);
  sceGuTexMode(GU_PSM_5551, 0, 0, GU_FALSE);
  sceGuTexImage(0, kSkyW, kSkyH, kSkyW, (void *)gSkyTex);
  sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
  sceGuTexFilter(GU_LINEAR, GU_LINEAR);
  sceGuTexWrap(GU_REPEAT, GU_CLAMP);
  sceGuDrawArray(GU_TRIANGLES,
                 GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT,
                 kSkyGridX * kSkyGridY * 6, gSkyIdx, gSkyGrid);

  // The sun: additive, over the panorama, still depth-test free.
  sceGuEnable(GU_BLEND);
  // GU has no GU_ONE factor; the fixed factor is the additive-white idiom
  // used by psp_scene2.cxx.
  sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xFFFFFFFF);
  sceGuTexMode(GU_PSM_8888, 0, 0, GU_FALSE);
  sceGuTexImage(0, kSunSize, kSunSize, kSunSize, (void *)gSunTex);
  sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
  sceGuTexFilter(GU_LINEAR, GU_LINEAR);
  sceGuTexWrap(GU_CLAMP, GU_CLAMP);
  sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 |
                                     GU_VERTEX_32BITF, 6, nullptr, gSunQuad);

  sceGuDisable(GU_TEXTURE_2D);
  sceGuDisable(GU_BLEND);
  sceGuDepthMask(GU_FALSE);
  sceGuEnable(GU_DEPTH_TEST);
}

void Scene4Draw() {
  sceGuEnable(GU_DEPTH_TEST);
  sceGuDepthMask(GU_FALSE);
  sceGuDisable(GU_BLEND);
  sceGuDisable(GU_TEXTURE_2D);
  sceGuDisable(GU_CULL_FACE);
  // Dusk haze: the GE's per-vertex linear fog, matching the warm dust the
  // object shader's hz term converges to.
  sceGuEnable(GU_FOG);
  sceGuFog(120.0f, 850.0f, 0xFF293352);   // ABGR: r=52 g=33 b=29

  // Ground and road first, so the blended shadows land on top of them and
  // every opaque object that follows still wins the depth test.
  sceGuDrawArray(GU_TRIANGLES,
                 GU_COLOR_8888 | GU_VERTEX_32BITF | GU_INDEX_16BIT,
                 gGroundIdxCount, gSolidIdx, gSolid);

  // Shadows: multiply the destination by (1 - alpha).
  sceGuDepthMask(GU_TRUE);
  sceGuEnable(GU_BLEND);
  sceGuBlendFunc(GU_ADD, GU_ZERO, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
  sceGuDrawArray(GU_TRIANGLES,
                 GU_COLOR_8888 | GU_VERTEX_32BITF | GU_INDEX_16BIT,
                 gShadowIdxCount, gShadowIdx, gShadow);
  sceGuDisable(GU_BLEND);
  sceGuDepthMask(GU_FALSE);

  // Everything else, from the end of the ground group to the last vertex.
  int detail = gSolidIdxCount - gGroundIdxCount;
  if (detail > 0)
    sceGuDrawArray(GU_TRIANGLES,
                   GU_COLOR_8888 | GU_VERTEX_32BITF | GU_INDEX_16BIT,
                   detail, gSolidIdx + gGroundIdxCount, gSolid);

  // Cables.
  if (gLineVerts > 0)
    sceGuDrawArray(GU_LINES, GU_COLOR_8888 | GU_VERTEX_32BITF,
                   gLineVerts, nullptr, gLine);

  sceGuDisable(GU_FOG);
}
