// ElectroBench — scene 4 of the single ElectroBench binary: the "power lines"
// scene. A late-afternoon Japanese suburb memory: orange sky, white drifting
// clouds, and a tangle of utility poles, crossarms, insulators, transformers
// and sagging wires receding into the heat haze — the serial-experiments
// mood, built entirely from analytic geometry (no model files, no textures:
// every cylinder, catenary and quad is generated on the CPU at startup).
//
// Like src/tidebench.cxx and src/pool.cxx, this translation unit is NOT a
// program of its own. It exports RunPoleScene(), which main.cxx calls as the
// fourth scene of the one and only ElectroBench executable.
//
// What is rendered:
//   * Orange sky dome (shaders/pole/sky_*.glsl): deep amber zenith through
//     peach into a hazy cream horizon, a veiled low sun bloom, and two
//     parallax layers of smooth white cumulus drifting on the wind. No
//     textures — two octaves of value noise, carved into puffy cells.
//   * The grid (shaders/pole/object_*.glsl): creosote utility poles on two
//     receding lines, crossarms with ceramic insulators, a transformer can,
//     and ~30 catenary wires (real sag: y = midpoint + cosh falloff) built
//     as swept tubes. One warm wrap-lighting shader for everything, with
//     aerial perspective sinking the far poles into the haze.
//   * The camera dollies along the line under the wires (auto mode), or
//     orbits with the usual mouse/keys.
//
// Controls: drag orbits the camera, wheel zooms, F toggles the auto camera,
// ESC quits. R has no meaning here (nothing falls).
//
// Headless flags shared with the other scenes: --screenshot, --shot-times,
// --width; scene-specific: --pole-only (run just this scene).

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

#include "../lib/asset_path.hxx"
#include "font_atlas.hxx" // shared HUD font data and atlas layout

// --------------------------------------------------------------- math block
// Same minimal helpers the pool scene carries (kept local: scene modules do
// not share a math header on purpose — each scene owns its own).
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
static const char *const gName = "ElectroBench - Power Lines";
static const int WIDTH = 1280, HEIGHT = 720;
static const float kGroundY = 0.0f;
// dusk sun: LOW and AHEAD of the dolly camera (up the corridor), slightly
// left — wires cross the sun disc, poles read as silhouettes, long shadows
// come back toward the viewer. BUILD-P2 hyper-realism pass.
// BUILD-P6: kSunDir is the T=0 state; SunDirNow() advances azimuth/elevation
// with the sim clock so the sky, lighting and shadows all move together.
static const Vec3 kSunDir{-0.30f, 0.20f, 0.93f};

// BUILD-P6 MOVING SUN: the dusk sun crawls azimuthally and sinks over the
// run (45 s bench = a visible slow sunset). Everything downstream follows:
// the sky bloom, the object lighting AND the ground shadows. Sinking is
// clamped so the sun never fully sets inside the bench window.
static Vec3 SunDirNow(double t) {
  float az = -0.30f - 0.004f * (float)t;                // ~10 deg over the run
  float el = 0.20f - 0.0011f * (float)t;                // slow sink (2.7 deg
                                                        // over the 45 s run)
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

// headless visual-test state (same flags as the other scenes)
static const char *gScreenshotPath = nullptr;
static std::vector<float> gShotTimes;
static size_t gNextShot = 0;
static int gWindowWidthOverride = 0;

// ------------------------------------------------------------------- camera
static bool gAutoCam = true;
static Vec3 gCamPos{0.0f, 2.0f, 4.0f};
static float gCamYaw = 0.0f, gCamPitch = -0.06f, gCamDist = 7.0f;
static float gXOld = 0.0f, gYOld = 0.0f;
static bool gIsHoldingMouse = false;

static void UpdateAutoCamera(float t) {
  // Dolly along the line: the camera walks the gravel path beside the poles,
  // the wire catenaries sweeping overhead pole after pole. Wrap around so
  // the 45 s bench never leaves the grid.
  const float span = 161.0f;   // 13 spans of the 15-pole corridor
  float z = 4.0f + std::fmod(t * 2.6f, span);
  // BUILD-P5: the camera drives the road's right lane (the road is centred
  // at x = +2.6), hugging the centreline so the poles stream past on the left
  gCamPos = {2.6f + std::sin(t * 0.05f) * 0.35f,         // gentle weave
             1.9f + 0.22f * std::sin(t * 0.11f),         // breathing height
             z};
  // BUILD-P2: aim up the corridor with the LOW SUN sitting on the horizon —
  // poles cross it as dark silhouettes and wires string straight over it
  // (the money shot of the serial-experiments look).
  Vec3 target{-0.45f, 5.5f, z + 26.0f};
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

// ------------------------------------------------------------------- the grid
// Per-vertex colour geometry: position(3) + normal(3) + colour(3).
struct ObjVertex { float x, y, z, nx, ny, nz, r, g, b; };
static std::vector<ObjVertex> gVerts;
static std::vector<unsigned int> gIdx;

static const Vec3 kWoodDark{0.165f, 0.115f, 0.085f};   // creosote pole
static const Vec3 kWoodOld{0.230f, 0.180f, 0.140f};   // weathered crossarm
static const Vec3 kCeramic{0.780f, 0.760f, 0.700f};   // insulator glaze
static const Vec3 kCable{0.055f, 0.050f, 0.055f};    // rubber wire
static const Vec3 kCableOld{0.085f, 0.075f, 0.070f};
static const Vec3 kMetal{0.190f, 0.195f, 0.200f};    // transformer can
static const Vec3 kGravel{0.520f, 0.420f, 0.310f};   // warm dirt road

static void PushVert(const Vec3 &p, const Vec3 &n, const Vec3 &c) {
  gVerts.push_back({p.x, p.y, p.z, n.x, n.y, n.z, c.x, c.y, c.z});
}

// Cylinder between two points (solid, capped): the pole trunk, insulators,
// the transformer can, stray posts.
static void AddCylinder(const Vec3 &base, const Vec3 &top, float rBase, float rTop,
                        int segs, const Vec3 &color) {
  Vec3 axis = Vec3Norm(Vec3Sub(top, base));
  Vec3 helper = std::abs(axis.y) > 0.9f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
  Vec3 s = Vec3Norm(Vec3Cross(axis, helper));
  Vec3 u = Vec3Cross(axis, s);
  unsigned int start = (unsigned int)gVerts.size();
  for (int i = 0; i <= segs; i++) {
    float a = (float)i / segs * 6.2831853f;
    Vec3 dir = Vec3Add(Vec3Scale(s, std::cos(a)), Vec3Scale(u, std::sin(a)));
    Vec3 n = dir;
    PushVert(Vec3Add(base, Vec3Scale(dir, rBase)), n, color);
    PushVert(Vec3Add(top, Vec3Scale(dir, rTop)), n, color);
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
    PushVert(c, n, color);
    for (int i = 0; i <= segs; i++) {
      float a = (float)i / segs * 6.2831853f;
      Vec3 dir = Vec3Add(Vec3Scale(s, std::cos(a)), Vec3Scale(u, std::sin(a)));
      PushVert(Vec3Add(c, Vec3Scale(dir, r)), n, color);
    }
    for (int i = 0; i < segs; i++) {
      unsigned int a = ci + 1 + (unsigned int)i;
      if (cap) { gIdx.push_back(ci); gIdx.push_back(a); gIdx.push_back(a + 1); }
      else     { gIdx.push_back(ci); gIdx.push_back(a + 1); gIdx.push_back(a); }
    }
  }
}

static void AddWire(const Vec3 &a, const Vec3 &b, float sag, float radius,
                    int samples, const Vec3 &color);

// BUILD-P4 TELECOM BUNDLE: a communication cable sags between its two pole
// brackets, and a bundle of thin DROP WIRES peels off along the span — the
// drippy ''telephone lines going everywhere'' of every Japanese street.
// Deterministic (hash of the span index) like everything else in the bench.
static void AddTelecomBundle(const Vec3 &a, const Vec3 &b, int seed,
                             float radius, int samples, const Vec3 &color) {
  AddWire(a, b, 0.55f + 0.10f * ((seed * 7) % 3), radius, samples, color);
  int drops = 4 + (seed % 3);                    // 4-6 drop wires per span
  for (int i = 0; i < drops; i++) {
    float t = 0.18f + 0.62f * (float)((seed * 13 + i * 29) % 100) / 100.0f;
    float drop = 0.35f + 0.55f * (float)((seed * 17 + i * 41) % 100) / 100.0f;
    Vec3 p{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t - drop,
           a.z + (b.z - a.z) * t};
    Vec3 q{p.x, p.y - 1.15f - 0.9f * (float)((seed * 23 + i * 13) % 100) / 100.0f,
           p.z};
    AddWire(p, q, 0.08f, radius * 0.55f, 5, color);
  }
}

// A sagging wire between two attachment points: a real catenary sampled as a
// swept tube (the Lain look is ALL about the droop of these cables).
static void AddWire(const Vec3 &a, const Vec3 &b, float sag, float radius,
                    int samples, const Vec3 &color) {
  Vec3 delta = Vec3Sub(b, a);
  float len = Vec3Len(delta);
  if (len < 1e-3f) return;
  Vec3 dir = Vec3Scale(delta, 1.0f / len);
  Vec3 side = Vec3Norm(Vec3Cross(dir, Vec3{0, 1, 0}));
  if (Vec3Len(side) < 1e-4f) side = Vec3{1, 0, 0};
  Vec3 up = Vec3Norm(Vec3Cross(side, dir));
  float halfSpan = len * 0.5f;

  std::vector<Vec3> centres(samples + 1);
  for (int i = 0; i <= samples; i++) {
    float t = (float)i / samples;              // 0..1 along the span
    // catenary: cosh(x/a) shape approximated by the standard sag parabola
    // plus a cosh tail — visually indistinguishable at wire radii.
    float x = (t - 0.5f) * len;
    float y = sag * (1.0f - 4.0f * (t - 0.5f) * (t - 0.5f));
    float tail = sag * 0.06f * ((float)std::cosh(x / (halfSpan * 0.72f)) - 1.0f)
               / (float)std::cosh(halfSpan / (halfSpan * 0.72f));
    Vec3 c = Vec3Add(Vec3Add(a, Vec3Scale(dir, x)), Vec3Scale(up, -y - tail));
    centres[i] = c;
  }
  unsigned int start = (unsigned int)gVerts.size();
  for (int i = 0; i <= samples; i++) {
    Vec3 t0 = centres[i < samples ? i + 1 : i];
    Vec3 t1 = centres[i > 0 ? i - 1 : i];
    Vec3 tang = Vec3Norm(Vec3Sub(t0, t1));
    Vec3 s2 = Vec3Norm(Vec3Cross(tang, Vec3{0, 1, 0}));
    if (Vec3Len(s2) < 1e-4f) s2 = side;
    Vec3 u2 = Vec3Norm(Vec3Cross(s2, tang));
    for (int k = 0; k < 4; k++) {
      float a2 = (float)k * 1.5707963f;
      Vec3 n = Vec3Add(Vec3Scale(s2, std::cos(a2)), Vec3Scale(u2, std::sin(a2)));
      PushVert(Vec3Add(centres[i], Vec3Scale(n, radius)), n, color);
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
static void AddBox(const Vec3 &center, const Vec3 &half, const Vec3 &color) {
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
    for (int k = 0; k < 4; k++) PushVert(c[quads[q][k]], norms[q], color);
    gIdx.push_back(s); gIdx.push_back(s + 1); gIdx.push_back(s + 2);
    gIdx.push_back(s); gIdx.push_back(s + 2); gIdx.push_back(s + 3);
  }
}

// One utility pole: trunk, two crossarms with diagonal braces, insulators,
// an earth wire down the trunk, optional transformer + service spool.
// BUILD-P2: every wire in the scene ties to a REAL insulator top — these
// two helpers are the single source of truth for the attachment points.
// BUILD-P6: poles LEAN (leanX/leanZ = total top displacement); every
// attachment point interpolates the same linear axis, so wires stay tied
// to slightly crooked poles like real streets.
struct PoleSpec {
  float x, z; float height; bool transformer; bool serviceSpool;
  float leanX, leanZ;
};
static Vec3 PoleAxisAt(const PoleSpec &p, float h) {
  float f = h / p.height;
  return {p.x + p.leanX * f, h, p.z + p.leanZ * f};
}
static Vec3 ArmInsulatorTop(const PoleSpec &p, float off) {
  // crossarm at height-0.55; its insulator stacks top out 0.35 above it
  Vec3 a = PoleAxisAt(p, p.height - 0.20f);
  return {a.x + off, p.height - 0.20f, a.z};
}
static Vec3 PoleTopInsulatorTop(const PoleSpec &p) {
  Vec3 a = PoleAxisAt(p, p.height + 0.24f);
  return {a.x, p.height + 0.24f, a.z};
}

static std::vector<PoleSpec> gLineA, gLineB;  // kept for per-frame shadows
static void AddPole(const PoleSpec &p) {
  Vec3 base = PoleAxisAt(p, kGroundY);
  Vec3 top = PoleAxisAt(p, p.height);
  AddCylinder(base, top, 0.17f, 0.115f, 10, kWoodDark);
  // dirt collar kicked up around the base (every real pole sits in one)
  AddCylinder({base.x, kGroundY - 0.02f, base.z}, {base.x, 0.10f, base.z},
              0.52f, 0.34f, 8, {0.30f, 0.24f, 0.18f});

  // main crossarm near the top + a smaller one below, with diagonal braces
  float armY = p.height - 0.55f;
  Vec3 armC = PoleAxisAt(p, armY);
  AddBox({armC.x, armY, armC.z}, {1.25f, 0.055f, 0.075f}, kWoodOld);
  Vec3 arm2C = PoleAxisAt(p, armY - 0.62f);
  AddBox({arm2C.x, armY - 0.62f, arm2C.z}, {0.85f, 0.05f, 0.07f}, kWoodOld);
  Vec3 brT = PoleAxisAt(p, armY - 0.05f);
  Vec3 brB = PoleAxisAt(p, armY - 0.57f);
  AddCylinder({brT.x - 0.34f, brT.y, brT.z + 0.03f},
              {brB.x - 0.94f, brB.y, brB.z + 0.03f}, 0.030f, 0.030f, 6,
              kWoodDark);
  AddCylinder({brT.x + 0.34f, brT.y, brT.z + 0.03f},
              {brB.x + 0.94f, brB.y, brB.z + 0.03f}, 0.030f, 0.030f, 6,
              kWoodDark);

  // ceramic insulators: three on the main arm, one atop the pole
  for (float off : {-1.05f, 0.0f, 1.05f}) {
    Vec3 ib{armC.x + off, armY + 0.05f, armC.z};
    AddCylinder(ib, Vec3Add(ib, Vec3{0, 0.24f, 0}), 0.052f, 0.062f, 8, kCeramic);
    AddCylinder(Vec3Add(ib, Vec3{0, 0.24f, 0}), Vec3Add(ib, Vec3{0, 0.30f, 0}),
                0.062f, 0.040f, 8, kCeramic);
  }
  AddCylinder(top, Vec3Add(top, Vec3{0, 0.18f, 0}), 0.05f, 0.058f, 8, kCeramic);
  AddCylinder(Vec3Add(top, Vec3{0, 0.18f, 0}), Vec3Add(top, Vec3{0, 0.24f, 0}),
              0.058f, 0.038f, 8, kCeramic);

  // earth wire: a bare cable clipped down the trunk, grounded at the collar
  AddWire({base.x + 0.115f, 0.12f, base.z}, {top.x + 0.085f, 4.0f, top.z},
          0.05f, 0.014f, 6, kMetal);

  if (p.serviceSpool) {
    // secondary service spool on the other flank (double-attachment poles)
    AddCylinder({p.x - 0.24f, 5.4f, p.z}, {p.x - 0.34f, 5.4f, p.z}, 0.05f,
                0.05f, 6, kMetal);
  }

  // BUILD-P4 TELECOM ARM: a second, lower crossarm carrying the phone/cable
  // bundles (Japanese poles stack a communications arm under the power arm).
  float telY = armY - 1.30f;
  Vec3 telC = PoleAxisAt(p, telY);
  AddBox({telC.x, telY, telC.z}, {0.95f, 0.05f, 0.06f}, kWoodOld);
  for (float off : {-0.70f, 0.0f, 0.70f})
    AddCylinder({telC.x + off, telY + 0.05f, telC.z},
                {telC.x + off, telY + 0.15f, telC.z}, 0.038f, 0.032f, 6, kMetal);

  // a couple of CableTV-style cylindrical boxes bolted to the trunk (some
  // poles, deterministic)
  if (((int(p.z * 7.0f)) % 3) == 0)
    AddCylinder({base.x + 0.20f, 3.9f, base.z}, {base.x + 0.20f, 4.5f, base.z},
                0.11f, 0.11f, 8, kMetal);

  if (p.transformer) {
    // the can: grey cylinder + cooling fins, bolted below the crossarm
    Vec3 tc{armC.x + 0.62f, armY - 1.35f, armC.z};
    AddCylinder(Vec3Add(tc, Vec3{-0.1f, -0.55f, 0}),
                Vec3Add(tc, Vec3{0.1f, 0.55f, 0}), 0.34f, 0.34f, 10, kMetal);
    AddBox({tc.x, tc.y + 0.30f, tc.z}, {0.40f, 0.16f, 0.16f}, kMetal);
    AddBox({tc.x, tc.y - 0.34f, tc.z}, {0.10f, 0.22f, 0.10f}, kMetal);
    // two ceramic bushings on the can's crown + their drop leads
    for (float bz : {-0.12f, 0.12f}) {
      Vec3 bt{tc.x, tc.y + 0.46f, tc.z + bz};
      AddCylinder(bt, Vec3Add(bt, Vec3{0, 0.16f, 0}), 0.045f, 0.038f, 6,
                  kCeramic);
      AddWire(Vec3Add(bt, Vec3{0, 0.18f, 0}), {tc.x, armY - 0.30f, tc.z + bz},
              0.08f, 0.011f, 5, kCable);
    }
  }
}

// BUILD-P6: suburban silhouette houses on both flanks — gabled roof boxes
// with dark window holes, the depth cue that kills the "empty flat world"
// look. Deterministic sizes/positions; drawn cheap (one box + one prism).
static void AddHouse(float x, float z, float w, float d, float h, float yaw) {
  // body: a simple box (axis-aligned; yaw only skews the roof ridge)
  AddBox({x, h * 0.5f, z}, {w * 0.5f, h * 0.5f, d * 0.5f},
         {0.34f, 0.26f, 0.20f});
  // gabled roof: two long slabs meeting at a ridge along the x axis
  Vec3 ridge{0.36f, 0.24f, 0.17f};
  AddCylinder({x - w * 0.5f, h, z}, {x + w * 0.5f, h, z}, 0.02f, 0.02f, 4,
              ridge);                                     // ridge beam
  AddBox({x, h + 0.22f, z - d * 0.28f}, {w * 0.55f, 0.05f, d * 0.34f}, ridge);
  AddBox({x, h + 0.22f, z + d * 0.28f}, {w * 0.55f, 0.05f, d * 0.34f}, ridge);
  // dark windows on the street-facing flank
  Vec3 win{0.045f, 0.04f, 0.05f};
  AddBox({x - w * 0.22f, h * 0.55f, z + (yaw >= 0.0f ? d * 0.5f : -d * 0.5f)},
         {0.28f, 0.22f, 0.02f}, win);
  AddBox({x + w * 0.18f, h * 0.55f, z + (yaw >= 0.0f ? d * 0.5f : -d * 0.5f)},
         {0.28f, 0.22f, 0.02f}, win);
}

static void BuildSceneGeometry() {
  gVerts.clear();
  gIdx.clear();
  gLineA.clear();
  gLineB.clear();

  // ---- ground: a big warm gravel plane (single quad, cheap as dirt).
  // BUILD-P6: much longer along +z so the corridor never shows its edge.
  {
    unsigned int s = (unsigned int)gVerts.size();
    Vec3 n{0, 1, 0};
    PushVert({-70, kGroundY, -60}, n, kGravel);
    PushVert({70, kGroundY, -60}, n, kGravel);
    PushVert({70, kGroundY, 220}, n, kGravel);
    PushVert({-70, kGroundY, 220}, n, kGravel);
    gIdx.push_back(s); gIdx.push_back(s + 1); gIdx.push_back(s + 2);
    gIdx.push_back(s); gIdx.push_back(s + 2); gIdx.push_back(s + 3);
  }

  // ---- the main pole line the camera walks beside (BUILD-P2: 15 poles,
  // tighter 12.4 m spacing — real suburban distribution is 10-14 m spans,
  // and the long vanishing corridor IS the Lain look). BUILD-P6: every pole
  // leans a little (deterministic), like real weathered streets.
  std::vector<PoleSpec> lineA;
  for (int i = 0; i < 15; i++)
    lineA.push_back({-3.4f, 2.0f + 12.4f * i, 8.6f + 0.35f * ((i * 5) % 3),
                     i == 1 || i == 6 || i == 11, i == 3 || i == 9,
                     0.10f * ((i * 7) % 3 - 1), 0.08f * ((i * 5) % 3 - 1)});
  for (const PoleSpec &p : lineA) AddPole(p);
  gLineA = lineA;

  // wires along line A: 3 crossarm conductors + the pole-top wire. BUILD-P2
  // FIX: every span ties INSULATOR TOP to INSULATOR TOP (ArmInsulatorTop /
  // PoleTopInsulatorTop) — the old below-arm offsets left wire ends hanging
  // in mid-air beside the insulators.
  for (int i = 0; i + 1 < (int)lineA.size(); i++) {
    const PoleSpec &p = lineA[i];
    const PoleSpec &q = lineA[i + 1];
    float sag = 0.78f + 0.12f * ((i * 3) % 3);
    for (float off : {-1.05f, 0.0f, 1.05f})
      AddWire(ArmInsulatorTop(p, off), ArmInsulatorTop(q, off), sag, 0.028f,
              14, kCable);
    AddWire(PoleTopInsulatorTop(p), PoleTopInsulatorTop(q), sag * 0.8f, 0.032f,
            14, kCableOld);
  }

  // ---- a second, closer line: depth + the layered-tangle feel. BUILD-P2:
  // the 17 m offset put line B so far off-axis it read as flat wallpaper;
  // 8 m puts a real second plane of poles in frame. Every wire ties
  // insulator-top to insulator-top (the old wires floated mid-air — the
  // "wires hanging out" glitch).
  std::vector<PoleSpec> lineB;
  for (int i = 0; i < 6; i++)
    lineB.push_back({8.6f, 6.0f + 15.5f * i, 7.6f, false, i == 1,
                     0.09f * ((i * 11) % 3 - 1), 0.07f * ((i * 3) % 3 - 1)});
  for (const PoleSpec &p : lineB) AddPole(p);
  gLineB = lineB;
  for (int i = 0; i + 1 < (int)lineB.size(); i++) {
    const PoleSpec &p = lineB[i];
    const PoleSpec &q = lineB[i + 1];
    for (float off : {-0.85f, 0.85f})
      AddWire(ArmInsulatorTop(p, off), ArmInsulatorTop(q, off), 0.88f, 0.026f,
              12, kCableOld);
  }

  // ---- the crossing spans: line B feeds into line A (the tangle). BUILD-P2
  // FIX: crossings now land on real insulator tops of both poles.
  for (int i = 0; i < 4; i++) {
    const PoleSpec &p = lineB[i];
    const PoleSpec &q = lineA[i + 1];
    AddWire(ArmInsulatorTop(p, 0.85f), ArmInsulatorTop(q, -1.05f), 1.30f,
            0.024f, 16, kCable);
  }

  // ---- BUILD-P4: THE TELECOM TANGLE. Two bundles per span on line A (one
  // per bracket pair) plus one on line B, and cross-line telecom spans
  // B->A — this is what makes a Japanese pole street read as a Japanese
  // pole street: tons and tons of sagging phone wire everywhere.
  for (int i = 0; i + 1 < (int)lineA.size(); i++) {
    const PoleSpec &p = lineA[i];
    const PoleSpec &q = lineA[i + 1];
    float telY = p.height - 1.85f;
    float telYq = q.height - 1.85f;
    AddTelecomBundle({p.x - 0.70f, telY, p.z}, {q.x - 0.70f, telYq, q.z},
                     i * 2 + 1, 0.022f, 12, kCable);
    AddTelecomBundle({p.x + 0.70f, telY, p.z}, {q.x + 0.70f, telYq, q.z},
                     i * 2 + 2, 0.022f, 12, kCable);
  }
  for (int i = 0; i + 1 < (int)lineB.size(); i++) {
    const PoleSpec &p = lineB[i];
    const PoleSpec &q = lineB[i + 1];
    AddTelecomBundle({p.x, p.height - 1.85f, p.z}, {q.x, q.height - 1.85f, q.z},
                     i * 3 + 40, 0.020f, 10, kCableOld);
  }
  // slack cross-line telecom loops B -> A (the messy diagonal drips)
  for (int i = 0; i < 5; i++) {
    const PoleSpec &p = lineB[i];
    const PoleSpec &q = lineA[i + 1];
    AddTelecomBundle({p.x - 0.35f, p.height - 1.85f, p.z},
                     {q.x - 0.70f, q.height - 1.85f, q.z}, i * 5 + 77,
                     0.018f, 14, kCable);
  }

  // ---- a service drop: from double-attachment poles down to a small
  // junction. BUILD-P2 FIX: the drop ties to the real service spool height,
  // not to a point floating off the pole flank.
  for (int side = 0; side < 2; side++) {
    const PoleSpec &p = lineA[side == 0 ? 3 : 9];
    // BUILD-P6: the drops land on REAL HOUSE WALLS on the left flank (the
    // houses below), not on floating points over the road — every wire in
    // the scene terminates on hardware.
    float hz = p.z + 6.0f;
    Vec3 wallA{-8.05f, 3.35f, hz};            // right wall of the left house
    Vec3 wallB{-8.05f, 2.90f, hz + 0.55f};
    AddWire(PoleAxisAt(p, 5.4f), wallA, 0.55f, 0.020f, 10, kCableOld);
    AddWire(ArmInsulatorTop(p, 1.05f), wallB, 0.50f, 0.020f, 10, kCableOld);
    // service mast on the house wall where the drops land
    AddCylinder({wallA.x - 0.02f, wallA.y - 0.25f, wallA.z},
                {wallA.x - 0.02f, wallA.y + 0.55f, wallA.z}, 0.05f, 0.05f, 6,
                kMetal);
    // junction cans stay mounted on the pole wall
    AddCylinder({PoleAxisAt(p, 3.05f).x - 0.30f, 3.05f, p.z},
                {PoleAxisAt(p, 2.45f).x - 0.30f, 2.45f, p.z},
                0.09f, 0.09f, 8, kMetal);
    AddBox({PoleAxisAt(p, 3.10f).x - 0.30f, 3.10f, p.z}, {0.16f, 0.10f, 0.12f},
           kMetal);
  }

  // ---- BUILD-P6: THE SUBURB. Silhouette houses on both flanks — gabled
  // roofs, dark windows — the depth cue that kills the "empty flat world"
  // look and gives the service drops something real to land on.
  for (int i = 0; i < 8; i++) {
    float z = -4.0f + 19.0f * i + 3.0f * ((i * 7) % 3);
    AddHouse(-11.5f - 2.0f * (i % 3), z, 4.6f + 1.4f * ((i * 3) % 3),
             5.2f + 1.2f * ((i * 5) % 3), 3.2f + 0.9f * ((i * 7) % 3), -1.0f);
  }
  for (int i = 0; i < 6; i++) {
    float z = 8.0f + 21.0f * i + 2.5f * ((i * 5) % 3);
    AddHouse(15.0f + 2.5f * (i % 3), z, 4.8f + 1.5f * ((i * 3) % 3),
             5.4f + 1.1f * ((i * 7) % 3), 3.1f + 1.0f * ((i * 5) % 3), 1.0f);
  }

  // ---- BUILD-P4/P6: THE ROAD. A straight asphalt strip BETWEEN the two
  // pole lines (lineA x=-3.4 = left shoulder, lineB x=+8.6 = right
  // shoulder, road centre x=+2.6, 5.4 m wide), worn centre dashes + solid
  // painted edge lines — the corridor the auto camera drives down the
  // middle of.
  {
    const float rx = 2.6f, halfW = 2.7f;
    Vec3 road{0.16f, 0.155f, 0.165f};
    Vec3 edge{0.20f, 0.19f, 0.19f};
    Vec3 paint{0.62f, 0.58f, 0.50f};
    unsigned int s = (unsigned int)gVerts.size();
    Vec3 n{0, 1, 0};
    // worn asphalt body
    PushVert({rx - halfW + 0.35f, 0.008f, -60.0f}, n, road);
    PushVert({rx + halfW - 0.35f, 0.008f, -60.0f}, n, road);
    PushVert({rx + halfW - 0.35f, 0.008f, 200.0f}, n, road);
    PushVert({rx - halfW + 0.35f, 0.008f, 200.0f}, n, road);
    gIdx.push_back(s); gIdx.push_back(s + 1); gIdx.push_back(s + 2);
    gIdx.push_back(s); gIdx.push_back(s + 2); gIdx.push_back(s + 3);
    // gravel-dusted edges either side
    for (int e = 0; e < 2; e++) {
      float xo = e ? halfW - 0.35f : -halfW;
      unsigned int es = (unsigned int)gVerts.size();
      PushVert({rx + xo, 0.006f, -60.0f}, n, edge);
      PushVert({rx + xo + (e ? 0.35f : -0.35f), 0.006f, -60.0f}, n, edge);
      PushVert({rx + xo + (e ? 0.35f : -0.35f), 0.006f, 200.0f}, n, edge);
      PushVert({rx + xo, 0.006f, 200.0f}, n, edge);
      gIdx.push_back(es); gIdx.push_back(es + 1); gIdx.push_back(es + 2);
      gIdx.push_back(es); gIdx.push_back(es + 2); gIdx.push_back(es + 3);
    }
    // centre dashes: 3 m paint, 5 m gap, the whole length
    for (float z = -40.0f; z < 160.0f; z += 8.0f) {
      unsigned int ds = (unsigned int)gVerts.size();
      PushVert({rx - 0.09f, 0.012f, z}, n, paint);
      PushVert({rx + 0.09f, 0.012f, z}, n, paint);
      PushVert({rx + 0.09f, 0.012f, z + 3.0f}, n, paint);
      PushVert({rx - 0.09f, 0.012f, z + 3.0f}, n, paint);
      gIdx.push_back(ds); gIdx.push_back(ds + 1); gIdx.push_back(ds + 2);
      gIdx.push_back(ds); gIdx.push_back(ds + 2); gIdx.push_back(ds + 3);
    }
  }

  // BUILD-P2/P6: long dusk shadows are PER-FRAME now (the sun moves — see
  // DrawGroundShadows), so nothing shadow-shaped is baked here anymore.

  // upload
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

// ------------------------------------------------------------------- HUD
// (kFontAtlasW/H/Cell come from font_atlas.hxx — shared with every scene.)
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
  // build tag: on-screen proof of which scene code the exe runs (stale-build
  // screenshots must be detectable at a glance)
  char line1[128];
  std::snprintf(line1, sizeof(line1), "FPS: %d   build P6   scene 4: power lines", gFps);
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
  std::snprintf(hint, sizeof(hint), "Power Lines score");

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
  glDisable(GL_DEPTH_TEST);
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
  Vec3 sd = SunDirNow(timeSec);   // BUILD-P6: the sky follows the moving sun
  glUniform3f(gSkyProg.loc("uSunDir"), sd.x, sd.y, sd.z);
  glUniform1f(gSkyProg.loc("uTime"), (float)timeSec);
  glDrawElements(GL_TRIANGLES, gSkyIndexCount, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
  glEnable(GL_CULL_FACE);
  glDepthMask(GL_TRUE);
  glEnable(GL_DEPTH_TEST);
}

// Per-frame ground shadows: a streamed quad per pole along the CURRENT sun
// ray. Because this rebuilds every frame, the shadows swing as the sun
// moves — the "alive street" cue. Drawn right after the static geometry
// with the same program (positions/normals/colours, same vertex layout).
static GLuint gShadowVbo = 0;
static void DrawGroundShadows(const Mat4 &view, const Vec3 &eye, double t,
                              const std::vector<PoleSpec> &lineA,
                              const std::vector<PoleSpec> &lineB) {
  if (!gShadowVbo) glGenBuffers(1, &gShadowVbo);
  Vec3 sun = Vec3Norm({SunDirNow(t).x, 0.0f, SunDirNow(t).z});
  Vec3 perp{-sun.z, 0.0f, sun.x};
  Vec3 sc{0.135f, 0.10f, 0.082f};              // warm dusk shadow
  std::vector<ObjVertex> v;
  v.reserve((lineA.size() + lineB.size()) * 4);
  for (const std::vector<PoleSpec> *line : {&lineA, &lineB}) {
    for (const PoleSpec &p : *line) {
      Vec3 b = PoleAxisAt(p, 0.0f);
      b.x = p.x; b.z = p.z;
      float reach = 16.0f * (1.0f + 0.05f * (float)((int(p.z) % 7)));
      Vec3 tip = Vec3Add(b, Vec3Scale(sun, -reach));   // AWAY from the sun
      float w0 = 0.34f, w1 = 1.15f;
      Vec3 c[4] = {
          Vec3Add(b, Vec3Scale(perp, w0)),
          Vec3Add(b, Vec3Scale(perp, -w0)),
          Vec3Add(tip, Vec3Scale(perp, w1)),
          Vec3Add(tip, Vec3Scale(perp, -w1))};
      unsigned int s = (unsigned int)v.size();
      for (int k = 0; k < 4; k++)
        v.push_back({c[k].x, 0.012f, c[k].z, 0, 1, 0, sc.x, sc.y, sc.z});
      (void)s;
    }
  }
  glBindBuffer(GL_ARRAY_BUFFER, gShadowVbo);
  glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(ObjVertex), v.data(),
               GL_STREAM_DRAW);
  Mat4 vp;
  Mat4Multiply(vp, gProj, view);
  Mat4 model;
  Mat4Identity(model);
  glUseProgram(gObjProg.handle);
  glBindVertexArray(gObjVao);
  glBindBuffer(GL_ARRAY_BUFFER, gShadowVbo);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void *)0);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ObjVertex),
                        (void *)(3 * sizeof(float)));
  glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(ObjVertex),
                        (void *)(6 * sizeof(float)));
  glDisable(GL_CULL_FACE);
  glUniformMatrix4fv(gObjProg.loc("uViewProj"), 1, GL_FALSE, vp.data());
  glUniformMatrix4fv(gObjProg.loc("uModel"), 1, GL_FALSE, model.data());
  glUniform3f(gObjProg.loc("uEyePos"), eye.x, eye.y, eye.z);
  Vec3 sd = SunDirNow(t);
  glUniform3f(gObjProg.loc("uSunDir"), sd.x, sd.y, sd.z);
  glUniform1f(gObjProg.loc("uTime"), (float)t);
  // the shadow quads are flat fans; indices come from the shared element
  // buffer layout of 4-vert quads — build a tiny index buffer once
  static std::vector<unsigned int> sIdx;
  static GLuint sIdxBuf = 0;
  if (sIdx.size() != v.size() / 4 * 6) {
    sIdx.clear();
    for (unsigned int q = 0; q + 3 < v.size(); q += 4) {
      sIdx.push_back(q); sIdx.push_back(q + 1); sIdx.push_back(q + 2);
      sIdx.push_back(q); sIdx.push_back(q + 2); sIdx.push_back(q + 3);
    }
    if (!sIdxBuf) glGenBuffers(1, &sIdxBuf);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sIdxBuf);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sIdx.size() * sizeof(unsigned int),
                 sIdx.data(), GL_STREAM_DRAW);
  } else {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sIdxBuf);
  }
  glDrawElements(GL_TRIANGLES, (GLsizei)sIdx.size(), GL_UNSIGNED_INT, nullptr);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gObjIbo);   // restore shared layout
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
  // Two-sided: the CPU generators do not guarantee one winding convention
  // across cylinders/boxes/quads, and the fragment shader resolves the
  // normal toward the eye itself. Culling here would clip whole surfaces
  // (the ground quad) away depending on their generation order.
  glDisable(GL_CULL_FACE);
  glUniformMatrix4fv(gObjProg.loc("uViewProj"), 1, GL_FALSE, vp.data());
  glUniformMatrix4fv(gObjProg.loc("uModel"), 1, GL_FALSE, model.data());
  glUniform3f(gObjProg.loc("uEyePos"), eye.x, eye.y, eye.z);
  glUniform3f(gObjProg.loc("uSunDir"), kSunDir.x, kSunDir.y, kSunDir.z);
  glUniform1f(gObjProg.loc("uTime"), (float)timeSec);
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
    // gaze: up the line at the wire bundle, toward the sun on the horizon
    Vec3 look{-0.45f, 5.5f, gCamPos.z + 26.0f};
    Mat4LookAt(view, eye, look, {0, 1, 0});
  }
  float aspect = (float)gWindowWidth / (float)gWindowHeight;
  Mat4Perspective(gProj, 52.0f, aspect, 0.1f, 900.0f);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, gWindowWidth, gWindowHeight);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  DrawSky(view, eye, gSimTime);
  DrawGrid(view, eye, gSimTime);
  // BUILD-P6: the moving sun re-draws the ground shadows every frame, so
  // they swing with it (lineA/lineB are built once at startup and kept).
  DrawGroundShadows(view, eye, gSimTime, gLineA, gLineB);
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

// ------------------------------------------------------------------- input
static void ProcessKeys(const SDL_Event &event) {
  if (event.key.keysym.sym == SDLK_ESCAPE) {
    gQuit = true;
  } else if (event.key.keysym.sym == SDLK_f) {
    gAutoCam = !gAutoCam;
  } else if (event.key.keysym.sym == SDLK_LEFT) {
    gCamYaw -= 0.05f;
  } else if (event.key.keysym.sym == SDLK_RIGHT) {
    gCamYaw += 0.05f;
  } else if (event.key.keysym.sym == SDLK_UP) {
    gCamPitch = std::fmin(gCamPitch + 0.03f, -0.02f);
  } else if (event.key.keysym.sym == SDLK_DOWN) {
    gCamPitch = std::fmax(gCamPitch - 0.03f, -1.2f);
  }
}

static void HandleMouseEvent(const SDL_Event &event) {
  if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
    gXOld = event.button.x;
    gYOld = event.button.y;
    gIsHoldingMouse = true;
    gAutoCam = false;
  } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
    gIsHoldingMouse = false;
  } else if (event.type == SDL_MOUSEWHEEL) {
    gCamDist *= (event.wheel.y > 0) ? 0.90f : 1.10f;
    if (gCamDist < 2.0f) gCamDist = 2.0f;
    if (gCamDist > 60.0f) gCamDist = 60.0f;
  }
}

static void HandleMouseMotion(const SDL_Event &event) {
  if (gIsHoldingMouse) {
    gCamYaw -= (event.motion.x - gXOld) * 0.005f;
    gXOld = event.motion.x;
    gCamPitch = std::fmin(std::fmax(gCamPitch + (event.motion.y - gYOld) * 0.004f, -1.2f), -0.02f);
    gYOld = event.motion.y;
  }
}

static void ChangeSize(int w, int h) {
  if (h == 0) h = 1;
  gWindowWidth = w;
  gWindowHeight = h;
  glViewport(0, 0, w, h);
}

// -------------------------------------------------------- visual-test support
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
  glClearColor(0.60f, 0.30f, 0.10f, 1.0f); // dusk amber (only visible on gaps)
}

// ------------------------------------------------------------------ session
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

  gWindow = SDL_CreateWindow(gName, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                             winW, winH,
                             SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
  if (gWindowWidthOverride > 0) {
    gWindowWidth = winW;
    gWindowHeight = winH;
    glViewport(0, 0, winW, winH);
  }
  if (!gWindow) {
    std::fprintf(stderr, "Window could not be created! SDL_Error: %s\n", SDL_GetError());
    return EXIT_FAILURE;
  }
  gContext = SDL_GL_CreateContext(gWindow);
  if (!gContext) {
    std::printf("Power lines scene: OpenGL 3.3 core context unavailable — skipping this scene\n");
    std::fflush(stdout);
    SDL_Quit();
    if (gaveUpOut) *gaveUpOut = true;
    return 1;
  }
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
      } else if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP ||
                 event.type == SDL_MOUSEWHEEL) {
        HandleMouseEvent(event);
      } else if (event.type == SDL_MOUSEMOTION) {
        HandleMouseMotion(event);
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
