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
// late-afternoon sun: low, warm, slightly off the line axis so the wires
// catch rim light from the side
static const Vec3 kSunDir{0.34f, 0.28f, -0.90f};

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
  const float span = 58.0f;
  float z = 4.0f + std::fmod(t * 2.6f, span);
  gCamPos = {1.9f + std::sin(t * 0.05f) * 0.9f,          // gentle weave
             1.85f + 0.22f * std::sin(t * 0.11f),        // breathing height
             z};
  // look up the line toward the wires: pitch lifts slowly with each pass
  Vec3 target{0.0f, 6.4f, z + 13.0f};
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

// One utility pole: trunk, two crossarms, insulators, optional transformer.
struct PoleSpec { float x, z; float height; bool transformer; };
static void AddPole(const PoleSpec &p) {
  Vec3 base{p.x, kGroundY, p.z};
  Vec3 top{p.x, p.height, p.z};
  AddCylinder(base, top, 0.17f, 0.115f, 10, kWoodDark);

  // main crossarm near the top + a smaller one below
  float armY = p.height - 0.55f;
  AddBox({p.x, armY, p.z}, {1.25f, 0.055f, 0.075f}, kWoodOld);
  AddBox({p.x, armY - 0.62f, p.z}, {0.85f, 0.05f, 0.07f}, kWoodOld);

  // ceramic insulators: three on the main arm, one atop the pole
  for (float off : {-1.05f, 0.0f, 1.05f}) {
    Vec3 ib{p.x + off, armY + 0.05f, p.z};
    AddCylinder(ib, Vec3Add(ib, Vec3{0, 0.24f, 0}), 0.052f, 0.062f, 8, kCeramic);
    AddCylinder(Vec3Add(ib, Vec3{0, 0.24f, 0}), Vec3Add(ib, Vec3{0, 0.30f, 0}),
                0.062f, 0.040f, 8, kCeramic);
  }
  AddCylinder(top, Vec3Add(top, Vec3{0, 0.18f, 0}), 0.05f, 0.058f, 8, kCeramic);
  AddCylinder(Vec3Add(top, Vec3{0, 0.18f, 0}), Vec3Add(top, Vec3{0, 0.24f, 0}),
              0.058f, 0.038f, 8, kCeramic);

  if (p.transformer) {
    // the can: grey cylinder + cooling fins, bolted below the crossarm
    Vec3 tc{p.x + 0.62f, armY - 1.35f, p.z};
    AddCylinder(Vec3Add(tc, Vec3{-0.1f, -0.55f, 0}),
                Vec3Add(tc, Vec3{0.1f, 0.55f, 0}), 0.34f, 0.34f, 10, kMetal);
    AddBox({tc.x, tc.y + 0.30f, tc.z}, {0.40f, 0.16f, 0.16f}, kMetal);
    AddBox({tc.x, tc.y - 0.34f, tc.z}, {0.10f, 0.22f, 0.10f}, kMetal);
  }
}

static void BuildSceneGeometry() {
  gVerts.clear();
  gIdx.clear();

  // ---- ground: a big warm gravel plane (single quad, cheap as dirt) ----
  {
    unsigned int s = (unsigned int)gVerts.size();
    Vec3 n{0, 1, 0};
    PushVert({-70, kGroundY, -50}, n, kGravel);
    PushVert({70, kGroundY, -50}, n, kGravel);
    PushVert({70, kGroundY, 110}, n, kGravel);
    PushVert({-70, kGroundY, 110}, n, kGravel);
    gIdx.push_back(s); gIdx.push_back(s + 1); gIdx.push_back(s + 2);
    gIdx.push_back(s); gIdx.push_back(s + 2); gIdx.push_back(s + 3);
  }

  // ---- the main pole line the camera walks beside ----
  std::vector<PoleSpec> lineA;
  for (int i = 0; i < 6; i++)
    lineA.push_back({0.0f, 2.0f + 13.0f * i, 8.6f + 0.35f * ((i * 5) % 3), i == 1 || i == 4});
  for (const PoleSpec &p : lineA) AddPole(p);

  // wires along line A: 3 crossarm points + the pole-top wire
  for (int i = 0; i + 1 < (int)lineA.size(); i++) {
    const PoleSpec &p = lineA[i];
    const PoleSpec &q = lineA[i + 1];
    float sag = 0.85f + 0.12f * ((i * 3) % 3);
    for (float off : {-1.05f, 0.0f, 1.05f})
      AddWire({p.x + off, p.height - 0.25f, p.z},
              {q.x + off, q.height - 0.25f, q.z}, sag, 0.028f, 14, kCable);
    AddWire({p.x, p.height + 0.24f, p.z},
            {q.x, q.height + 0.24f, q.z}, sag * 0.8f, 0.032f, 14, kCableOld);
  }

  // ---- a second, farther line: depth + the layered-tangle feel ----
  std::vector<PoleSpec> lineB;
  for (int i = 0; i < 5; i++)
    lineB.push_back({-17.0f, 8.0f + 15.0f * i, 7.6f, i == 2});
  for (const PoleSpec &p : lineB) AddPole(p);
  for (int i = 0; i + 1 < (int)lineB.size(); i++) {
    const PoleSpec &p = lineB[i];
    const PoleSpec &q = lineB[i + 1];
    for (float off : {-0.85f, 0.85f})
      AddWire({p.x + off, p.height - 0.5f, p.z},
              {q.x + off, q.height - 0.5f, q.z}, 0.95f, 0.026f, 12, kCableOld);
  }

  // ---- the crossing spans: line B feeds into line A (the tangle) ----
  for (int i = 0; i < 3; i++) {
    const PoleSpec &p = lineB[i];
    const PoleSpec &q = lineA[i + 1];
    AddWire({p.x + 0.85f, p.height - 0.5f, p.z},
            {q.x - 1.05f, q.height - 0.25f, q.z}, 1.35f, 0.024f, 16, kCable);
  }

  // ---- a service drop: from the nearest pole down to a small junction ----
  {
    const PoleSpec &p = lineA[1];
    AddWire({p.x + 1.05f, p.height - 0.7f, p.z},
            {4.6f, 3.1f, p.z + 3.2f}, 0.55f, 0.020f, 10, kCableOld);
    AddWire({p.x + 0.4f, p.height - 0.7f, p.z},
            {4.1f, 3.1f, p.z + 3.4f}, 0.50f, 0.020f, 10, kCableOld);
    AddCylinder({4.35f, 3.0f, p.z + 3.3f}, {4.35f, 2.45f, p.z + 3.3f},
                0.09f, 0.09f, 8, kMetal);
    AddBox({4.35f, 3.05f, p.z + 3.3f}, {0.16f, 0.10f, 0.12f}, kMetal);
  }

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
  std::snprintf(line1, sizeof(line1), "FPS: %d   build P1   scene 4: power lines", gFps);
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
  glUniform3f(gSkyProg.loc("uSunDir"), kSunDir.x, kSunDir.y, kSunDir.z);
  glUniform1f(gSkyProg.loc("uTime"), (float)timeSec);
  glDrawElements(GL_TRIANGLES, gSkyIndexCount, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
  glEnable(GL_CULL_FACE);
  glDepthMask(GL_TRUE);
  glEnable(GL_DEPTH_TEST);
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
    // gaze: up the line at the wire bundle, a little above head height
    Vec3 look{0.0f, 5.6f, gCamPos.z + 13.0f};
    Mat4LookAt(view, eye, look, {0, 1, 0});
  }
  float aspect = (float)gWindowWidth / (float)gWindowHeight;
  Mat4Perspective(gProj, 52.0f, aspect, 0.1f, 900.0f);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, gWindowWidth, gWindowHeight);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  DrawSky(view, eye, gSimTime);
  DrawGrid(view, eye, gSimTime);
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
