// ElectroBench — scene 4 of the single ElectroBench binary: the power-lines
// scene. A late-afternoon Japanese suburb memory: orange sky,
// white drifting clouds, and a tangle of utility poles, crossarms, insulators,
// transformers and sagging wires receding into the heat haze — the
// serial-experiments mood, built entirely from analytic geometry (no model
// files, no textures: every cylinder, catenary and quad is generated on the CPU
// at startup).
//
// Like src/scene2.cxx and src/scene3.cxx, this translation unit is NOT a
// program of its own. It exports RunPoleScene(), which main.cxx calls as the
// fourth scene of the one and only ElectroBench executable.
//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#ifndef SDL_GL_SAMPLES
#define SDL_GL_SAMPLES SDL_GL_MULTISAMPLESAMPLES
#endif

#include "../lib/asset_path.hxx"

extern bool gWindowedMode;
#include "font_atlas.hxx"
// --------------------------------------------------------------- math block
struct Vec3 {
  float x, y, z;
};
static inline Vec3 Vec3Add(const Vec3 &a, const Vec3 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static inline Vec3 Vec3Scale(const Vec3 &a, float s) { return {a.x * s, a.y * s, a.z * s}; }
static inline Vec3 Vec3Sub(const Vec3 &a, const Vec3 &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static inline float Vec3Dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3 Vec3Cross(const Vec3 &a, const Vec3 &b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static inline float Vec3Len(const Vec3 &a) {
  return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
}
static inline Vec3 Vec3Norm(const Vec3 &a) {
  float l = Vec3Len(a);
  return l > 1e-8f ? Vec3Scale(a, 1.0f / l) : Vec3{0.0f, 1.0f, 0.0f};
}
static inline Vec3 Vec3PerpTo(const Vec3 &v) {
  const Vec3 ref = (std::fabs(v.y) < 0.9f) ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
  return Vec3Norm(Vec3Cross(v, ref));
}

using Mat4 = std::array<float, 16>;
static void Mat4Identity(Mat4 &m) { m.fill(0.0f); m[0] = m[5] = m[10] = m[15] = 1.0f; }
static void Mat4Multiply(Mat4 &out, const Mat4 &a, const Mat4 &b) {
  Mat4 r{};
  for (int c = 0; c < 4; c++)
    for (int rw = 0; rw < 4; rw++) {
      float s = 0.0f;
      for (int k = 0; k < 4; k++) s += a[k * 4 + rw] * b[c * 4 + k];
      r[c * 4 + rw] = s;
    }
  out = r;
}
static void Mat4Perspective(Mat4 &m, float fovYDeg, float aspect, float zNear, float zFar) {
  m.fill(0.0f);
  float f = 1.0f / std::tan(fovYDeg * 3.14159265f / 360.0f);
  m[0] = f / aspect;
  m[5] = f;
  m[10] = (zFar + zNear) / (zNear - zFar);
  m[11] = -1.0f;
  m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
}
static void Mat4LookAt(Mat4 &m, const Vec3 &eye, const Vec3 &center, const Vec3 &up) {
  Vec3 f = Vec3Norm(Vec3Sub(center, eye));
  Vec3 s = Vec3Norm(Vec3Cross(f, up));
  Vec3 u = Vec3Cross(s, f);
  Mat4 r{};
  r[0] = s.x;  r[4] = s.y;  r[8] = s.z;
  r[1] = u.x;  r[5] = u.y;  r[9] = u.z;
  r[2] = -f.x; r[6] = -f.y; r[10] = -f.z;
  r[12] = -Vec3Dot(s, eye);
  r[13] = -Vec3Dot(u, eye);
  r[14] = Vec3Dot(f, eye);
  r[15] = 1.0f;
  m = r;
}

static double NowSeconds() {
  return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
}

// ------------------------------------------------------------ shader plumbing
struct Program {
  GLuint handle = 0;
  std::vector<std::pair<std::string, GLint>> uniforms;

  GLint loc(const char *name) {
    for (auto &u : uniforms)
      if (u.first == name) return u.second;
    GLint l = glGetUniformLocation(handle, name);
    uniforms.emplace_back(name, l);
    return l;
  }
};

static GLuint CompileShader(GLenum type, const char *src, const char *name) {
  GLuint sh = glCreateShader(type);
  glShaderSource(sh, 1, &src, nullptr);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[2048];
    glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
    std::fprintf(stderr, "pole scene: shader %s failed:\n%s\n", name, log);
  }
  return sh;
}

static Program LinkProgram(const char *vsPath, const char *fsPath) {
  FILE *f = std::fopen(vsPath, "rb");
  if (!f) { std::fprintf(stderr, "pole scene: cannot open %s\n", vsPath); return {}; }
  std::string vs;
  {
    char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) vs.append(buf, n);
  }
  std::fclose(f);
  f = std::fopen(fsPath, "rb");
  if (!f) { std::fprintf(stderr, "pole scene: cannot open %s\n", fsPath); return {}; }
  std::string fs;
  {
    char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) fs.append(buf, n);
  }
  std::fclose(f);

  Program p;
  p.handle = glCreateProgram();
  GLuint v = CompileShader(GL_VERTEX_SHADER, vs.c_str(), vsPath);
  GLuint fr = CompileShader(GL_FRAGMENT_SHADER, fs.c_str(), fsPath);
  glAttachShader(p.handle, v);
  glAttachShader(p.handle, fr);
  glLinkProgram(p.handle);
  GLint ok = 0;
  glGetProgramiv(p.handle, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[2048];
    glGetProgramInfoLog(p.handle, sizeof(log), nullptr, log);
    std::fprintf(stderr, "pole scene: link %s+%s failed:\n%s\n", vsPath, fsPath, log);
  }
  glDeleteShader(v);
  glDeleteShader(fr);
  return p;
}

// ------------------------------------------------------------------ constants
static const char *const gName = "ElectroBench - Scene 4 (Lain)";
static const int WIDTH = 1280, HEIGHT = 720;
static const float kGroundY = 0.0f;
static const Vec3 kSunDir{-0.30f, 0.20f, 0.93f};

static Vec3 SunDirNow(double t) {
  float az = -0.30f - 0.004f * (float)t;               
  float el = 0.20f - 0.0011f * (float)t;                
  if (el < 0.15f) el = 0.15f;
  Vec3 s{az, el, 0.93f};
  return Vec3Norm(s);
}

// ---------------------------------------------------------------- scene state
static Program gSkyProg, gObjProg, gHudProg;
static GLuint gFontTex = 0;
static GLuint gSkyVao = 0, gSkyVbo = 0, gSkyIbo = 0;
static int gSkyIndexCount = 0;
static GLuint gObjVao = 0, gObjVbo = 0, gObjIbo = 0;
static int gObjIndexCount = 0;

static SDL_Window *gWindow = nullptr;
static SDL_GLContext gContext = nullptr;
static int gWindowWidth = WIDTH, gWindowHeight = HEIGHT;

static int gFrame = 0, gFps = 0, gFrameAccum = 0;
static double gFpsTimer = 0.0;
static double gStartTime = 0.0;
static double gSimTime = 0.0;      // SIM seconds: screenshot times are sim-based
static bool gQuit = false;
static bool gFusedDone = false;
static bool gStandaloneScene = false;
static double gResultsShownAt = 0.0;
static bool gResultsShown = false;
static double gResultsElapsed = 0.0, gResultsFps = 0.0, gResultsScore = 0.0;
static const double kResultsScreenSeconds = 4.0;
double gFusedPoleScore = 0.0;      // read by main.cxx for the combined screen

static const char *gScreenshotPath = nullptr;
static std::vector<float> gShotTimes;
static size_t gNextShot = 0;
static int gWindowWidthOverride = 0;

// ------------------------------------------------------------------- camera
static bool gAutoCam = true;
static Vec3 gCamPos{0.0f, 2.0f, 4.0f};
static float gCamYaw = 0.0f, gCamPitch = -0.06f, gCamDist = 7.0f;

static void UpdateAutoCamera(float t) {
  const float span = 161.0f;   // 13 spans of the 15-pole corridor
  float z = 4.0f + std::fmod(t * 2.6f, span);
  gCamPos = {2.0f + std::sin(t * 0.05f) * 0.30f,         // gentle weave
             1.55f + 0.18f * std::sin(t * 0.11f),        // breathing height
             z};
  Vec3 target{-1.15f, 7.6f, z + 17.0f};
  Vec3 f = Vec3Norm(Vec3Sub(target, gCamPos));
  gCamYaw = std::atan2(-f.x, -f.z);
  gCamPitch = std::asin(f.y) * -1.0f;   // negative = looking up
}

static Vec3 OrbitCamPos() {
  float cp = std::cos(gCamPitch), sp = std::sin(gCamPitch);
  Vec3 pos;
  pos.x = gCamPos.x + std::sin(gCamYaw) * cp * gCamDist;
  pos.y = gCamPos.y + sp * gCamDist;
  pos.z = gCamPos.z + std::cos(gCamYaw) * cp * gCamDist;
  if (pos.y < 0.5f) pos.y = 0.5f;                        // never under the ground
  return pos;
}
struct ObjVertex { float x, y, z, nx, ny, nz, r, g, b, mat, alpha; };
static std::vector<ObjVertex> gVerts;
static std::vector<unsigned int> gIdx;

static const float kMatPaint = 0.0f;      // generic, no detail
static const float kMatWood = 1.0f;
static const float kMatGround = 2.0f;
static const float kMatRoad = 3.0f;
static const float kMatLine = 4.0f;       // road paint
static const float kMatCable = 5.0f;
static const float kMatCeramic = 6.0f;
static const float kMatMetal = 7.0f;
static const float kMatWall = 8.0f;
static const float kMatRoof = 9.0f;
static const float kMatGlass = 10.0f;
static const float kMatLeaf = 11.0f;
static const float kMatShadow = 12.0f;
static const float kMatGlow = 13.0f;     
static const float kMatSteel = 14.0f;
static const float kMatKerb = 15.0f;
static const float kMatConcrete = 16.0f;
static const Vec3 kConcreteGrey{0.212f, 0.216f, 0.218f};

static const Vec3 kWoodDark{0.165f, 0.115f, 0.085f};   // creosote (treeline)
static const Vec3 kWoodOld{0.230f, 0.180f, 0.140f};   // weathered timber
static const Vec3 kCeramic{0.780f, 0.760f, 0.700f};   // insulator glaze
static const Vec3 kCable{0.055f, 0.050f, 0.055f};    // rubber wire
static const Vec3 kCableOld{0.085f, 0.075f, 0.070f};
static const Vec3 kMetal{0.190f, 0.195f, 0.200f};   
static const Vec3 kSteelGalv{0.252f, 0.261f, 0.273f}; // hot-dip zinc, trunk
static const Vec3 kSteelArm{0.265f, 0.272f, 0.282f};  // angle iron, brackets
static const Vec3 kSteelPlate{0.560f, 0.545f, 0.505f};// pole number plate
static const Vec3 kGravel{0.330f, 0.272f, 0.205f};  
static const float kRoadX = 2.6f;       // road centre (matches uRoadX)
static const float kRoadHalf = 2.7f;    // 5.4 m carriageway
static const float kRoadY = 0.030f;
static const float kPaintY = 0.050f;
static const float kShadowY = 0.075f;
static const float kKerbTopY = 0.190f;

static void PushVert(const Vec3 &p, const Vec3 &n, const Vec3 &c,
                     float mat = kMatPaint, float alpha = 1.0f) {
  gVerts.push_back({p.x, p.y, p.z, n.x, n.y, n.z, c.x, c.y, c.z, mat, alpha});
}
static void AddCylinder(const Vec3 &base, const Vec3 &top, float rBase, float rTop,
                        int segs, const Vec3 &color, float mat = kMatPaint) {
  Vec3 axis = Vec3Norm(Vec3Sub(top, base));
  Vec3 helper = std::abs(axis.y) > 0.9f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
  Vec3 s = Vec3Norm(Vec3Cross(axis, helper));
  Vec3 u = Vec3Cross(axis, s);
  unsigned int start = (unsigned int)gVerts.size();
  for (int i = 0; i <= segs; i++) {
    float a = (float)i / segs * 6.2831853f;
    Vec3 dir = Vec3Add(Vec3Scale(s, std::cos(a)), Vec3Scale(u, std::sin(a)));
    Vec3 n = dir;
    PushVert(Vec3Add(base, Vec3Scale(dir, rBase)), n, color, mat);
    PushVert(Vec3Add(top, Vec3Scale(dir, rTop)), n, color, mat);
  }
  for (int i = 0; i < segs; i++) {
    unsigned int a = start + (unsigned int)i * 2;
    gIdx.push_back(a); gIdx.push_back(a + 1); gIdx.push_back(a + 2);
    gIdx.push_back(a + 1); gIdx.push_back(a + 3); gIdx.push_back(a + 2);
  }
  // caps (fan around the centre vertex)
  for (int cap = 0; cap < 2; cap++) {
    Vec3 c = cap ? top : base;
    Vec3 n = cap ? axis : Vec3Scale(axis, -1.0f);
    float r = cap ? rTop : rBase;
    unsigned int ci = (unsigned int)gVerts.size();
    PushVert(c, n, color, mat);
    for (int i = 0; i <= segs; i++) {
      float a = (float)i / segs * 6.2831853f;
      Vec3 dir = Vec3Add(Vec3Scale(s, std::cos(a)), Vec3Scale(u, std::sin(a)));
      PushVert(Vec3Add(c, Vec3Scale(dir, r)), n, color, mat);
    }
    for (int i = 0; i < segs; i++) {
      unsigned int a = ci + 1 + (unsigned int)i;
      if (cap) { gIdx.push_back(ci); gIdx.push_back(a); gIdx.push_back(a + 1); }
      else     { gIdx.push_back(ci); gIdx.push_back(a + 1); gIdx.push_back(a); }
    }
  }
}

static void AddWire(const Vec3 &a, const Vec3 &b, float sag, float radius,
                    int samples, const Vec3 &color, float mat = kMatCable);

static Vec3 WirePoint(const Vec3 &a, const Vec3 &b, float sag, float t) {
  Vec3 delta = Vec3Sub(b, a);
  float len = Vec3Len(delta);
  if (len < 1e-3f) return a;
  Vec3 dir = Vec3Scale(delta, 1.0f / len);
  Vec3 side = Vec3PerpTo(dir);
  Vec3 up = Vec3Norm(Vec3Cross(side, dir));
  float halfSpan = len * 0.5f;
  Vec3 mid = Vec3Add(a, Vec3Scale(delta, 0.5f));
  float x = (t - 0.5f) * len;
  float y = sag * (1.0f - 4.0f * (t - 0.5f) * (t - 0.5f));
  float tail = sag * 0.06f * (std::cosh(x / (halfSpan * 0.72f)) - 1.0f)
             / (float)std::cosh(halfSpan / (halfSpan * 0.72f));
  return Vec3Add(Vec3Add(mid, Vec3Scale(dir, x)), Vec3Scale(up, -y - tail));
}

static std::vector<Vec3> gDropAnchors;
static void AddTelecomBundle(const Vec3 &a, const Vec3 &b, int seed,
                             float radius, int samples, const Vec3 &color) {
  float sag = 0.55f + 0.10f * ((seed * 7) % 3);
  AddWire(a, b, sag, radius, samples, color, kMatCable);
  int drops = 2 + (seed % 2);                    
  for (int i = 0; i < drops; i++) {
    float t = 0.18f + 0.62f * (float)((seed * 13 + i * 29) % 100) / 100.0f;
    float drop = 0.35f + 0.55f * (float)((seed * 17 + i * 41) % 100) / 100.0f;
    Vec3 peel = WirePoint(a, b, sag, t);
    Vec3 p{peel.x, peel.y - 0.035f, peel.z};
    AddCylinder(Vec3Add(peel, Vec3{0, -0.035f, 0}), Vec3Add(peel, Vec3{0, 0.035f, 0}),
                0.030f, 0.030f, 6, kMetal, kMatMetal);
    int best = -1;
    float bestD = 1e9f;
    const size_t n = gDropAnchors.size();
    for (size_t kk = 0; kk < n; kk++) {
      const Vec3 &q = gDropAnchors[(kk + (size_t)seed * 3 + (size_t)i) % n];
      float dxz = std::sqrt((q.x - p.x) * (q.x - p.x) + (q.z - p.z) * (q.z - p.z));
      if (dxz > 11.0f || q.y > p.y - 1.6f) continue;   // too far / uphill
      if (dxz < bestD) { bestD = dxz; best = (int)((kk + (size_t)seed * 3 +
                                                   (size_t)i) % n); }
    }
    if (best >= 0) {
      const Vec3 &q = gDropAnchors[best];
      AddWire(p, q, 0.16f + 0.10f * (float)(i % 2), radius * 0.55f, 9, color,
              kMatCable);
    } else {
      float len = 1.05f + 0.85f * (float)((seed * 23 + i * 13) % 100) / 100.0f;
      Vec3 q{p.x, p.y - len, p.z};
      AddWire(p, q, 0.08f, radius * 0.55f, 5, color, kMatCable);
      AddCylinder(q, Vec3Add(q, Vec3{0, -0.11f, 0}), 0.030f, 0.022f, 6,
                  kCableOld, kMatMetal);
      AddCylinder(Vec3Add(q, Vec3{0, -0.01f, 0}), Vec3Add(q, Vec3{0, 0.06f, 0}),
                  0.026f, 0.026f, 6, kCeramic, kMatCeramic);
    }
  }
}


static void AddWire(const Vec3 &a, const Vec3 &b, float sag, float radius,
                    int samples, const Vec3 &color, float mat) {
  Vec3 delta = Vec3Sub(b, a);
  float len = Vec3Len(delta);
  if (len < 1e-3f) return;

  Vec3 dir = Vec3Scale(delta, 1.0f / len);
  Vec3 side = Vec3PerpTo(dir);
  Vec3 up = Vec3Norm(Vec3Cross(side, dir));
  float halfSpan = len * 0.5f;

  std::vector<Vec3> centres(samples + 1);
  for (int i = 0; i <= samples; i++) {
    centres[i] = WirePoint(a, b, sag, (float)i / samples);
  }
  unsigned int start = (unsigned int)gVerts.size();
  for (int i = 0; i <= samples; i++) {
    Vec3 t0 = centres[i < samples ? i + 1 : i];
    Vec3 t1 = centres[i > 0 ? i - 1 : i];
    Vec3 tang = Vec3Norm(Vec3Sub(t0, t1));
    Vec3 s2 = Vec3PerpTo(tang);
    Vec3 u2 = Vec3Norm(Vec3Cross(s2, tang));
    for (int k = 0; k < 4; k++) {
      float a2 = (float)k * 1.5707963f;
      Vec3 n = Vec3Add(Vec3Scale(s2, std::cos(a2)), Vec3Scale(u2, std::sin(a2)));
      PushVert(Vec3Add(centres[i], Vec3Scale(n, radius)), n, color, mat);
    }
  }
  for (int i = 0; i < samples; i++) {
    unsigned int r0 = start + (unsigned int)i * 4;
    unsigned int r1 = r0 + 4;
    for (int k = 0; k < 4; k++) {
      unsigned int p0 = r0 + (unsigned int)k;
      unsigned int p1 = r0 + (unsigned int)((k + 1) % 4);
      unsigned int q0 = r1 + (unsigned int)k;
      unsigned int q1 = r1 + (unsigned int)((k + 1) % 4);
      gIdx.push_back(p0); gIdx.push_back(q0); gIdx.push_back(q1);
      gIdx.push_back(p0); gIdx.push_back(q1); gIdx.push_back(p1);
    }
  }
}

// Box via 6 quads (crossarms, transformer fins).
static void AddBox(const Vec3 &center, const Vec3 &half, const Vec3 &color,
                   float mat = kMatPaint) {
  static const int quads[6][4] = {
      {0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1},
      {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
  static const Vec3 norms[6] = {{0, 0, 1},  {0, 0, -1}, {1, 0, 0},
                                {-1, 0, 0}, {0, 1, 0},  {0, -1, 0}};
  Vec3 c[8];
  int m = 0;
  for (int dz = -1; dz <= 1; dz += 2)
    for (int dy = -1; dy <= 1; dy += 2)
      for (int dx = -1; dx <= 1; dx += 2)
        c[m++] = {center.x + half.x * dx, center.y + half.y * dy, center.z + half.z * dz};
  for (int q = 0; q < 6; q++) {
    unsigned int s = (unsigned int)gVerts.size();
    for (int k = 0; k < 4; k++) PushVert(c[quads[q][k]], norms[q], color, mat);
    gIdx.push_back(s); gIdx.push_back(s + 1); gIdx.push_back(s + 2);
    gIdx.push_back(s); gIdx.push_back(s + 2); gIdx.push_back(s + 3);
  }
}

struct PoleSpec {
  float x, z; float height; bool transformer; bool serviceSpool;
  float leanX, leanZ;
};
static Vec3 PoleAxisAt(const PoleSpec &p, float h) {
  float f = h / p.height;
  return {p.x + p.leanX * f, h, p.z + p.leanZ * f};
}
static const float kShaftRBase = 0.132f;
static const float kShaftRTop  = 0.086f;
static float TrunkRadius(const PoleSpec &p, float h) {
  float f = h / p.height;
  return kShaftRBase + (kShaftRTop - kShaftRBase) * f;
}
static Vec3 TelecomBracketTop(const PoleSpec &p, float off) {
  float telY = p.height - 0.55f - 1.30f;
  Vec3 a = PoleAxisAt(p, telY);
  return {a.x + off, telY + 0.15f, a.z};
}
static Vec3 ArmInsulatorTop(const PoleSpec &p, float off) {
  Vec3 a = PoleAxisAt(p, p.height - 0.25f);
  return {a.x + off, p.height - 0.25f, a.z};
}
static Vec3 PoleTopInsulatorTop(const PoleSpec &p) {
  Vec3 a = PoleAxisAt(p, p.height + 0.24f);
  return {a.x, p.height + 0.24f, a.z};
}
static Vec3 Arm2InsulatorTop(const PoleSpec &p, float off) {
  Vec3 a = PoleAxisAt(p, p.height - 0.87f);
  return {a.x + off, p.height - 0.87f, a.z};
}

static std::vector<PoleSpec> gLineA, gLineB;  
static float PoleRoadSide(const PoleSpec &p) { return p.x < kRoadX ? 1.0f : -1.0f; }


// geometry and the wire attachment cannot drift apart again.
static void AddInsulator(const Vec3 &ib) {
  AddCylinder(ib, Vec3Add(ib, Vec3{0, 0.055f, 0}), 0.044f, 0.040f, 6, kMetal,
              kMatMetal);
  AddCylinder(Vec3Add(ib, Vec3{0, 0.055f, 0}), Vec3Add(ib, Vec3{0, 0.175f, 0}),
              0.050f, 0.061f, 8, kCeramic, kMatCeramic);
  AddCylinder(Vec3Add(ib, Vec3{0, 0.175f, 0}), Vec3Add(ib, Vec3{0, 0.213f, 0}),
              0.061f, 0.038f, 8, kCeramic, kMatCeramic);
}

static void AddStepBolts(const PoleSpec &p, float from, float to, float side) {
  int i = 0;
  for (float h = from; h <= to + 1e-3f; h += 0.30f, i++) {
    float s = (i & 1) ? -side : side;
    Vec3 a = PoleAxisAt(p, h);
    float r = TrunkRadius(p, h);
    AddCylinder({a.x + s * (r - 0.02f), h, a.z}, {a.x + s * (r + 0.10f), h, a.z},
                0.024f, 0.019f, 5, kSteelArm, kMatSteel);
  }
}

static void AddGuyWire(const PoleSpec &p, float side, float zSign) {
  float hAtt = p.height - 0.95f;
  Vec3 att = PoleAxisAt(p, hAtt);
  float r = TrunkRadius(p, hAtt);
  att.x += side * (r + 0.11f);
  AddBox({att.x - side * 0.06f, att.y, att.z}, {0.05f, 0.13f, 0.09f}, kSteelArm,
         kMatSteel);
  Vec3 mid{att.x + side * 0.13f, att.y - 0.44f, att.z + zSign * 0.28f};
  AddCylinder(att, mid, 0.026f, 0.023f, 5, kSteelGalv, kMatSteel);
  Vec3 anc{p.x + side * 0.62f, 0.34f, p.z + zSign * 3.40f};
  AddWire(mid, anc, 0.03f, 0.015f, 5, kSteelGalv, kMatSteel);
  AddCylinder({anc.x - side * 0.30f, -0.10f, anc.z}, {anc.x, 0.42f, anc.z},
              0.042f, 0.032f, 6, kSteelGalv, kMatSteel);
  AddBox({anc.x - side * 0.13f, 0.14f, anc.z}, {0.36f, 0.14f, 0.36f},
         {0.230f, 0.222f, 0.208f}, kMatPaint);
}
static void AddSlackCoil(const PoleSpec &p, float h, float side, int turns,
                         float radius, float pitch) {
  float r0 = TrunkRadius(p, h);
  Vec3 a = PoleAxisAt(p, h);
  float cx = a.x + side * (r0 + radius);
  const int steps = turns * 12;
  const float coilRun = (float)turns * radius * 0.62f;
  AddCylinder({a.x + side * (r0 - 0.02f), h + pitch * 0.5f, a.z},
              {cx, h + pitch * 0.5f, a.z}, 0.022f, 0.022f, 5, kSteelArm,
              kMatSteel);
  Vec3 prev{cx + radius * 0.28f, h + pitch * 0.5f, a.z};
  for (int i = 1; i <= steps; i++) {
    float t = (float)i / (float)steps;
    float ang = t * (float)turns * 6.28318f;
    Vec3 cur{cx + radius * 0.28f * std::cos(ang),
             h + pitch * 0.5f + radius * std::sin(ang),
             a.z + coilRun * t};
    AddWire(prev, cur, 0.0f, 0.0115f, 2, kCable, kMatCable);
    prev = cur;
  }
  AddWire(prev, {a.x + side * (r0 + 0.03f), h + pitch * 0.5f + 0.06f, a.z},
          0.05f, 0.0115f, 8, kCable, kMatCable);
}

static void AddSlackLoop(const PoleSpec &p, float h, float side, float radius) {
  float r0 = TrunkRadius(p, h);
  Vec3 a = PoleAxisAt(p, h);
  const int steps = 22;
  Vec3 prev = a;
  for (int i = 1; i <= steps; i++) {
    float t = (float)i / (float)steps;
    float ang = 3.14159f * t;
    Vec3 cur{a.x + side * (r0 + 0.10f + radius * std::sin(ang) * 0.85f),
             h - radius * (1.0f - std::cos(ang)) * 0.5f,
             a.z};
    AddWire(prev, cur, 0.0f, 0.0115f, 2, kCable, kMatCable);
    prev = cur;
  }
  AddWire(prev, a, 0.0f, 0.0115f, 2, kCable, kMatCable);
}
static void AddPylon(float x, float z, float height) {
  const Vec3 steel{0.170f, 0.172f, 0.180f};
  const Vec3 galv{0.215f, 0.220f, 0.232f};
  const float footHalf = height * 0.175f;   // splayed feet
  const float kneeY = height * 0.34f;       // where the A-frame closes
  const float kneeHalf = footHalf * 0.46f;
  const float topHalf = height * 0.048f;
  for (int c = 0; c < 4; c++) {
    float sx = (c == 0 || c == 3) ? -1.0f : 1.0f;
    float sz = (c < 2) ? -1.0f : 1.0f;
    AddBox({x + sx * footHalf, 0.16f, z + sz * footHalf},
           {0.78f, 0.16f, 0.78f}, {0.255f, 0.250f, 0.238f}, kMatConcrete);
  }
  for (int c = 0; c < 4; c++) {
    float sx = (c == 0 || c == 3) ? -1.0f : 1.0f;
    float sz = (c < 2) ? -1.0f : 1.0f;
    AddCylinder({x + sx * footHalf, 0.12f, z + sz * footHalf},
                {x + sx * kneeHalf, kneeY, z + sz * kneeHalf}, 0.195f,
                0.145f, 5, steel, kMatSteel);
  }
  for (int c = 0; c < 4; c++) {
    float sx = (c == 0 || c == 3) ? -1.0f : 1.0f;
    float sz = (c < 2) ? -1.0f : 1.0f;
    AddCylinder({x + sx * kneeHalf, kneeY, z + sz * kneeHalf},
                {x + sx * topHalf, height, z + sz * topHalf}, 0.145f, 0.095f,
                5, steel, kMatSteel);
  }
  for (int s = 0; s < 4; s++) {
    float ax = (s == 0 || s == 3) ? -kneeHalf : kneeHalf;
    float az = (s < 2) ? -kneeHalf : kneeHalf;
    AddCylinder({x + ax, kneeY, z + az}, {x - ax, kneeY, z + az}, 0.105f,
                0.105f, 4, steel, kMatSteel);
    AddCylinder({x + ax, kneeY, z + az}, {x + ax, kneeY, z - az}, 0.105f,
                0.105f, 4, steel, kMatSteel);
  }
  for (float t = 0.0f; t < 0.94f; t += 0.235f) {
    float h = kneeY * (t + 0.118f);
    float hb = footHalf + (kneeHalf - footHalf) * ((t + 0.118f));
    for (int s = 0; s < 4; s++) {
      int n = (s + 1) & 3;
      float a0x = (s == 0 || s == 3) ? -hb : hb;
      float a0z = (s < 2) ? -hb : hb;
      float a1x = (n == 0 || n == 3) ? -hb : hb;
      float a1z = (n < 2) ? -hb : hb;
      AddCylinder({x + a0x, h, z + a0z}, {x + a1x, h + kneeY * 0.235f,
                                         z + a1z}, 0.085f, 0.085f, 3, steel,
                  kMatSteel);
    }
  }
  auto crossarm = [&](float y, float halfLen) {
    AddBox({x, y + 0.38f, z}, {halfLen, 0.38f, 0.42f}, steel, kMatSteel);
    AddBox({x, y - 0.34f, z}, {halfLen, 0.34f, 0.38f}, steel, kMatSteel);
    int bays = (int)(halfLen / 1.9f);
    for (int b = -bays; b <= bays; b++) {
      float bx = b * 1.9f;
      AddCylinder({x + bx, y - 0.34f, z}, {x + bx, y + 0.38f, z}, 0.105f,
                  0.105f, 4, steel, kMatSteel);
      if (std::fabs(bx) > topHalf + 0.4f && std::fabs(bx) < halfLen - 1.0f) {
        float sgn = bx < 0.0f ? -1.0f : 1.0f;
        AddCylinder({x + bx, y - 0.34f, z},
                    {x + bx - sgn * 1.9f, y - 1.45f, z}, 0.085f, 0.085f, 3,
                    steel, kMatSteel);
      }
    }
  };
  const float upperY = height * 0.735f, lowerY = height * 0.545f;
  crossarm(upperY, height * 0.345f);
  crossarm(lowerY, height * 0.265f);

  const Vec3 shed{0.300f, 0.288f, 0.262f};  
  auto insulator = [&](float ix, float iy) {
    AddBox({ix, iy + 0.13f, z}, {0.24f, 0.19f, 0.24f}, galv, kMatSteel);
    const float drop = 1.55f;             
    AddCylinder({ix, iy - 0.05f, z}, {ix, iy - drop - 0.05f, z}, 0.055f,
                0.046f, 5, steel, kMatSteel);
    const int sheds = 7;
    for (int i = 0; i < sheds; i++) {
      float t = (float)(i + 1) / (float)(sheds + 1);
      float y = iy - 0.05f - drop * t;
      float rr = 0.150f - 0.052f * t;     
      AddCylinder({ix, y - 0.042f, z}, {ix, y + 0.042f, z}, rr, rr, 6,
                  shed, kMatCeramic);
    }
    AddCylinder({ix, iy - drop - 0.04f, z}, {ix, iy - drop - 0.34f, z},
                0.050f, 0.040f, 6, steel, kMatSteel);
  };
  {
    const float uh = height * 0.345f, lh = height * 0.265f;
    for (float f : {0.94f, 0.66f, 0.38f})
      insulator(x + uh * f, upperY + 0.02f);
    for (float f : {0.90f, 0.58f})
      insulator(x - lh * f, lowerY + 0.02f);
  }
}

static void AddPole(const PoleSpec &p, bool concrete = false) {
  Vec3 base = PoleAxisAt(p, kGroundY);
  Vec3 top = PoleAxisAt(p, p.height);
  const float side = PoleRoadSide(p);
  if (concrete)
    AddCylinder(base, top, kShaftRBase, kShaftRTop, 10, kConcreteGrey, kMatConcrete);
  else
    AddCylinder(base, top, kShaftRBase, kShaftRTop, 10, kSteelGalv, kMatSteel);
  AddCylinder({base.x, kGroundY - 0.02f, base.z}, {base.x, 0.10f, base.z},
              0.52f, 0.34f, 8, {0.30f, 0.24f, 0.18f}, kMatGround);
  AddCylinder({base.x, 0.03f, base.z}, {base.x, 0.29f, base.z}, 0.235f, 0.205f,
              8, kSteelArm, kMatSteel);

  float armY = p.height - 0.55f;
  Vec3 armC = PoleAxisAt(p, armY);
  AddBox({armC.x, armY, armC.z}, {1.25f, 0.075f, 0.013f}, kSteelArm, kMatSteel);
  AddBox({armC.x, armY + 0.072f, armC.z}, {1.25f, 0.013f, 0.058f}, kSteelArm,
         kMatSteel);
  AddBox({armC.x, armY - 0.072f, armC.z}, {1.25f, 0.013f, 0.058f}, kSteelArm,
         kMatSteel);
  float arm2Y = armY - 0.62f;
  Vec3 arm2C = PoleAxisAt(p, arm2Y);
  AddBox({arm2C.x, arm2Y, arm2C.z}, {0.85f, 0.075f, 0.013f}, kSteelArm, kMatSteel);
  AddBox({arm2C.x, arm2Y + 0.072f, arm2C.z}, {0.85f, 0.013f, 0.052f}, kSteelArm,
         kMatSteel);
  AddBox({arm2C.x, arm2Y - 0.072f, arm2C.z}, {0.85f, 0.013f, 0.052f}, kSteelArm,
         kMatSteel);
  // diagonal knee braces, flat bar this time
  Vec3 brT = PoleAxisAt(p, armY - 0.05f);
  Vec3 brB = PoleAxisAt(p, armY - 0.57f);
  AddCylinder({brT.x - 0.34f, brT.y, brT.z + 0.05f},
              {brB.x - 0.94f, brB.y, brB.z + 0.05f}, 0.028f, 0.028f, 6,
              kSteelArm, kMatSteel);
  AddCylinder({brT.x + 0.34f, brT.y, brT.z + 0.05f},
              {brB.x + 0.94f, brB.y, brB.z + 0.05f}, 0.028f, 0.028f, 6,
              kSteelArm, kMatSteel);
  // bolted arm-to-shaft clamps: two bands and a nut each
  for (float by : {armY + 0.075f, arm2Y + 0.075f}) {
    Vec3 ba = PoleAxisAt(p, by);
    float br2 = TrunkRadius(p, by);
    AddCylinder({ba.x, by - 0.030f, ba.z}, {ba.x, by + 0.030f, ba.z},
                br2 + 0.035f, br2 + 0.035f, 8, kSteelArm, kMatSteel);
  }
  for (float off : {-1.05f, 0.0f, 1.05f})
    AddInsulator({armC.x + off, armY + 0.087f, armC.z});
  for (float off : {-0.55f, 0.55f})
    AddInsulator({arm2C.x + off, arm2Y + 0.087f, arm2C.z});
  AddCylinder(top, Vec3Add(top, Vec3{0, 0.18f, 0}), 0.05f, 0.058f, 8, kCeramic,
              kMatCeramic);
  AddCylinder(Vec3Add(top, Vec3{0, 0.18f, 0}), Vec3Add(top, Vec3{0, 0.24f, 0}),
              0.058f, 0.038f, 8, kCeramic, kMatCeramic);
  AddCylinder({top.x + 0.06f, p.height - 0.20f, top.z},
              {top.x + 0.13f, p.height + 0.60f, top.z}, 0.021f, 0.010f, 5,
              kSteelGalv, kMatSteel);

  {
    const float eside = -side;
    Vec3 prev = PoleAxisAt(p, 0.32f);
    prev.x += eside * (TrunkRadius(p, 0.32f) + 0.024f);
    AddWire(prev, {base.x + eside * 0.19f, 0.14f, base.z}, 0.01f, 0.012f, 3,
            kSteelGalv, kMatCable);
    for (float h = 1.4f; h <= 5.01f; h += 1.2f) {
      Vec3 cur = PoleAxisAt(p, h);
      cur.x += eside * (TrunkRadius(p, h) + 0.024f);
      AddWire(prev, cur, 0.012f, 0.012f, 3, kSteelGalv, kMatCable);
      Vec3 mid{(prev.x + cur.x) * 0.5f, (prev.y + cur.y) * 0.5f,
               (prev.z + cur.z) * 0.5f};
      Vec3 clip0 = PoleAxisAt(p, mid.y);   
      AddCylinder(clip0, mid, 0.019f, 0.019f, 4, kSteelArm, kMatSteel);
      prev = cur;
    }
  }

  // BUILD-P9: pole hardware on the road face — step bolts, the asset number
  // plate with its band strap, and (on a deterministic scatter) the guy wire.
  AddStepBolts(p, 3.15f, 6.15f, side);
  {
    Vec3 pa = PoleAxisAt(p, 2.35f);
    float pr = TrunkRadius(p, 2.35f);
    AddCylinder({pa.x + pr, 2.35f, pa.z}, {pa.x + pr + 0.035f, 2.35f, pa.z},
                pr + 0.030f, pr + 0.030f, 8, kSteelArm, kMatSteel);
    // the plate itself stands proud of the band, with a dark legend block
    AddBox({pa.x + pr + 0.042f, 2.62f, pa.z}, {0.012f, 0.085f, 0.062f},
           kSteelPlate, kMatMetal);
    AddBox({pa.x + pr + 0.058f, 2.62f, pa.z}, {0.006f, 0.020f, 0.050f},
           {0.075f, 0.070f, 0.065f}, kMatMetal);
    AddBox({pa.x + pr + 0.058f, 2.545f, pa.z}, {0.006f, 0.012f, 0.050f},
           {0.075f, 0.070f, 0.065f}, kMatMetal);
  }
  if (concrete && ((((int)(p.z * 0.5f)) % 3) == 0))
    AddSlackCoil(p, 5.2f, -side, 6, 0.30f, 1.15f);
  if (concrete && ((((int)(p.z * 0.7f)) % 4) == 1))
    AddSlackLoop(p, 6.5f, side, 0.85f);
  if ((((int)(p.z * 0.4f)) % 3) == 0) {
    AddGuyWire(p, side, 1.0f);
    if ((((int)(p.z * 0.8f)) % 5) == 0) AddGuyWire(p, side, -1.0f);
  }

  if (p.serviceSpool) {
    Vec3 sa = PoleAxisAt(p, 5.4f);
    float sr = TrunkRadius(p, 5.4f);
    AddCylinder({sa.x - sr, 5.4f, sa.z}, {sa.x - sr - 0.10f, 5.4f, sa.z}, 0.035f,
                0.035f, 6, kMetal, kMatMetal);
    AddCylinder({sa.x - sr - 0.10f, 5.4f, sa.z}, {sa.x - sr - 0.13f, 5.4f, sa.z},
                0.135f, 0.135f, 10, kSteelArm, kMatSteel);
    AddCylinder({sa.x - sr - 0.13f, 5.4f, sa.z}, {sa.x - sr - 0.24f, 5.4f, sa.z},
                0.135f, 0.135f, 10, kSteelArm, kMatSteel);
    AddCylinder({sa.x - sr - 0.17f, 5.4f, sa.z}, {sa.x - sr - 0.22f, 5.4f, sa.z},
                0.052f, 0.052f, 8, kMetal, kMatMetal);
  }
  float telY = armY - 1.30f;
  Vec3 telC = PoleAxisAt(p, telY);
  AddBox({telC.x, telY, telC.z}, {0.95f, 0.05f, 0.014f}, kSteelArm, kMatSteel);
  AddBox({telC.x, telY + 0.045f, telC.z}, {0.95f, 0.012f, 0.048f}, kSteelArm,
         kMatSteel);
  for (float off : {-0.70f, 0.0f, 0.70f}) {
    AddCylinder({telC.x + off, telY + 0.05f, telC.z},
                {telC.x + off, telY + 0.15f, telC.z}, 0.038f, 0.032f, 6, kMetal,
                kMatMetal);
  }
  if (((int(p.z * 7.0f)) % 3) == 0) {
    Vec3 ca = PoleAxisAt(p, 3.9f);
    float cr = TrunkRadius(p, 3.9f);
    float cx = ca.x + cr + 0.075f;
    for (float by : {3.99f, 4.41f})
      AddBox({ca.x + cr * 0.5f, by, ca.z}, {0.075f, 0.030f, 0.075f}, kSteelArm,
             kMatSteel);
    AddCylinder({cx, 3.9f, ca.z}, {cx, 4.5f, ca.z}, 0.11f, 0.11f, 8, kMetal,
                kMatMetal);
    AddCylinder({cx, 4.36f, ca.z}, {cx, 4.40f, ca.z}, 0.125f, 0.125f, 8,
                kSteelArm, kMatSteel);
    AddBox({cx, 4.06f, ca.z}, {0.055f, 0.045f, 0.115f}, kSteelArm, kMatSteel);
  }

  if (p.transformer) {
    Vec3 tc{armC.x + 0.62f, armY - 1.42f, armC.z + 0.44f};
    for (float bz : {-0.13f, 0.13f})
      AddBox({tc.x + bz, armY - 0.44f, (tc.z + armC.z) * 0.5f},
             {0.045f, 0.47f, 0.24f}, kSteelArm, kMatSteel);
    AddCylinder(Vec3Add(tc, Vec3{-0.1f, -0.55f, 0}),
                Vec3Add(tc, Vec3{0.1f, 0.55f, 0}), 0.34f, 0.34f, 10, kMetal,
                kMatMetal);
    for (int fi = 0; fi < 5; fi++) {
      float fy = tc.y - 0.40f + 0.20f * fi;
      AddBox({tc.x, fy, tc.z + 0.34f}, {0.30f, 0.028f, 0.085f}, kSteelArm,
             kMatSteel);
      AddBox({tc.x, fy, tc.z - 0.34f}, {0.30f, 0.028f, 0.085f}, kSteelArm,
             kMatSteel);
    }
    AddBox({tc.x, tc.y + 0.30f, tc.z}, {0.40f, 0.16f, 0.16f}, kMetal, kMatMetal);
    AddBox({tc.x, tc.y - 0.34f, tc.z}, {0.10f, 0.22f, 0.10f}, kMetal, kMatMetal);
    AddBox({tc.x + 0.30f, tc.y - 0.10f, tc.z + 0.10f}, {0.10f, 0.16f, 0.13f},
           kSteelArm, kMatSteel);
    for (float bz : {-0.12f, 0.12f}) {
      Vec3 bt{tc.x, tc.y + 0.46f, tc.z + bz};
      AddCylinder(bt, Vec3Add(bt, Vec3{0, 0.16f, 0}), 0.045f, 0.038f, 6,
                  kCeramic, kMatCeramic);
      AddWire(Vec3Add(bt, Vec3{0, 0.17f, 0}),
              ArmInsulatorTop(p, bz < 0.0f ? 0.0f : 1.05f), 0.05f, 0.011f, 6,
              kCable, kMatCable);
    }
    for (float cx : {-0.42f, 0.42f}) {
      AddCylinder({armC.x + cx, armY - 0.04f, armC.z},
                  {armC.x + cx, armY - 0.04f, armC.z + 0.17f}, 0.020f, 0.020f,
                  5, kSteelArm, kMatSteel);
      Vec3 ct{armC.x + cx, armY - 0.05f, armC.z + 0.17f};
      AddCylinder(ct, Vec3Add(ct, Vec3{0, -0.30f, 0}), 0.030f, 0.036f, 6,
                  kCeramic, kMatCeramic);
      AddCylinder(Vec3Add(ct, Vec3{0, -0.30f, 0}), Vec3Add(ct, Vec3{0, -0.36f, 0}),
                  0.038f, 0.028f, 6, kMetal, kMatMetal);
    }
    for (float ax : {-0.78f, 0.78f}) {
      AddCylinder({armC.x + ax, armY - 0.04f, armC.z},
                  {armC.x + ax, armY - 0.04f, armC.z + 0.15f}, 0.018f, 0.018f,
                  5, kSteelArm, kMatSteel);
      Vec3 at{armC.x + ax, armY - 0.05f, armC.z + 0.15f};
      AddCylinder(at, Vec3Add(at, Vec3{0, -0.22f, 0}), 0.032f, 0.038f, 6,
                  kCeramic, kMatCeramic);
    }
  }
}
static void AddQuad(const Vec3 &p0, const Vec3 &p1, const Vec3 &p2,
                    const Vec3 &p3, const Vec3 &color, float mat) {
  Vec3 n = Vec3Norm(Vec3Cross(Vec3Sub(p1, p0), Vec3Sub(p3, p0)));
  unsigned int s = (unsigned int)gVerts.size();
  PushVert(p0, n, color, mat);
  PushVert(p1, n, color, mat);
  PushVert(p2, n, color, mat);
  PushVert(p3, n, color, mat);
  gIdx.push_back(s); gIdx.push_back(s + 1); gIdx.push_back(s + 2);
  gIdx.push_back(s); gIdx.push_back(s + 2); gIdx.push_back(s + 3);
}
static const Vec3 kWallTints[5] = {
    {0.235f, 0.208f, 0.178f}, {0.285f, 0.256f, 0.214f}, {0.185f, 0.170f, 0.158f},
    {0.330f, 0.300f, 0.246f}, {0.160f, 0.152f, 0.148f}};
static const Vec3 kRoofTints[3] = {
    {0.095f, 0.084f, 0.082f}, {0.215f, 0.112f, 0.066f}, {0.125f, 0.118f, 0.115f}};

static void AddHouse(float x, float z, float w, float d, float h, float face) {
  int pick = (int)std::fmod(std::fabs(std::sin(x * 12.9898f + z * 78.233f)) *
                                43758.5453f,
                            5.0f);
  const Vec3 &wall = kWallTints[pick];
  const Vec3 &roof = kRoofTints[pick % 3];
  float streetX = face * w * 0.5f;         

  AddBox({x, 0.09f, z}, {w * 0.5f + 0.07f, 0.09f, d * 0.5f + 0.07f},
         {0.150f, 0.142f, 0.132f}, kMatPaint);
  AddBox({x, 0.10f + h * 0.5f, z}, {w * 0.5f, h * 0.5f, d * 0.5f}, wall,
         kMatWall);

  const float ov = 0.42f;                    
  const float rh = 0.55f + 0.06f * (float)(pick % 3);
  float yEave = 0.10f + h;
  float yRidge = yEave + rh;
  float x0 = x - w * 0.5f - ov, x1 = x + w * 0.5f + ov;
  float zA = z - d * 0.5f - ov, zB = z + d * 0.5f + ov;
  Vec3 r0{x0, yRidge, z}, r1{x1, yRidge, z};
  Vec3 eA{x0, yEave, zA}, eB{x1, yEave, zA};
  Vec3 eC{x1, yEave, zB}, eD{x0, yEave, zB};
  AddQuad(r0, r1, eB, eA, roof, kMatRoof);
  AddQuad(r1, r0, eD, eC, roof, kMatRoof);
  AddQuad(eA, eB, eC, eD, Vec3Add(roof, Vec3{0, 0.10f, 0}), kMatRoof);
  AddCylinder({x0, yRidge + 0.02f, z}, {x1, yRidge + 0.02f, z}, 0.055f, 0.055f,
              5, Vec3Add(roof, Vec3{0.05f, 0.03f, 0.02f}), kMatRoof);
  if (pick % 3 != 1) {
    float tx = x - w * 0.22f, tz = z + d * 0.10f;
    const Vec3 tank{0.055f, 0.058f, 0.062f};
    for (int l = 0; l < 4; l++) {
      float lx = tx + (l & 1 ? 0.26f : -0.26f);
      float lz = tz + (l & 2 ? 0.22f : -0.22f);
      AddCylinder({lx, yRidge - 0.45f, lz}, {lx, yRidge + 0.52f, lz}, 0.045f,
                  0.045f, 4, {0.090f, 0.086f, 0.082f}, kMatMetal);
    }
    AddCylinder({tx, yRidge + 0.52f, tz}, {tx, yRidge + 1.02f, tz}, 0.34f, 0.31f,
                8, tank, kMatMetal);
  }
  {   
    float ax = x + w * 0.28f, az = z - d * 0.06f;
    AddCylinder({ax, yRidge - 0.45f, az}, {ax, yRidge + 0.95f, az}, 0.028f,
                0.020f, 4, {0.120f, 0.118f, 0.115f}, kMatMetal);
    for (int k = 0; k < 2; k++)
      AddBox({ax, yRidge + 0.62f + 0.24f * k, az}, {0.30f, 0.016f, 0.016f},
             {0.120f, 0.118f, 0.115f}, kMatMetal);
  }
  if (pick % 2 == 0) { 
    float cx2 = x + streetX + face * 0.15f;
    AddBox({cx2, 1.85f, z + d * 0.12f}, {0.20f, 0.17f, 0.28f},
           {0.165f, 0.160f, 0.155f}, kMatMetal);
  }

  const Vec3 frame{0.320f, 0.300f, 0.270f};
  const Vec3 glass{0.060f, 0.075f, 0.095f};
  for (int i = 0; i < 3; i++) {
    float wx = x + streetX + face * 0.03f;
    float wz = z + (float)(i - 1) * (d * 0.30f);
    float wy = 0.10f + h * 0.60f;
    AddBox({wx + face * 0.02f, wy, wz}, {0.05f, 0.26f, 0.34f}, frame, kMatPaint);
    bool lit = ((i + pick) % 3) == 0;
    AddBox({wx + face * 0.06f, wy, wz}, {0.02f, 0.21f, 0.29f},
           lit ? Vec3{0.96f, 0.71f, 0.40f} : glass, lit ? kMatGlow : kMatGlass);
    if (lit) { 
      AddBox({wx + face * 0.082f, wy, wz}, {0.012f, 0.21f, 0.020f}, frame,
             kMatPaint);
      AddBox({wx + face * 0.082f, wy, wz}, {0.012f, 0.020f, 0.29f}, frame,
             kMatPaint);
    }
    AddBox({wx + face * 0.09f, wy - 0.28f, wz}, {0.07f, 0.035f, 0.40f}, frame,
           kMatPaint);
  }
  AddBox({x - w * 0.5f + 0.03f * face, 0.10f + h * 0.55f, z + d * 0.22f},
         {0.03f, 0.20f, 0.28f}, frame, kMatPaint);
  AddBox({x - w * 0.5f + 0.06f * face, 0.10f + h * 0.55f, z + d * 0.22f},
         {0.02f, 0.16f, 0.24f}, glass, kMatGlass);

  if (pick % 2 == 0) {
    Vec3 deck{0.290f, 0.270f, 0.240f};
    AddBox({x + streetX + face * 0.55f, 2.32f, z - d * 0.18f},
           {0.55f, 0.05f, 0.62f}, deck, kMatRoof);
    AddCylinder({x + streetX + face * 1.02f, 0.0f, z - d * 0.18f},
                {x + streetX + face * 1.02f, 2.30f, z - d * 0.18f}, 0.055f,
                0.045f, 6, deck, kMatPaint);
    AddBox({x + streetX + face * 0.02f, 1.20f, z - d * 0.18f},
           {0.04f, 0.55f, 0.34f}, frame, kMatPaint);
  }
  Vec3 anchor{x + streetX + face * 0.16f, 0.10f + h * 0.78f, z + d * 0.30f};
  AddBox({anchor.x - face * 0.04f, anchor.y, anchor.z}, {0.07f, 0.045f, 0.045f},
         kMetal, kMatMetal);
  AddCylinder({anchor.x, anchor.y + 0.04f, anchor.z},
              {anchor.x, anchor.y + 0.17f, anchor.z}, 0.030f, 0.026f, 6,
              kCeramic, kMatCeramic);
  gDropAnchors.push_back({anchor.x, anchor.y + 0.17f, anchor.z});
}

static void AddPropertyLine(float x, float z0, float z1, float mirror) {
  const Vec3 wall{0.150f, 0.145f, 0.135f};
  const Vec3 cap{0.190f, 0.176f, 0.158f};
  float z = z0;
  int i = 0;
  while (z < z1) {
    float seg = 7.0f + 3.0f * (float)((i * 7) % 4) / 4.0f;
    float h = 1.50f + 0.26f * (float)((i * 5) % 3) / 3.0f;
    float cz = z + seg * 0.5f;
    AddBox({x, h * 0.5f, cz}, {0.11f, h * 0.5f, seg * 0.5f}, wall, kMatPaint);
    AddBox({x, h + 0.045f, cz}, {0.17f, 0.045f, seg * 0.5f}, cap, kMatRoof);
    if ((i % 3) == 1) {                     
      AddBox({x, 1.05f, z}, {0.17f, 1.05f, 0.17f}, cap, kMatPaint);
      AddBox({x, 2.14f, z}, {0.22f, 0.05f, 0.22f}, cap, kMatRoof);
    }
    if ((i % 2) == 0) {                     
      float hx = x + mirror * (0.95f + 0.55f * (float)((i * 3) % 3) / 3.0f);
      AddBox({hx, 0.58f, cz}, {0.46f, 0.58f, seg * 0.40f},
             {0.048f, 0.078f, 0.040f}, kMatLeaf);
    }
    z += seg + 0.35f;
    i++;
  }
}

static void AddStreetLight(const PoleSpec &p, float side) {
  float armY = p.height - 2.35f;
  Vec3 a = PoleAxisAt(p, armY);
  float tipx = a.x + side * 2.15f;
  AddCylinder({a.x, armY, a.z}, {tipx, armY + 0.30f, a.z}, 0.055f, 0.045f, 6,
              {0.140f, 0.138f, 0.135f}, kMatMetal);
  AddBox({tipx + side * 0.12f, armY + 0.26f, a.z}, {0.26f, 0.07f, 0.15f},
         {0.150f, 0.148f, 0.145f}, kMatMetal);
  AddBox({tipx + side * 0.12f, armY + 0.145f, a.z}, {0.21f, 0.045f, 0.12f},
         {0.88f, 0.68f, 0.42f}, kMatGlow);
}
static void AddVendingMachine(float x, float z, float face) {
  AddBox({x, 0.78f, z}, {0.42f, 0.78f, 0.32f}, {0.185f, 0.150f, 0.120f},
         kMatMetal);
  AddBox({x + face * 0.46f, 0.80f, z}, {0.03f, 0.62f, 0.26f},
         {0.88f, 0.93f, 1.00f}, kMatGlow);
  AddBox({x + face * 0.45f, 0.18f, z}, {0.03f, 0.12f, 0.24f},
         {0.30f, 0.26f, 0.22f}, kMatMetal);
}
static void AddTreeline() {
  const int kCount = 74;
  for (int i = 0; i < kCount; i++) {
    float a = (float)i * 2.3999632f;               
    float rad = 205.0f + 95.0f * (float)((i * 7) % 5) / 5.0f;
    float cx = std::sin(a) * rad * 1.25f;
    float cz = 90.0f + std::cos(a) * rad;
    float th = 9.0f + 9.0f * (float)((i * 13) % 7) / 7.0f;
    float wd = 2.6f + 1.8f * (float)((i * 5) % 4) / 4.0f;
    float g = 0.55f + 0.45f * (float)((i * 11) % 5) / 5.0f;
    Vec3 leaf{0.088f * g + 0.045f, 0.130f * g + 0.050f, 0.070f * g + 0.035f};
    AddBox({cx, th * 0.5f, cz}, {wd, th * 0.5f, wd * 0.9f}, leaf, kMatLeaf);
    AddBox({cx + wd * 0.5f, th * 0.34f, cz - wd * 0.4f},
           {wd * 0.8f, th * 0.34f, wd * 0.7f}, leaf, kMatLeaf);
    // trunk
    AddCylinder({cx, 0.0f, cz}, {cx, th * 0.30f, cz}, 0.24f, 0.18f, 5,
                {0.100f, 0.082f, 0.070f}, kMatWood);
  }
  // a low ridge of hills behind the trees, so the skyline is not a hard line
  for (int i = 0; i < 26; i++) {
    float a = (float)i * 0.2417f + 0.3f;
    float rad = 330.0f + 40.0f * (float)((i * 3) % 4);
    float cx = std::sin(a) * rad * 1.5f;
    float cz = 90.0f + std::cos(a) * rad;
    float hh = 26.0f + 16.0f * (float)((i * 9) % 5) / 5.0f;
    AddBox({cx, hh * 0.30f, cz}, {hh * 1.7f, hh * 0.30f, hh * 1.2f},
           {0.215f, 0.185f, 0.175f}, kMatLeaf);
  }
}static void BuildSceneGeometry() {
  gVerts.clear();
  gIdx.clear();
  gLineA.clear();
  gLineB.clear();
  gDropAnchors.clear();
  {
    unsigned int s = (unsigned int)gVerts.size();
    Vec3 n{0, 1, 0};
    PushVert({-430, kGroundY, -300}, n, kGravel, kMatGround);
    PushVert({430, kGroundY, -300}, n, kGravel, kMatGround);
    PushVert({430, kGroundY, 880}, n, kGravel, kMatGround);
    PushVert({-430, kGroundY, 880}, n, kGravel, kMatGround);
    gIdx.push_back(s); gIdx.push_back(s + 1); gIdx.push_back(s + 2);
    gIdx.push_back(s); gIdx.push_back(s + 2); gIdx.push_back(s + 3);
  }

  std::vector<PoleSpec> lineA;
  for (int i = 0; i < 15; i++)
    lineA.push_back({-3.4f, 2.0f + 12.4f * i, 8.6f + 0.35f * ((i * 5) % 3),
                     i == 1 || i == 6 || i == 11, i == 3 || i == 9,
                     0.10f * ((i * 7) % 3 - 1), 0.08f * ((i * 5) % 3 - 1)});
  for (const PoleSpec &p : lineA) AddPole(p, true);
  gLineA = lineA;
  for (int i = 0; i < (int)lineA.size(); i += 2) AddStreetLight(lineA[i], 1.0f);
  for (int i = 0; i + 1 < (int)lineA.size(); i++) {
    const PoleSpec &p = lineA[i];
    const PoleSpec &q = lineA[i + 1];
    float sag = 0.78f + 0.12f * ((i * 3) % 3);
    for (float off : {-1.05f, 0.0f, 1.05f})
      AddWire(ArmInsulatorTop(p, off), ArmInsulatorTop(q, off), sag, 0.032f,
              14, kCable, kMatCable);
    AddWire(PoleTopInsulatorTop(p), PoleTopInsulatorTop(q), sag * 0.8f, 0.037f,
            14, kCableOld, kMatCable);
    for (float off : {-0.55f, 0.55f})
      AddWire(Arm2InsulatorTop(p, off), Arm2InsulatorTop(q, off), sag * 0.74f,
              0.028f, 12, kCable, kMatCable);
  }

  for (int i = 0; i + 1 < (int)lineA.size(); i++) {
    const PoleSpec &p = lineA[i];
    const PoleSpec &q = lineA[i + 1];
    for (int k = 0; k < 16; k++) {
      float fk = (float)k;
      float u0 = fk * 0.37f;  u0 -= std::floor(u0);
      float u1 = fk * 0.61f + 0.23f; u1 -= std::floor(u1);
      float u2 = fk * 0.29f;  u2 -= std::floor(u2);
      float u3 = fk * 0.83f;  u3 -= std::floor(u3);
      Vec3 a = TelecomBracketTop(p, -0.66f + 1.32f * u0);
      Vec3 b = TelecomBracketTop(q, -0.66f + 1.32f * u1);
      float sag = 0.18f + 0.62f * u2;
      AddWire(a, b, sag, r, 10, kCableOld, kMatCable);
    }
  }
  AddPylon(48.0f, 140.0f, 26.0f);
  AddPylon(-60.0f, 172.0f, 31.0f);
  std::vector<PoleSpec> lineB;
  for (int i = 0; i < 6; i++)
    lineB.push_back({8.6f, 6.0f + 15.5f * i, 7.6f, false, i == 1,
                     0.09f * ((i * 11) % 3 - 1), 0.07f * ((i * 3) % 3 - 1)});
  for (const PoleSpec &p : lineB) AddPole(p);
  gLineB = lineB;
  for (int i = 0; i + 1 < (int)lineB.size(); i++) {
    const PoleSpec &p = lineB[i];
    const PoleSpec &q = lineB[i + 1];
    for (float off : {-1.05f, 1.05f})
      AddWire(ArmInsulatorTop(p, off), ArmInsulatorTop(q, off), 0.88f, 0.030f,
              12, kCableOld, kMatCable);
    AddWire(PoleTopInsulatorTop(p), PoleTopInsulatorTop(q), 0.70f, 0.035f, 12,
            kCableOld, kMatCable);
    for (float off : {-0.55f, 0.55f})
      AddWire(Arm2InsulatorTop(p, off), Arm2InsulatorTop(q, off), 0.62f, 0.028f,
              12, kCableOld, kMatCable);
  }
  for (int i = 0; i < 4; i++) {
    const PoleSpec &p = lineB[i];
    const PoleSpec &q = lineA[i + 1];
    AddWire(ArmInsulatorTop(p, 1.05f), ArmInsulatorTop(q, -1.05f), 1.30f,
            0.028f, 16, kCable, kMatCable);
  }
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
  for (int i = 0; i + 1 < (int)lineA.size(); i++) {
    const PoleSpec &p = lineA[i];
    const PoleSpec &q = lineA[i + 1];
    AddTelecomBundle(TelecomBracketTop(p, -0.70f), TelecomBracketTop(q, -0.70f),
                     i * 2 + 1, 0.027f, 12, kCable);
    AddTelecomBundle(TelecomBracketTop(p, 0.70f), TelecomBracketTop(q, 0.70f),
                     i * 2 + 2, 0.027f, 12, kCable);
    if ((i % 2) == 0)
      AddTelecomBundle(TelecomBracketTop(p, 0.0f), TelecomBracketTop(q, 0.0f),
                       i * 2 + 13, 0.024f, 12, kCableOld);
  }
  for (int i = 0; i + 1 < (int)lineB.size(); i++) {
    const PoleSpec &p = lineB[i];
    const PoleSpec &q = lineB[i + 1];
    AddTelecomBundle(TelecomBracketTop(p, 0.0f), TelecomBracketTop(q, 0.0f),
                     i * 3 + 40, 0.024f, 10, kCableOld);
  }
  for (int i = 0; i < 5; i++) {
    const PoleSpec &p = lineB[i];
    const PoleSpec &q = lineA[i + 1];
    AddTelecomBundle(TelecomBracketTop(p, -0.70f), TelecomBracketTop(q, -0.70f),
                     i * 5 + 77, 0.022f, 14, kCable);
  }
  for (int side = 0; side < 2; side++) {
    const PoleSpec &p = lineA[side == 0 ? 3 : 9];
    int best = -1;
    float bestD = 1e9f;
    for (size_t k = 0; k < gDropAnchors.size(); k++) {
      const Vec3 &q = gDropAnchors[k];
      float d = std::sqrt((q.x - p.x) * (q.x - p.x) + (q.z - p.z) * (q.z - p.z));
      if (d < bestD && d > 3.0f) { bestD = d; best = (int)k; }
    }
    if (best >= 0) {
      const Vec3 &a = gDropAnchors[best];
      AddWire(PoleAxisAt(p, 5.4f), a, 0.55f, 0.024f, 12, kCableOld, kMatCable);
      AddWire(ArmInsulatorTop(p, 1.05f), a, 0.48f, 0.024f, 12, kCableOld,
              kMatCable);
    }
    Vec3 ja = PoleAxisAt(p, 3.05f);
    float jr = TrunkRadius(p, 3.05f);
    AddBox({ja.x - jr - 0.06f, 3.10f, ja.z}, {0.16f, 0.10f, 0.12f}, kMetal,
           kMatMetal);
    Vec3 stubEnd{ja.x - jr - 0.10f, 2.45f, ja.z};
    AddCylinder({ja.x - jr, 3.05f, ja.z}, stubEnd, 0.09f, 0.09f, 8, kMetal,
                kMatMetal);
    AddCylinder(stubEnd, {stubEnd.x - 0.04f, 2.30f, stubEnd.z}, 0.075f, 0.105f,
                8, kMetal, kMatMetal); 
    if (best >= 0) {
      AddWire({stubEnd.x - 0.04f, 2.24f, stubEnd.z}, gDropAnchors[best], 0.42f,
              0.019f, 8, kCableOld, kMatCable);
    } else {
      AddWire({stubEnd.x - 0.04f, 2.24f, stubEnd.z},
              {ja.x - jr - 0.02f, 2.86f, ja.z}, 0.16f, 0.019f, 8, kCableOld,
              kMatCable);
    }
  }
  {
    Vec3 n{0, 1, 0};
    const float z0 = -200.0f, z1 = 520.0f;
    Vec3 road{0.195f, 0.190f, 0.198f};
    Vec3 edge{0.235f, 0.215f, 0.190f};
    Vec3 paint{0.520f, 0.480f, 0.420f};
    unsigned int s = (unsigned int)gVerts.size();
    PushVert({kRoadX - kRoadHalf, kRoadY, z0}, n, road, kMatRoad);
    PushVert({kRoadX + kRoadHalf, kRoadY, z0}, n, road, kMatRoad);
    PushVert({kRoadX + kRoadHalf, kRoadY, z1}, n, road, kMatRoad);
    PushVert({kRoadX - kRoadHalf, kRoadY, z1}, n, road, kMatRoad);
    gIdx.push_back(s); gIdx.push_back(s + 1); gIdx.push_back(s + 2);
    gIdx.push_back(s); gIdx.push_back(s + 2); gIdx.push_back(s + 3);
    for (int e = 0; e < 2; e++) {
      float xs = e ? kRoadX + kRoadHalf : kRoadX - kRoadHalf;
      float xe = e ? xs + 0.90f : xs - 0.90f;
      unsigned int es = (unsigned int)gVerts.size();
      PushVert({xs, kRoadY, z0}, n, edge, kMatGround);
      PushVert({xe, kRoadY, z0}, n, edge, kMatGround);
      PushVert({xe, kRoadY, z1}, n, edge, kMatGround);
      PushVert({xs, kRoadY, z1}, n, edge, kMatGround);
      gIdx.push_back(es); gIdx.push_back(es + 1); gIdx.push_back(es + 2);
      gIdx.push_back(es); gIdx.push_back(es + 2); gIdx.push_back(es + 3);
    }
    for (int e = 0; e < 2; e++) {
      float sgn = e ? 1.0f : -1.0f;
      float xc = (e ? kRoadX + kRoadHalf : kRoadX - kRoadHalf) + sgn * 0.16f;
      AddBox({xc, kKerbTopY * 0.5f, (z0 + z1) * 0.5f}, {0.16f, kKerbTopY * 0.5f,
             (z1 - z0) * 0.5f},
             {0.245f, 0.238f, 0.225f}, kMatKerb);
    }
    for (float z = -60.0f; z < 400.0f; z += 8.0f) {
      unsigned int ds = (unsigned int)gVerts.size();
      PushVert({kRoadX - 0.09f, kPaintY, z}, n, paint, kMatLine);
      PushVert({kRoadX + 0.09f, kPaintY, z}, n, paint, kMatLine);
      PushVert({kRoadX + 0.09f, kPaintY, z + 3.0f}, n, paint, kMatLine);
      PushVert({kRoadX - 0.09f, kPaintY, z + 3.0f}, n, paint, kMatLine);
      gIdx.push_back(ds); gIdx.push_back(ds + 1); gIdx.push_back(ds + 2);
      gIdx.push_back(ds); gIdx.push_back(ds + 2); gIdx.push_back(ds + 3);
    }
    for (int e = 0; e < 2; e++) {
      float xl = e ? kRoadX + kRoadHalf - 0.40f : kRoadX - kRoadHalf + 0.26f;
      unsigned int es = (unsigned int)gVerts.size();
      PushVert({xl, kPaintY, z0}, n, paint, kMatLine);
      PushVert({xl + 0.14f, kPaintY, z0}, n, paint, kMatLine);
      PushVert({xl + 0.14f, kPaintY, z1}, n, paint, kMatLine);
      PushVert({xl, kPaintY, z1}, n, paint, kMatLine);
      gIdx.push_back(es); gIdx.push_back(es + 1); gIdx.push_back(es + 2);
      gIdx.push_back(es); gIdx.push_back(es + 2); gIdx.push_back(es + 3);
    }
  }

  glGenVertexArrays(1, &gObjVao);
  glGenBuffers(1, &gObjVbo);
  glGenBuffers(1, &gObjIbo);
  glBindVertexArray(gObjVao);
  glBindBuffer(GL_ARRAY_BUFFER, gObjVbo);
  glBufferData(GL_ARRAY_BUFFER, gVerts.size() * sizeof(ObjVertex), gVerts.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gObjIbo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, gIdx.size() * sizeof(unsigned int), gIdx.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void *)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void *)(6 * sizeof(float)));
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void *)(9 * sizeof(float)));
  glEnableVertexAttribArray(4);
  glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void *)(10 * sizeof(float)));
  glBindVertexArray(0);
  gObjIndexCount = (int)gIdx.size();
}

// ------------------------------------------------------------------- sky mesh
static const int kDomeRings = 24, kDomeSeg = 32, kDomeRadius = 400;
static void BuildSkyMesh() {
  std::vector<float> verts;
  std::vector<unsigned int> idx;
  for (int r = 0; r <= kDomeRings; r++) {
    float phi = (float)r / kDomeRings * 3.14159265f;
    float cy = std::cos(phi), cr = std::sin(phi);
    for (int s = 0; s <= kDomeSeg; s++) {
      float th = (float)s / kDomeSeg * 3.14159265f * 2.0f;
      float x = cr * std::cos(th), y = cy, z = cr * std::sin(th);
      verts.push_back(x); verts.push_back(y); verts.push_back(z);
    }
  }
  for (int r = 0; r < kDomeRings; r++)
    for (int s = 0; s < kDomeSeg; s++) {
      unsigned int i0 = (unsigned int)(r * (kDomeSeg + 1) + s);
      unsigned int i1 = i0 + (unsigned int)(kDomeSeg + 1);
      idx.push_back(i0); idx.push_back(i0 + 1); idx.push_back(i1);
      idx.push_back(i0 + 1); idx.push_back(i1 + 1); idx.push_back(i1);
    }
  gSkyIndexCount = (int)idx.size();
  glGenVertexArrays(1, &gSkyVao);
  glGenBuffers(1, &gSkyVbo);
  glGenBuffers(1, &gSkyIbo);
  glBindVertexArray(gSkyVao);
  glBindBuffer(GL_ARRAY_BUFFER, gSkyVbo);
  glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gSkyIbo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
  glBindVertexArray(0);
}

static GLuint gHudVao = 0, gHudVbo = 0;
static int gHudVertexFloats = 0;

static void BuildFontAtlas() {
  std::vector<unsigned char> px((size_t)kFontAtlasW * kFontAtlasH * 4);
  FontAtlasFillRGBA(px.data(), px.size());
  glGenTextures(1, &gFontTex);
  glBindTexture(GL_TEXTURE_2D, gFontTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kFontAtlasW, kFontAtlasH, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static void RenderText(float x, float y, const char *text, float scale = 2.0f) {
  static std::vector<float> buf;
  buf.clear();
  float pen = x;
  for (const char *p = text; *p; ++p) {
    unsigned char c = (unsigned char)*p;
    if (!FontAtlasHasGlyph(c)) {
      pen += (float)kFontAtlasCell * scale * 0.75f;
      continue;
    }
    float uv[4];
    FontAtlasGlyphUV(c, uv);
    const float u0 = uv[0], v0 = uv[1], u1 = uv[2], v1 = uv[3];
    const float glyphSize = (float)kFontAtlasCell * scale;
    float x0 = pen, y0 = y, x1 = pen + glyphSize, y1 = y + glyphSize;
    auto push = [&](float px, float py, float u, float v) {
      buf.push_back(px); buf.push_back(py); buf.push_back(u); buf.push_back(v);
    };
    push(x0, y0, u0, v0); push(x1, y0, u1, v0); push(x1, y1, u1, v1);
    push(x0, y0, u0, v0); push(x1, y1, u1, v1); push(x0, y1, u0, v1);
    pen += (float)kFontAtlasCell * scale;
  }
  gHudVertexFloats = (int)buf.size();
  if (!gHudVertexFloats) return;
  glBindBuffer(GL_ARRAY_BUFFER, gHudVbo);
  glBufferData(GL_ARRAY_BUFFER, buf.size() * sizeof(float), buf.data(), GL_DYNAMIC_DRAW);
  glUseProgram(gHudProg.handle);
  glUniform2f(gHudProg.loc("uResolution"), (float)gWindowWidth, (float)gWindowHeight);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, gFontTex);
  glUniform1i(gHudProg.loc("uAtlas"), 0);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glBindVertexArray(gHudVao);
  glDrawArrays(GL_TRIANGLES, 0, gHudVertexFloats / 4);
  glBindVertexArray(0);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
}

static void RenderHUD() {
  char line1[128];
  std::snprintf(line1, sizeof(line1), "FPS: %d   Scene 4   Release 1 v0.4", gFps);
  RenderText(16.0f, 16.0f, line1);
}

static void RenderResults() {
  glViewport(0, 0, gWindowWidth, gWindowHeight);
  glClearColor(0.09f, 0.045f, 0.02f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  char big[96], timeLine[128], hint[96];
  std::snprintf(big, sizeof(big), "SCORE : %.0f", gResultsScore);
  std::snprintf(timeLine, sizeof(timeLine), "Time : %.1fs   Average FPS : %.1f",
                gResultsElapsed, gResultsFps);
  std::snprintf(hint, sizeof(hint), "Scene 4 score");

  float cx = 0.5f * (float)gWindowWidth;
  float cy = 0.5f * (float)gWindowHeight;
  RenderText(cx - (float)std::strlen(big) * 8.0f * 2.0f, cy - 42.0f, big, 4.0f);
  RenderText(cx - (float)std::strlen(timeLine) * 8.0f, cy + 30.0f, timeLine);
  RenderText(cx - (float)std::strlen(hint) * 8.0f, cy + 64.0f, hint);
}

// -------------------------------------------------------------- render passes
static Mat4 gProj;

static void DrawSky(const Mat4 &view, const Vec3 &eye, double timeSec) {
  Mat4 vp;
  Mat4Multiply(vp, gProj, view);
  glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE);
  glUseProgram(gSkyProg.handle);
  glBindVertexArray(gSkyVao);
  glUniformMatrix4fv(gSkyProg.loc("uViewProj"), 1, GL_FALSE, vp.data());
  Mat4 m;
  Mat4Identity(m);
  m[0] = kDomeRadius; m[5] = kDomeRadius; m[10] = kDomeRadius;
  m[12] = eye.x; m[13] = eye.y; m[14] = eye.z;
  glUniformMatrix4fv(gSkyProg.loc("uModel"), 1, GL_FALSE, m.data());
  glUniform3f(gSkyProg.loc("uEyePos"), eye.x, eye.y, eye.z);
  Vec3 sd = SunDirNow(timeSec);
  glUniform3f(gSkyProg.loc("uSunDir"), sd.x, sd.y, sd.z);
  glUniform1f(gSkyProg.loc("uTime"), (float)timeSec);
  glDrawElements(GL_TRIANGLES, gSkyIndexCount, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
  glEnable(GL_CULL_FACE);
  glDepthMask(GL_TRUE);
  glEnable(GL_DEPTH_TEST);
}

static GLuint gShadowVao = 0, gShadowVbo = 0, gShadowIbo = 0;
static void DrawGroundShadows(const Mat4 &view, const Vec3 &eye, double t,
                              const std::vector<PoleSpec> &lineA,
                              const std::vector<PoleSpec> &lineB) {
  if (!gShadowVao) {
    glGenVertexArrays(1, &gShadowVao);
    glGenBuffers(1, &gShadowVbo);
    glGenBuffers(1, &gShadowIbo);
    glBindVertexArray(gShadowVao);
    glBindBuffer(GL_ARRAY_BUFFER, gShadowVbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ObjVertex),
                          (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(ObjVertex),
                          (void *)(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(ObjVertex),
                          (void *)(9 * sizeof(float)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(ObjVertex),
                          (void *)(10 * sizeof(float)));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gShadowIbo);
    glBindVertexArray(0);
  }
  Vec3 sunDir = SunDirNow(t);
  Vec3 sun = Vec3Norm({sunDir.x, 0.0f, sunDir.z});
  Vec3 perp{-sun.z, 0.0f, sun.x};
  const float kAlpha[6] = {0.46f, 0.39f, 0.31f, 0.23f, 0.15f, 0.07f};
  const int kStrips = 5;
  std::vector<ObjVertex> v;
  std::vector<unsigned int> idx;
  v.reserve((lineA.size() + lineB.size()) * kStrips * 4);
  idx.reserve((lineA.size() + lineB.size()) * kStrips * 6);
  for (const std::vector<PoleSpec> *line : {&lineA, &lineB}) {
    for (const PoleSpec &p : *line) {
      float reach = 16.0f * (1.0f + 0.05f * (float)((int(p.z) % 7)));
      Vec3 b{p.x, kShadowY, p.z};
      for (int k = 0; k < kStrips; k++) {
        float t0 = (float)k / kStrips, t1 = (float)(k + 1) / kStrips;
        Vec3 c0 = Vec3Add(b, Vec3Scale(sun, -reach * t0));
        Vec3 c1 = Vec3Add(b, Vec3Scale(sun, -reach * t1));
        float w0 = 0.30f + 0.95f * t0, w1 = 0.30f + 0.95f * t1;
        float a0 = kAlpha[k], a1 = kAlpha[k + 1];
        unsigned int s = (unsigned int)v.size();
        v.push_back({c0.x + perp.x * w0, kShadowY, c0.z + perp.z * w0, 0, 1, 0,
                     0, 0, 0, kMatShadow, a0});
        v.push_back({c0.x - perp.x * w0, kShadowY, c0.z - perp.z * w0, 0, 1, 0,
                     0, 0, 0, kMatShadow, a0});
        v.push_back({c1.x - perp.x * w1, kShadowY, c1.z - perp.z * w1, 0, 1, 0,
                     0, 0, 0, kMatShadow, a1});
        v.push_back({c1.x + perp.x * w1, kShadowY, c1.z + perp.z * w1, 0, 1, 0,
                     0, 0, 0, kMatShadow, a1});
        idx.push_back(s); idx.push_back(s + 1); idx.push_back(s + 2);
        idx.push_back(s); idx.push_back(s + 2); idx.push_back(s + 3);
      }
    }
  }
  // stream the buffers (VAO unbound: buffer bindings here are global)
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, gShadowVbo);
  glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(ObjVertex), v.data(),
               GL_STREAM_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gShadowIbo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int),
               idx.data(), GL_STREAM_DRAW);

  glBindVertexArray(gShadowVao);
  glUseProgram(gObjProg.handle);
  Mat4 vp;
  Mat4Multiply(vp, gProj, view);
  Mat4 model;
  Mat4Identity(model);
  glDisable(GL_CULL_FACE);
  glUniformMatrix4fv(gObjProg.loc("uViewProj"), 1, GL_FALSE, vp.data());
  glUniformMatrix4fv(gObjProg.loc("uModel"), 1, GL_FALSE, model.data());
  glUniform3f(gObjProg.loc("uEyePos"), eye.x, eye.y, eye.z);
  glUniform3f(gObjProg.loc("uSunDir"), sunDir.x, sunDir.y, sunDir.z);
  glUniform1f(gObjProg.loc("uTime"), (float)t);
  glUniform1f(gObjProg.loc("uRoadX"), kRoadX);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);  
  glDrawElements(GL_TRIANGLES, (GLsizei)idx.size(), GL_UNSIGNED_INT, nullptr);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glBindVertexArray(0);
}

static void DrawGrid(const Mat4 &view, const Vec3 &eye, double timeSec) {
  (void)timeSec;
  Mat4 vp;
  Mat4Multiply(vp, gProj, view);
  Mat4 model;
  Mat4Identity(model);
  glUseProgram(gObjProg.handle);
  glBindVertexArray(gObjVao);
  glDisable(GL_CULL_FACE);
  glUniformMatrix4fv(gObjProg.loc("uViewProj"), 1, GL_FALSE, vp.data());
  glUniformMatrix4fv(gObjProg.loc("uModel"), 1, GL_FALSE, model.data());
  glUniform3f(gObjProg.loc("uEyePos"), eye.x, eye.y, eye.z);
  Vec3 sd = SunDirNow(timeSec);
  glUniform3f(gObjProg.loc("uSunDir"), sd.x, sd.y, sd.z);
  glUniform1f(gObjProg.loc("uTime"), (float)timeSec);
  glUniform1f(gObjProg.loc("uRoadX"), kRoadX);
  glDrawElements(GL_TRIANGLES, gObjIndexCount, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
  glEnable(GL_CULL_FACE);
}

static void WriteScreenshotPPM(const char *path);

static void RenderScene() {
  double now = NowSeconds();

  if (gResultsShown) {
    RenderResults();
    SDL_GL_SwapWindow(gWindow);
    if (now - gResultsShownAt >= kResultsScreenSeconds) {
      if (gStandaloneScene) {
        SDL_Quit();
        std::exit(0);
      }
      gFusedDone = true;
    }
    return;
  }

  static double lastFrame = -1.0;
  if (lastFrame < 0.0) lastFrame = now;
  double dt = now - lastFrame;
  lastFrame = now;
  if (dt > 0.1) dt = 0.1; // clamp hitches
  gSimTime += dt;

  float t = (float)gSimTime;
  if (gAutoCam) UpdateAutoCamera(t);
  Vec3 eye = gAutoCam ? gCamPos : OrbitCamPos();

  Mat4 view;
  {
    Vec3 look{-0.45f, 5.5f, gCamPos.z + 26.0f};
    Mat4LookAt(view, eye, look, {0, 1, 0});
  }
  float aspect = (float)gWindowWidth / (float)gWindowHeight;
  Mat4Perspective(gProj, 52.0f, aspect, 0.1f, 900.0f);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, gWindowWidth, gWindowHeight);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  DrawGrid(view, eye, gSimTime);
  DrawGroundShadows(view, eye, gSimTime, gLineA, gLineB);
  DrawSky(view, eye, gSimTime);
  RenderHUD();

  if (gScreenshotPath && gNextShot < gShotTimes.size() &&
      gSimTime >= (double)gShotTimes[gNextShot]) {
    WriteScreenshotPPM(gScreenshotPath);
    gNextShot++;
    if (gNextShot >= gShotTimes.size()) {
      SDL_Quit();
      std::exit(0);
    }
  }

  SDL_GL_SwapWindow(gWindow);

  gFrame++;
  gFrameAccum++;
  if (now - gFpsTimer >= 1.0) {
    double inst = gFrameAccum / (now - gFpsTimer);
    gFps = (int)std::lround(inst);
    gFrameAccum = 0;
    gFpsTimer = now;
    char title[256];
    std::snprintf(title, sizeof(title), "%s - FPS : %d", gName, gFps);
    SDL_SetWindowTitle(gWindow, title);
  }
  if (gShotTimes.empty() && (now - gStartTime) * 1000.0 >= 45000.0) {
    double elapsed = now - gStartTime;
    double fps = (double)gFrame / elapsed;
    double score = fps * fps * 2.0;
    std::printf("Benchmark Results - Time : %.1fs, Average FPS : %.1f, Score : %.0f\n",
                elapsed, fps, score);
    std::fflush(stdout);
    gFusedPoleScore = score;
    gResultsElapsed = elapsed;
    gResultsFps = fps;
    gResultsScore = score;
    gResultsShownAt = now;
    gResultsShown = true;
  }
}

static void ProcessKeys(const SDL_Event &event) {
  if (event.key.keysym.sym == SDLK_ESCAPE) {
    gQuit = true;
  }
}

static void ChangeSize(int w, int h) {
  if (h == 0) h = 1;
  gWindowWidth = w;
  gWindowHeight = h;
  glViewport(0, 0, w, h);
}

static bool ParseShotTimes(const char *arg) {
  gShotTimes.clear();
  const char *p = arg;
  while (*p) {
    char *end = nullptr;
    double v = std::strtod(p, &end);
    if (end == p) return false;
    gShotTimes.push_back((float)v);
    p = end;
    if (*p == ',') p++;
  }
  return !gShotTimes.empty();
}

int PoleSceneParseArgs(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--shot-times") && i + 1 < argc) {
      if (!ParseShotTimes(argv[++i])) {
        std::fprintf(stderr, "Bad --shot-times list: %s\n", argv[i]);
        return EXIT_FAILURE;
      }
    } else if (!std::strcmp(argv[i], "--width") && i + 1 < argc) {
      gWindowWidthOverride = std::atoi(argv[++i]);
    }
  }
  return EXIT_SUCCESS;
}

void PoleSceneSetScreenshot(const char *path) { gScreenshotPath = path; }
void PoleSceneSetShotTime(float t) {
  if (gShotTimes.empty()) gShotTimes.push_back(t);
}
void PoleSceneSetStandalone(bool standalone) { gStandaloneScene = standalone; }

static void WriteScreenshotPPM(const char *path) {
  const int w = gWindowWidth, h = gWindowHeight;
  std::vector<unsigned char> rgb((size_t)w * h * 3);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
  FILE *f = std::fopen(path, "wb");
  if (!f) {
    std::fprintf(stderr, "Cannot write screenshot %s\n", path);
    return;
  }
  std::fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (int y = h - 1; y >= 0; y--)
    std::fwrite(&rgb[(size_t)y * w * 3], 1, (size_t)w * 3, f);
  std::fclose(f);
  std::printf("Screenshot written: %s\n", path);
  std::fflush(stdout);
}

// ------------------------------------------------------------------- setup
static void Setup() {
  BuildSkyMesh();
  BuildSceneGeometry();
  BuildFontAtlas();

  glGenVertexArrays(1, &gHudVao);
  glGenBuffers(1, &gHudVbo);
  glBindVertexArray(gHudVao);
  glBindBuffer(GL_ARRAY_BUFFER, gHudVbo);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
  glBindVertexArray(0);

  gSkyProg = LinkProgram(resolveAssetPath("shaders/pole/sky_vert.glsl").c_str(),
                         resolveAssetPath("shaders/pole/sky_frag.glsl").c_str());
  gObjProg = LinkProgram(resolveAssetPath("shaders/pole/object_vert.glsl").c_str(),
                         resolveAssetPath("shaders/pole/object_frag.glsl").c_str());
  gHudProg = LinkProgram(resolveAssetPath("shaders/ps14/hud_vert.glsl").c_str(),
                         resolveAssetPath("shaders/pole/hud_frag.glsl").c_str());

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glClearColor(0.60f, 0.30f, 0.10f, 1.0f);
}

int RunPoleScene(bool *gaveUpOut) {
  if (gaveUpOut) *gaveUpOut = false;

  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    std::fprintf(stderr, "SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
    return EXIT_FAILURE;
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  int winW = WIDTH, winH = HEIGHT;
  if (gWindowWidthOverride > 0) {
    winW = gWindowWidthOverride;
    winH = (gWindowWidthOverride * HEIGHT + WIDTH / 2) / WIDTH;
  }
  gWindowWidth = winW;
  gWindowHeight = winH;
  bool msaa = true;
  for (int attempt = 0; attempt < 2; attempt++) {
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, msaa ? 1 : 0);
    SDL_GL_SetAttribute(SDL_GL_SAMPLES, msaa ? 4 : 0);
    gWindow = SDL_CreateWindow(gName, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                               winW, winH,
                               SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
                               | (gWindowedMode ? 0u : SDL_WINDOW_FULLSCREEN_DESKTOP));
    if (!gWindow) {
      std::fprintf(stderr, "Window could not be created! SDL_Error: %s\n", SDL_GetError());
      return EXIT_FAILURE;
    }
    gContext = SDL_GL_CreateContext(gWindow);
    if (gContext) break;
    SDL_DestroyWindow(gWindow);
    gWindow = nullptr;
    msaa = false;
  }
  if (!gContext) {
    std::printf("Power lines scene: OpenGL 3.3 core context unavailable — skipping this scene\n");
    std::fflush(stdout);
    SDL_Quit();
    if (gaveUpOut) *gaveUpOut = true;
    return 1;
  }
  if (!gWindowedMode) {
    int dw = gWindowWidth, dh = gWindowHeight;
    SDL_GetWindowSize(gWindow, &dw, &dh);
    if (dw > 0 && dh > 0) {
      gWindowWidth = dw;
      gWindowHeight = dh;
    }
  }
  glViewport(0, 0, gWindowWidth, gWindowHeight);
  int samples = 0;
  SDL_GL_GetAttribute(SDL_GL_SAMPLES, &samples);
  std::printf("Antialiasing: %dx MSAA%s\n", samples, samples > 1 ? "" : " (unavailable)");
  std::printf("Display: %s %dx%d | capture with Win+PrtScr, or --screenshot FILE\n",
              gWindowedMode ? "windowed" : "borderless fullscreen",
              gWindowWidth, gWindowHeight);
  std::fflush(stdout);
  SDL_GL_SetSwapInterval(0);

  if (glewInit() != GLEW_OK) {
    std::fprintf(stderr, "glewInit failed\n");
    return EXIT_FAILURE;
  }

  std::printf("Renderer: %s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

  Setup();

  gStartTime = NowSeconds();
  gFpsTimer = gStartTime;
  gSimTime = 0.0;

  SDL_Event event;
  while (!gQuit && !gFusedDone) {
    while (SDL_PollEvent(&event) != 0) {
      if (event.type == SDL_QUIT) {
        gQuit = true;
      } else if (event.type == SDL_KEYDOWN) {
        ProcessKeys(event);
      } else if (event.type == SDL_WINDOWEVENT) {
        if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
          ChangeSize(event.window.data1, event.window.data2);
        }
      }
    }
    RenderScene();
  }

  SDL_GL_DeleteContext(gContext);
  SDL_DestroyWindow(gWindow);
  SDL_Quit();
  if (gQuit) return 2;
  return 0;
}
