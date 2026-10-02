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
// Per-vertex colour geometry: position(3) + normal(3) + colour(3) +
// material(1) + alpha(1).
//
// BUILD-P7: the material id is what lets ONE fragment shader give asphalt,
// creosote bark, siding and pantile their own surface detail instead of
// painting every surface with the same flat ramp. Alpha is only used by the
// streamed ground shadows (the pass is alpha-blended and multiplies what is
// already in the framebuffer); it stays 1.0 everywhere else.
struct ObjVertex { float x, y, z, nx, ny, nz, r, g, b, mat, alpha; };
static std::vector<ObjVertex> gVerts;
static std::vector<unsigned int> gIdx;

// material ids — MUST match the kMat* constants in
// shaders/pole/object_frag.glsl.
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

static const Vec3 kWoodDark{0.165f, 0.115f, 0.085f};   // creosote pole
static const Vec3 kWoodOld{0.230f, 0.180f, 0.140f};   // weathered crossarm
static const Vec3 kCeramic{0.780f, 0.760f, 0.700f};   // insulator glaze
static const Vec3 kCable{0.055f, 0.050f, 0.055f};    // rubber wire
static const Vec3 kCableOld{0.085f, 0.075f, 0.070f};
static const Vec3 kMetal{0.190f, 0.195f, 0.200f};    // transformer can
static const Vec3 kGravel{0.520f, 0.420f, 0.310f};   // warm dirt road

// BUILD-P7 ROAD GEOMETRY. The road was rebuilt because build P6 laid it out
// as three coplanar strips with a 0.35 m HOLE down the left side (the body
// started 0.35 m inboard of where the edge strip ended), which showed up on
// the user's box as a pale diagonal band of bare gravel running the length
// of the road. The heights below are the anti-z-fight ladder: ground 0,
// road kRoadY, paint kPaintY, shadows kShadowY, each with centimetre-scale
// separation that survives the 0.1..900 m depth range at 100 m out.
static const float kRoadX = 2.6f;       // road centre (matches uRoadX)
static const float kRoadHalf = 2.7f;    // 5.4 m carriageway
static const float kRoadY = 0.030f;
static const float kPaintY = 0.050f;
static const float kShadowY = 0.075f;

static void PushVert(const Vec3 &p, const Vec3 &n, const Vec3 &c,
                     float mat = kMatPaint, float alpha = 1.0f) {
  gVerts.push_back({p.x, p.y, p.z, n.x, n.y, n.z, c.x, c.y, c.z, mat, alpha});
}

// Cylinder between two points (solid, capped): the pole trunk, insulators,
// the transformer can, stray posts.
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

// BUILD-P7: the drop wires need somewhere REAL to land. AddHouse registers a
// service anchor (an eave bracket) here; AddTelecomBundle runs a drop to the
// nearest reachable anchor instead of leaving it hanging in mid air, which is
// what made build P6 read as "the wires are cut".
static std::vector<Vec3> gDropAnchors;

// BUILD-P4 TELECOM BUNDLE: a communication cable sags between its two pole
// brackets, and a bundle of thin DROP WIRES peels off along the span — the
// drippy ''telephone lines going everywhere'' of every Japanese street.
// Deterministic (hash of the span index) like everything else in the bench.
//
// BUILD-P7 NO CUT ENDS: a drop either runs to a house eave anchor (with the
// bracket that carries it) or ends in a real termination fitting — the small
// dark boot + ceramic that a real drop wire is capped with. A bare tube end
// floating in the air is the exact artefact the user screenshotted.
static void AddTelecomBundle(const Vec3 &a, const Vec3 &b, int seed,
                             float radius, int samples, const Vec3 &color) {
  AddWire(a, b, 0.55f + 0.10f * ((seed * 7) % 3), radius, samples, color,
          kMatCable);
  int drops = 4 + (seed % 3);                    // 4-6 drop wires per span
  for (int i = 0; i < drops; i++) {
    float t = 0.18f + 0.62f * (float)((seed * 13 + i * 29) % 100) / 100.0f;
    float drop = 0.35f + 0.55f * (float)((seed * 17 + i * 41) % 100) / 100.0f;
    Vec3 p{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t - drop,
           a.z + (b.z - a.z) * t};
    // the clamp where the drop peels off the bundle (a small metal ferrule
    // ON the cable — not a rod hanging in the air)
    Vec3 peel{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
              a.z + (b.z - a.z) * t};
    AddCylinder(peel, Vec3Add(peel, Vec3{0, 0.09f, 0}), 0.036f, 0.036f, 6,
                kMetal, kMatMetal);
    // nearest house anchor this drop can plausibly reach. The scan starts at
    // a per-drop offset so five drops in one span fan out to five different
    // brackets instead of all bunching on the same eave.
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
      // service tail: short, and capped with a real fitting
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

// A sagging wire between two attachment points: a real catenary sampled as a
// swept tube (the Lain look is ALL about the droop of these cables).
static void AddWire(const Vec3 &a, const Vec3 &b, float sag, float radius,
                    int samples, const Vec3 &color, float mat) {
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
  // crossarm at height-0.55, insulator stack on top of it ends at +0.30 —
  // BUILD-P7: the old height-0.20 attachment floated 5 cm ABOVE the glaze,
  // which read (correctly) as a wire stopping short of its insulator
  Vec3 a = PoleAxisAt(p, p.height - 0.25f);
  return {a.x + off, p.height - 0.25f, a.z};
}
static Vec3 PoleTopInsulatorTop(const PoleSpec &p) {
  Vec3 a = PoleAxisAt(p, p.height + 0.24f);
  return {a.x, p.height + 0.24f, a.z};
}

static std::vector<PoleSpec> gLineA, gLineB;  // kept for per-frame shadows
static void AddPole(const PoleSpec &p) {
  Vec3 base = PoleAxisAt(p, kGroundY);
  Vec3 top = PoleAxisAt(p, p.height);
  AddCylinder(base, top, 0.17f, 0.115f, 10, kWoodDark, kMatWood);
  // dirt collar kicked up around the base (every real pole sits in one)
  AddCylinder({base.x, kGroundY - 0.02f, base.z}, {base.x, 0.10f, base.z},
              0.52f, 0.34f, 8, {0.30f, 0.24f, 0.18f}, kMatGround);

  // main crossarm near the top + a smaller one below, with diagonal braces
  float armY = p.height - 0.55f;
  Vec3 armC = PoleAxisAt(p, armY);
  AddBox({armC.x, armY, armC.z}, {1.25f, 0.055f, 0.075f}, kWoodOld, kMatWood);
  Vec3 arm2C = PoleAxisAt(p, armY - 0.62f);
  AddBox({arm2C.x, armY - 0.62f, arm2C.z}, {0.85f, 0.05f, 0.07f}, kWoodOld,
         kMatWood);
  Vec3 brT = PoleAxisAt(p, armY - 0.05f);
  Vec3 brB = PoleAxisAt(p, armY - 0.57f);
  AddCylinder({brT.x - 0.34f, brT.y, brT.z + 0.03f},
              {brB.x - 0.94f, brB.y, brB.z + 0.03f}, 0.030f, 0.030f, 6,
              kWoodDark, kMatWood);
  AddCylinder({brT.x + 0.34f, brT.y, brT.z + 0.03f},
              {brB.x + 0.94f, brB.y, brB.z + 0.03f}, 0.030f, 0.030f, 6,
              kWoodDark, kMatWood);

  // ceramic insulators: three on the main arm, one atop the pole
  for (float off : {-1.05f, 0.0f, 1.05f}) {
    Vec3 ib{armC.x + off, armY + 0.05f, armC.z};
    AddCylinder(ib, Vec3Add(ib, Vec3{0, 0.24f, 0}), 0.052f, 0.062f, 8, kCeramic,
                kMatCeramic);
    AddCylinder(Vec3Add(ib, Vec3{0, 0.24f, 0}), Vec3Add(ib, Vec3{0, 0.30f, 0}),
                0.062f, 0.040f, 8, kCeramic, kMatCeramic);
  }
  AddCylinder(top, Vec3Add(top, Vec3{0, 0.18f, 0}), 0.05f, 0.058f, 8, kCeramic,
              kMatCeramic);
  AddCylinder(Vec3Add(top, Vec3{0, 0.18f, 0}), Vec3Add(top, Vec3{0, 0.24f, 0}),
              0.058f, 0.038f, 8, kCeramic, kMatCeramic);

  // earth wire: a bare cable clipped down the trunk, grounded at the collar.
  // BUILD-P7: it follows the LEANING axis hop by hop instead of running as
  // one straight line from the base to a fixed point, which used to drift off
  // the trunk surface on the leaning poles.
  {
    Vec3 prev = PoleAxisAt(p, 0.12f);
    for (float h = 1.0f; h <= 4.01f; h += 1.0f) {
      float f = h / p.height;
      Vec3 cur = PoleAxisAt(p, h);
      float r = 0.17f + (0.115f - 0.17f) * f;
      prev.x += 0.13f;                       // stand off the bark
      cur.x += r * 0.92f;
      AddWire(prev, cur, 0.02f, 0.013f, 3, kMetal, kMatCable);
      prev = PoleAxisAt(p, h);
    }
  }

  if (p.serviceSpool) {
    // secondary service spool on the other flank (double-attachment poles)
    AddCylinder({p.x - 0.24f, 5.4f, p.z}, {p.x - 0.34f, 5.4f, p.z}, 0.05f,
                0.05f, 6, kMetal, kMatMetal);
  }

  // BUILD-P4 TELECOM ARM: a second, lower crossarm carrying the phone/cable
  // bundles (Japanese poles stack a communications arm under the power arm).
  float telY = armY - 1.30f;
  Vec3 telC = PoleAxisAt(p, telY);
  AddBox({telC.x, telY, telC.z}, {0.95f, 0.05f, 0.06f}, kWoodOld, kMatWood);
  for (float off : {-0.70f, 0.0f, 0.70f})
    AddCylinder({telC.x + off, telY + 0.05f, telC.z},
                {telC.x + off, telY + 0.15f, telC.z}, 0.038f, 0.032f, 6, kMetal,
                kMatMetal);

  // a couple of CableTV-style cylindrical boxes bolted to the trunk (some
  // poles, deterministic)
  if (((int(p.z * 7.0f)) % 3) == 0)
    AddCylinder({base.x + 0.20f, 3.9f, base.z}, {base.x + 0.20f, 4.5f, base.z},
                0.11f, 0.11f, 8, kMetal, kMatMetal);

  if (p.transformer) {
    // the can: grey cylinder + cooling fins, bolted below the crossarm
    Vec3 tc{armC.x + 0.62f, armY - 1.35f, armC.z};
    AddCylinder(Vec3Add(tc, Vec3{-0.1f, -0.55f, 0}),
                Vec3Add(tc, Vec3{0.1f, 0.55f, 0}), 0.34f, 0.34f, 10, kMetal,
                kMatMetal);
    AddBox({tc.x, tc.y + 0.30f, tc.z}, {0.40f, 0.16f, 0.16f}, kMetal, kMatMetal);
    AddBox({tc.x, tc.y - 0.34f, tc.z}, {0.10f, 0.22f, 0.10f}, kMetal, kMatMetal);
    // two ceramic bushings on the can's crown + their drop leads
    for (float bz : {-0.12f, 0.12f}) {
      Vec3 bt{tc.x, tc.y + 0.46f, tc.z + bz};
      AddCylinder(bt, Vec3Add(bt, Vec3{0, 0.16f, 0}), 0.045f, 0.038f, 6,
                  kCeramic, kMatCeramic);
      AddWire(Vec3Add(bt, Vec3{0, 0.18f, 0}), {tc.x, armY - 0.30f, tc.z + bz},
              0.08f, 0.011f, 5, kCable, kMatCable);
    }
  }
}

// BUILD-P7: a single free quad. The object pass runs with culling OFF (the
// generators do not share one winding convention) and the fragment shader
// resolves the normal toward the eye, so winding does not matter here —
// which makes sloped roof planes a two-line job instead of a matrix helper.
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

// BUILD-P6/P7: suburban silhouette houses on both flanks. Build P6 drew plain
// untextured slabs that read as boxes parked on a flat plane; P7 gives every
// house a concrete plinth, siding-shaded walls, deep overhanging eaves, a
// ridge cap, glazed windows with frames and sills and an entry canopy. Each
// house also registers a SERVICE ANCHOR (an eave bracket on the street side)
// so the telecom drops terminate on hardware instead of in mid air.
static const Vec3 kWallTints[5] = {
    {0.400f, 0.352f, 0.300f}, {0.470f, 0.425f, 0.360f}, {0.330f, 0.300f, 0.278f},
    {0.520f, 0.470f, 0.386f}, {0.290f, 0.272f, 0.262f}};
static const Vec3 kRoofTints[3] = {
    {0.150f, 0.132f, 0.128f}, {0.330f, 0.175f, 0.105f}, {0.190f, 0.180f, 0.175f}};

static void AddHouse(float x, float z, float w, float d, float h, float face) {
  int pick = (int)std::fmod(std::fabs(std::sin(x * 12.9898f + z * 78.233f)) *
                                43758.5453f,
                            5.0f);
  const Vec3 &wall = kWallTints[pick];
  const Vec3 &roof = kRoofTints[pick % 3];
  float streetX = face * w * 0.5f;           // the road-facing wall

  // concrete plinth: the house sits ON something instead of hovering
  AddBox({x, 0.09f, z}, {w * 0.5f + 0.07f, 0.09f, d * 0.5f + 0.07f},
         {0.230f, 0.215f, 0.200f}, kMatPaint);
  // body
  AddBox({x, 0.10f + h * 0.5f, z}, {w * 0.5f, h * 0.5f, d * 0.5f}, wall,
         kMatWall);

  // ---- pitched roof: two slanted planes + a thin under-plane for thickness
  const float ov = 0.42f;                     // eave overhang
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
  // ridge cap
  AddCylinder({x0, yRidge + 0.02f, z}, {x1, yRidge + 0.02f, z}, 0.055f, 0.055f,
              5, Vec3Add(roof, Vec3{0.05f, 0.03f, 0.02f}), kMatRoof);

  // ---- windows: frame + sill + glazing on the street flank and the gable
  const Vec3 frame{0.320f, 0.300f, 0.270f};
  const Vec3 glass{0.060f, 0.075f, 0.095f};
  for (int i = 0; i < 3; i++) {
    float wx = x + streetX + face * 0.03f;
    float wz = z + (float)(i - 1) * (d * 0.30f);
    float wy = 0.10f + h * 0.60f;
    AddBox({wx + face * 0.02f, wy, wz}, {0.05f, 0.26f, 0.34f}, frame, kMatPaint);
    AddBox({wx + face * 0.06f, wy, wz}, {0.02f, 0.21f, 0.29f}, glass, kMatGlass);
    AddBox({wx + face * 0.09f, wy - 0.28f, wz}, {0.07f, 0.035f, 0.40f}, frame,
           kMatPaint);
  }
  AddBox({x - w * 0.5f + 0.03f * face, 0.10f + h * 0.55f, z + d * 0.22f},
         {0.03f, 0.20f, 0.28f}, frame, kMatPaint);
  AddBox({x - w * 0.5f + 0.06f * face, 0.10f + h * 0.55f, z + d * 0.22f},
         {0.02f, 0.16f, 0.24f}, glass, kMatGlass);

  // ---- entry canopy over the front door (every other house)
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

  // ---- SERVICE ANCHOR: the eave bracket the telecom drops land on
  Vec3 anchor{x + streetX + face * 0.16f, 0.10f + h * 0.78f, z + d * 0.30f};
  AddBox({anchor.x - face * 0.04f, anchor.y, anchor.z}, {0.07f, 0.045f, 0.045f},
         kMetal, kMatMetal);
  AddCylinder({anchor.x, anchor.y + 0.04f, anchor.z},
              {anchor.x, anchor.y + 0.17f, anchor.z}, 0.030f, 0.026f, 6,
              kCeramic, kMatCeramic);
  gDropAnchors.push_back({anchor.x, anchor.y + 0.17f, anchor.z});
}

// BUILD-P7: the far treeline. An empty plane running to a bare horizon is the
// other half of "looks unrealistic" — real suburbs have a ragged band of
// cedar and bamboo closing the view. Cheap silhouette boxes, deterministic,
// sitting well inside the far plane so they never clip.
static void AddTreeline() {
  const int kCount = 74;
  for (int i = 0; i < kCount; i++) {
    // deterministic ring, jittered radius, taller in the middle band
    float a = (float)i * 2.3999632f;                       // golden-angle
    float rad = 205.0f + 95.0f * (float)((i * 7) % 5) / 5.0f;
    float cx = std::sin(a) * rad * 1.25f;
    float cz = 90.0f + std::cos(a) * rad;
    float th = 9.0f + 9.0f * (float)((i * 13) % 7) / 7.0f;
    float wd = 2.6f + 1.8f * (float)((i * 5) % 4) / 4.0f;
    // cedar green, warmed toward olive near the haze
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

  // ---- ground: a big warm gravel plane (single quad, cheap as dirt).
  // BUILD-P7: stretched to +-430 m / z -300..880 so the corridor never shows
  // its edge and the treeline has ground to stand on. Corners stay inside the
  // 900 m far plane from the furthest camera position (z = 165 m).
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

  // ---- BUILD-P6/P7: THE SUBURB, built BEFORE the telecom tangle so the drop
  // wires have real eave brackets to land on. Houses on both flanks (they
  // give the corridor its depth) and a far treeline so the horizon is not a
  // bare line between dirt and sky.
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
  AddTreeline();

  // ---- BUILD-P4: THE TELECOM TANGLE. Two bundles per span on line A (one
  // per bracket pair) plus one on line B, and cross-line telecom spans
  // B->A — this is what makes a Japanese pole street read as a Japanese
  // pole street: tons and tons of sagging phone wire everywhere. Every drop
  // now terminates on a house bracket or a real fitting (see
  // AddTelecomBundle).
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

  // ---- service drops: from the pole service spool down to the NEAREST house
  // bracket. BUILD-P7: build P6 dropped these onto two hard-coded wall points
  // that were over a metre clear of the actual house wall, which is why the
  // left of frame was full of wires stopping in thin air. Now the destination
  // is whatever eave bracket the house itself registered.
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
      AddWire(PoleAxisAt(p, 5.4f), a, 0.55f, 0.020f, 12, kCableOld, kMatCable);
      AddWire(ArmInsulatorTop(p, 1.05f), a, 0.48f, 0.020f, 12, kCableOld,
              kMatCable);
    }
    // junction cans stay mounted on the pole wall
    AddCylinder({PoleAxisAt(p, 3.05f).x - 0.30f, 3.05f, p.z},
                {PoleAxisAt(p, 2.45f).x - 0.30f, 2.45f, p.z},
                0.09f, 0.09f, 8, kMetal, kMatMetal);
    AddBox({PoleAxisAt(p, 3.10f).x - 0.30f, 3.10f, p.z}, {0.16f, 0.10f, 0.12f},
           kMetal, kMatMetal);
  }

  // ---- BUILD-P7: THE ROAD, REBUILT. Build P6 laid it out in three pieces
  // whose x ranges did not tile: the body ran [rx-2.35, rx+2.35] while the
  // left shoulder ran [rx-3.05, rx-2.70], leaving a 0.35 m strip of BARE
  // BRIGHT GRAVEL between them for the whole length of the corridor. At the
  // camera's grazing angle that strip reads as a pale diagonal band lying
  // across the road — the artifact the user screenshotted. The carriageway is
  // now ONE contiguous quad, the shoulders live strictly OUTSIDE it, and the
  // paint sits on its own level above it (see the kRoadY/kPaintY/kShadowY
  // ladder). Runs from z=-200 to the treeline at z=520.
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
    // gravel shoulders, strictly outside the carriageway — no overlap, so no
    // coplanar z-fighting either
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
    // centre dashes: 3 m paint, 5 m gap
    for (float z = -60.0f; z < 400.0f; z += 8.0f) {
      unsigned int ds = (unsigned int)gVerts.size();
      PushVert({kRoadX - 0.09f, kPaintY, z}, n, paint, kMatLine);
      PushVert({kRoadX + 0.09f, kPaintY, z}, n, paint, kMatLine);
      PushVert({kRoadX + 0.09f, kPaintY, z + 3.0f}, n, paint, kMatLine);
      PushVert({kRoadX - 0.09f, kPaintY, z + 3.0f}, n, paint, kMatLine);
      gIdx.push_back(ds); gIdx.push_back(ds + 1); gIdx.push_back(ds + 2);
      gIdx.push_back(ds); gIdx.push_back(ds + 2); gIdx.push_back(ds + 3);
    }
    // solid edge lines, inset from the carriageway edge
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
  std::snprintf(line1, sizeof(line1), "FPS: %d   build P7   scene 4: power lines", gFps);
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
  // BUILD-P7: the dome is drawn LAST, depth-tested against a world that is
  // already in the framebuffer, so the cloud fbm only runs on the pixels the
  // scene did not cover — roughly half of them. Previously it shaded the
  // whole frame and was then painted over.
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

// Per-frame ground shadows: a streamed strip per pole along the CURRENT sun
// ray. Because this rebuilds every frame, the shadows swing as the sun
// moves — the "alive street" cue.
//
// BUILD-P7: shadows are ALPHA-BLENDED (blendFunc ZERO, ONE_MINUS_SRC_ALPHA),
// so the pass multiplies whatever is already in the framebuffer instead of
// stamping opaque brown quads over it. That fixes two things at once: the
// shadow lying across the road now darkens the painted DASHES too (before, a
// flat opaque bar covered them), and the strip can fade out along its length
// into a penumbra instead of ending in a hard edge. Five strips with a
// decaying alpha ramp, all sharing vertices at the strip boundaries, give a
// continuous gradient for the cost of ten triangles per pole.
//
// BUILD-P6.1 CRITICAL DRIVER FIX: the shadows live in their OWN VAO. The
// first version rebound the shared object VAO's attribute pointers to the
// shadow VBO every frame and restored only the element buffer — the object
// mesh then read its VERTICES from the 84-vertex shadow buffer. Mesa
// returns zeros for out-of-bounds VBO fetches (invisible), AMD returns
// garbage: giant misindexed triangles washed over the whole frame (the
// "soooo glitched" white/orange screenshot). Separate VAO = zero state
// bleed, on every driver.
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
  // dst *= (1 - srcAlpha): a real shadow, not a brown sticker
  glEnable(GL_BLEND);
  glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);          // shadows must never occlude anything
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
  // Two-sided: the CPU generators do not guarantee one winding convention
  // across cylinders/boxes/quads, and the fragment shader resolves the
  // normal toward the eye itself. Culling here would clip whole surfaces
  // (the ground quad) away depending on their generation order.
  glDisable(GL_CULL_FACE);
  glUniformMatrix4fv(gObjProg.loc("uViewProj"), 1, GL_FALSE, vp.data());
  glUniformMatrix4fv(gObjProg.loc("uModel"), 1, GL_FALSE, model.data());
  glUniform3f(gObjProg.loc("uEyePos"), eye.x, eye.y, eye.z);
  // BUILD-P7: DrawGrid was still uploading the FROZEN build-P2 kSunDir while
  // the sky and the shadows advanced with SunDirNow() — the sun's disc drifted
  // but the lighting stayed put. One timebase for all three now.
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
    // gaze: up the line at the wire bundle, toward the sun on the horizon
    Vec3 look{-0.45f, 5.5f, gCamPos.z + 26.0f};
    Mat4LookAt(view, eye, look, {0, 1, 0});
  }
  float aspect = (float)gWindowWidth / (float)gWindowHeight;
  Mat4Perspective(gProj, 52.0f, aspect, 0.1f, 900.0f);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, gWindowWidth, gWindowHeight);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  DrawGrid(view, eye, gSimTime);
  // BUILD-P6: the moving sun re-draws the ground shadows every frame, so
  // they swing with it (lineA/lineB are built once at startup and kept).
  // BUILD-P7: they are alpha-blended, so they multiply whatever is under
  // them — road paint included.
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
