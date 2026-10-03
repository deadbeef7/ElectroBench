// ElectroBench — scene 4 of the single ElectroBench binary: "LainBench", the
// power-lines scene. A late-afternoon Japanese suburb memory: orange sky,
// white drifting clouds, and a tangle of utility poles, crossarms, insulators,
// transformers and sagging wires receding into the heat haze — the
// serial-experiments mood, built entirely from analytic geometry (no model
// files, no textures: every cylinder, catenary and quad is generated on the CPU
// at startup).
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
//   * The grid (shaders/pole/object_*.glsl): GALVANIZED STEEL utility poles on
//     two receding lines — tapered shafts, step bolts, number plates, guy
//     wires into buried anchors, angle-iron crossarms with ceramic pin
//     insulators, cut-out fuses, a finned transformer can — and six tiers of
//     catenary wire per bay (real sag: y = midpoint + cosh falloff) built as
//     swept tubes. One wrap-lighting shader for everything, with aerial
//     perspective sinking the far poles into the haze.
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
// SDL 2.0.5 renamed SDL_GL_MULTISAMPLESAMPLES to SDL_GL_SAMPLES (same enum
// value, 14). Accept either spelling so the MSAA request still builds against
// the older SDL2 headers some of these static builds ship.
#ifndef SDL_GL_SAMPLES
#define SDL_GL_SAMPLES SDL_GL_MULTISAMPLESAMPLES
#endif

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
static const char *const gName = "ElectroBench - LainBench";
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
  gCamPos = {2.0f + std::sin(t * 0.05f) * 0.30f,         // gentle weave
             1.55f + 0.18f * std::sin(t * 0.11f),        // breathing height
             z};
  // BUILD-P12: AIM HIGHER. The reference photograph is taken from the pavement
  // looking UP, and the subject of the shot is the top of the pole: the
  // crossarms, the insulators, the transformer and the cable web. Aiming at
  // 5.5 m framed the middle of the shaft instead, which is the least
  // interesting 3 m of it. The aim point now sits just under the main arm, so
  // the upper half of every pole fills the frame and the web reads against the
  // sky rather than against more poles.
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
static const float kMatGlow = 13.0f;     // emissive: lit signs at dusk
// BUILD-P9: galvanized steel. The poles stopped being creosote timber and
// became hot-dip galvanized steel, and that needed its OWN shader branch —
// zinc is a bright, semi-specular, vertically weathered surface, and running it
// through the wood or metal branches gave the same flat dark post it had
// before (the "poles look like plain wooden sticks" complaint). The branch adds
// zinc spangle, rain-washed streaks, rust blooming at the foot and a much
// sharper specular than paint.
static const float kMatSteel = 14.0f;
// BUILD-P10: cast concrete kerb (must match kMatKerb in object_frag.glsl)
static const float kMatKerb = 15.0f;
// BUILD-P12: weathered precast concrete pole shaft (must match kMatConcrete in
// object_frag.glsl). Japanese distribution poles are overwhelmingly CONCRETE,
// not timber and not steel — a round shaft with a weathered grey skin, which
// is what the reference photograph actually shows.
static const float kMatConcrete = 16.0f;
static const Vec3 kConcreteGrey{0.300f, 0.293f, 0.278f};

static const Vec3 kWoodDark{0.165f, 0.115f, 0.085f};   // creosote (treeline)
static const Vec3 kWoodOld{0.230f, 0.180f, 0.140f};   // weathered timber
static const Vec3 kCeramic{0.780f, 0.760f, 0.700f};   // insulator glaze
static const Vec3 kCable{0.055f, 0.050f, 0.055f};    // rubber wire
static const Vec3 kCableOld{0.085f, 0.075f, 0.070f};
static const Vec3 kMetal{0.190f, 0.195f, 0.200f};    // transformer can
static const Vec3 kSteelGalv{0.345f, 0.356f, 0.368f}; // hot-dip zinc, trunk
static const Vec3 kSteelArm{0.265f, 0.272f, 0.282f};  // angle iron, brackets
static const Vec3 kSteelPlate{0.560f, 0.545f, 0.505f};// pole number plate
static const Vec3 kGravel{0.330f, 0.272f, 0.205f};   // BUILD-P8: dry dirt is
                                                     // ~0.3 albedo, not 0.52

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
// BUILD-P10: the kerb stands 16 cm proud of the carriageway, top at kKerbTopY,
// and its gutter face carries the standing water the road shader reflects.
static const float kKerbTopY = 0.190f;

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

// BUILD-P8: the exact point of a sagging wire at parameter t, shared by the
// tube builder and by everything that has to ATTACH to a wire. Build P7
// computed telecom drop points on the straight line between the two span
// ends, but the bundle sags up to 0.75 m below that line — so every drop's
// top end and every clamp bead floated clear of the cable it belonged to
// (the "small cylinders sitting mid air" report). One evaluator, used by
// both, so the attachment can never drift from the geometry again.
static Vec3 WirePoint(const Vec3 &a, const Vec3 &b, float sag, float t) {
  Vec3 delta = Vec3Sub(b, a);
  float len = Vec3Len(delta);
  if (len < 1e-3f) return a;
  Vec3 dir = Vec3Scale(delta, 1.0f / len);
  Vec3 side = Vec3Norm(Vec3Cross(dir, Vec3{0, 1, 0}));
  if (Vec3Len(side) < 1e-4f) side = Vec3{1, 0, 0};
  Vec3 up = Vec3Norm(Vec3Cross(side, dir));
  float halfSpan = len * 0.5f;
  // BUILD-P8 CRITICAL: `x` is measured FROM THE MIDPOINT, so the curve has to
  // be based at the midpoint. It used to be added to `a` (the span start),
  // which put t=0 at a - dir*halfSpan and t=1 at a + dir*halfSpan — i.e.
  // EVERY wire in the scene was drawn half a span too early, starting at the
  // midpoint of the previous span and stopping in mid air between poles.
  // That is the root cause of the "wires are cut" reports: no amount of
  // careful insulator-top attachment could fix it, because the attachment
  // points were never where the wire actually began and ended.
  Vec3 mid = Vec3Add(a, Vec3Scale(delta, 0.5f));
  float x = (t - 0.5f) * len;
  float y = sag * (1.0f - 4.0f * (t - 0.5f) * (t - 0.5f));
  float tail = sag * 0.06f * (std::cosh(x / (halfSpan * 0.72f)) - 1.0f)
             / (float)std::cosh(halfSpan / (halfSpan * 0.72f));
  return Vec3Add(Vec3Add(mid, Vec3Scale(dir, x)), Vec3Scale(up, -y - tail));
}

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
  float sag = 0.55f + 0.10f * ((seed * 7) % 3);
  AddWire(a, b, sag, radius, samples, color, kMatCable);
  int drops = 4 + (seed % 3);                    // 4-6 drop wires per span
  for (int i = 0; i < drops; i++) {
    float t = 0.18f + 0.62f * (float)((seed * 13 + i * 29) % 100) / 100.0f;
    float drop = 0.35f + 0.55f * (float)((seed * 17 + i * 41) % 100) / 100.0f;
    // BUILD-P8: peel the drop off the cable where the cable ACTUALLY is, not
    // where the straight line between the poles would be.
    Vec3 peel = WirePoint(a, b, sag, t);
    // BUILD-P8: the drop hangs OFF the clamp, so it starts at the clamp's
    // lower face. It used to start a further `drop` (0.35-0.90 m) below the
    // cable — i.e. it hung from nothing at all, which the audit measured at
    // up to 0.895 m from any hardware.
    Vec3 p{peel.x, peel.y - 0.035f, peel.z};
    // the clamp ferrule the drop is bound to — sitting ON the cable
    AddCylinder(Vec3Add(peel, Vec3{0, -0.035f, 0}), Vec3Add(peel, Vec3{0, 0.035f, 0}),
                0.030f, 0.030f, 6, kMetal, kMatMetal);
    // (birds are added once per bundle below, not once per drop wire)
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
  // BUILD-P9: the perched birds are GONE. At corridor scale every bird body
  // crossed the bright sky as a hard black blob, and fifty-one of them
  // scattered across the wire tangle read as dirt on the lens rather than as
  // birds — they were the single loudest "this is not a real photograph" tell
  // in the frame. (Their two-cylinder stand-in body was also drawn with
  // kMatMetal, so it shaded like a grey lump, not a bird.)
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
    // catenary: cosh(x/a) shape approximated by the standard sag parabola
    // plus a cosh tail — visually indistinguishable at wire radii.
    centres[i] = WirePoint(a, b, sag, (float)i / samples);
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
// BUILD-P8: one source of truth for the trunk radius, so anything mounted on
// the bark can be measured off it instead of guessing a fixed offset (which is
// how the service spool and the junction cans ended up hanging in mid air).
static float TrunkRadius(const PoleSpec &p, float h) {
  float f = h / p.height;
  return 0.17f + (0.115f - 0.17f) * f;
}
// BUILD-P8: the telecom arm sits 1.30 m under the power arm and its three
// bracket stubs stand 0.15 m proud of the wood. The telecom spans used to
// attach at the BRACKET BASE on the UNLEANED pole x, i.e. up to 0.16 m from
// any real hardware — another set of wire ends floating just clear of the
// things meant to hold them.
static Vec3 TelecomBracketTop(const PoleSpec &p, float off) {
  float telY = p.height - 0.55f - 1.30f;
  Vec3 a = PoleAxisAt(p, telY);
  return {a.x + off, telY + 0.15f, a.z};
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
// BUILD-P9: the SHORTER lower arm finally carries conductors. Build P8 built
// that arm, hung braces on it and then ran no wires along it at all, so a whole
// tier of hardware sat under the main arm doing nothing. Same 0.213 m stack, so
// this is main-arm height minus 0.62 m.
static Vec3 Arm2InsulatorTop(const PoleSpec &p, float off) {
  Vec3 a = PoleAxisAt(p, p.height - 0.87f);
  return {a.x + off, p.height - 0.87f, a.z};
}

static std::vector<PoleSpec> gLineA, gLineB;  // kept for per-frame shadows

// BUILD-P9: the hardware side of a steel pole. Everything bolted on goes on
// the ROAD side of the shaft — that is the side a lineworker can reach and the
// side the camera walks past, so it is derived from the road centre instead of
// being another per-pole constant to keep in sync.
static float PoleRoadSide(const PoleSpec &p) { return p.x < kRoadX ? 1.0f : -1.0f; }

// BUILD-P9: ONE pin insulator, 0.213 m from its base pin to the top of the
// glaze. ArmInsulatorTop / Arm2InsulatorTop both quote that number, so the
// geometry and the wire attachment cannot drift apart again.
static void AddInsulator(const Vec3 &ib) {
  AddCylinder(ib, Vec3Add(ib, Vec3{0, 0.055f, 0}), 0.044f, 0.040f, 6, kMetal,
              kMatMetal);
  AddCylinder(Vec3Add(ib, Vec3{0, 0.055f, 0}), Vec3Add(ib, Vec3{0, 0.175f, 0}),
              0.050f, 0.061f, 8, kCeramic, kMatCeramic);
  AddCylinder(Vec3Add(ib, Vec3{0, 0.175f, 0}), Vec3Add(ib, Vec3{0, 0.213f, 0}),
              0.061f, 0.038f, 8, kCeramic, kMatCeramic);
}

// BUILD-P9: STEP BOLTS — the ladder of alternating studs that lets a lineworker
// climb the shaft. Cheap, and the strongest "this is a steel distribution pole"
// tell available, because they break an otherwise unbroken 9 m stick.
static void AddStepBolts(const PoleSpec &p, float from, float to, float side) {
  int i = 0;
  for (float h = from; h <= to + 1e-3f; h += 0.30f, i++) {
    float s = (i & 1) ? -side : side;
    Vec3 a = PoleAxisAt(p, h);
    float r = TrunkRadius(p, h);
    AddCylinder({a.x + s * (r - 0.02f), h, a.z}, {a.x + s * (r + 0.14f), h, a.z},
                0.024f, 0.019f, 5, kSteelArm, kMatSteel);
  }
}

// BUILD-P9: GUY WIRE + ANCHOR. Every few spans the conductor is too heavy for
// the pole alone and a steel guy takes the load into a buried block. It is also
// the only STRAIGHT wire in the scene — every other line is a soft catenary —
// so it reads as structure rather than decoration, and its anchor blocks break
// up the empty verge.
static void AddGuyWire(const PoleSpec &p, float side, float zSign) {
  float hAtt = p.height - 0.95f;
  Vec3 att = PoleAxisAt(p, hAtt);
  float r = TrunkRadius(p, hAtt);
  att.x += side * (r + 0.11f);
  // attachment plate bolted flat to the shaft, then the turnbuckle eye
  AddBox({att.x - side * 0.06f, att.y, att.z}, {0.05f, 0.13f, 0.09f}, kSteelArm,
         kMatSteel);
  Vec3 mid{att.x + side * 0.13f, att.y - 0.44f, att.z + zSign * 0.28f};
  AddCylinder(att, mid, 0.026f, 0.023f, 5, kSteelGalv, kMatSteel);
  // the anchor: a rod leaning out of a concrete block on the verge
  Vec3 anc{p.x + side * 0.62f, 0.34f, p.z + zSign * 3.40f};
  AddWire(mid, anc, 0.03f, 0.015f, 5, kSteelGalv, kMatSteel);
  AddCylinder({anc.x - side * 0.30f, -0.10f, anc.z}, {anc.x, 0.42f, anc.z},
              0.042f, 0.032f, 6, kSteelGalv, kMatSteel);
  AddBox({anc.x - side * 0.13f, 0.14f, anc.z}, {0.36f, 0.14f, 0.36f},
         {0.230f, 0.222f, 0.208f}, kMatPaint);
}

// BUILD-P12: SLACK COIL. The single most recognisable object on a Japanese
// utility pole: a metre of spare black cable wound into a flat helix and hung
// off the side of the shaft on a bracket. It is pure silhouette, it costs
// nothing per pixel, and its absence is why a modelled pole reads as a pole
// in a diagram rather than a pole in a street.
//
// Built from short straight runs rather than one swept tube because AddWire
// already owns the tube builder and a catenary between two points 4 cm apart
// is indistinguishable from a straight one — this gets the shape for free.
static void AddSlackCoil(const PoleSpec &p, float h, float side, int turns,
                         float radius, float pitch) {
  float r0 = TrunkRadius(p, h);
  Vec3 a = PoleAxisAt(p, h);
  float cx = a.x + side * (r0 + radius);
  const int steps = turns * 12;
  // the bracket the coil hangs from — the coil is ATTACHED, not floating
  AddCylinder({a.x + side * (r0 - 0.02f), h + pitch * 0.5f, a.z},
              {cx, h + pitch * 0.5f, a.z}, 0.022f, 0.022f, 5, kSteelArm,
              kMatSteel);
  Vec3 prev{cx + radius, h + pitch * 0.5f, a.z};
  for (int i = 1; i <= steps; i++) {
    float t = (float)i / (float)steps;
    float ang = t * (float)turns * 6.28318f;
    // the coil flattens as it hangs: a wound cable under its own weight is an
    // ellipse, not a circle seen side-on
    Vec3 cur{cx + radius * 0.28f * std::cos(ang), h + pitch * 0.5f - t * pitch,
             a.z + radius * std::sin(ang)};
    // BUILD-P12 BUG: at five samples per turn and 17 mm radius each segment of
    // this helix was a 38 cm long, 3.4 cm thick STUBBY TUBE with a visible joint
    // to the next one — a row of beads hanging off the pole, which read as
    // exactly the "4-6 hanging cylinders in the sky" the user screenshotted.
    // Twelve samples a turn and a thinner cable puts every segment under the
    // size at which the beading is legible, and it is the same silhouette.
    AddWire(prev, cur, 0.0f, 0.0115f, 2, kCable, kMatCable);
    prev = cur;
  }
  // and the tail, running back up to the shaft: a coil whose cable simply
  // stops is the same "floating cylinder" mistake as an unterminated stub
  AddWire(prev, {a.x + side * (r0 + 0.03f), h + pitch * 0.5f + 0.06f, a.z},
          0.05f, 0.0115f, 6, kCable, kMatCable);
}

// BUILD-P12: SLACK LOOP. The big circular bight of service cable left hanging
// at a drop point so the cable is not pulled taut. Distinct from the coil: one
// turn, much larger, and it hangs in the plane of the road where it reads
// against the sky.
static void AddSlackLoop(const PoleSpec &p, float h, float side, float radius) {
  float r0 = TrunkRadius(p, h);
  Vec3 a = PoleAxisAt(p, h);
  const int steps = 22;
  Vec3 prev = a;
  for (int i = 1; i <= steps; i++) {
    float t = (float)i / (float)steps;
    float ang = 3.14159f * t;
    // hangs in the z/y plane, hanging off the road side of the shaft
    Vec3 cur{a.x + side * (r0 + 0.10f + 0.16f * std::sin(ang)),
             h - radius * (1.0f - std::cos(ang)) * 0.5f,
             a.z + radius * std::sin(ang) * 0.85f};
    AddWire(prev, cur, 0.0f, 0.0115f, 2, kCable, kMatCable);
    prev = cur;
  }
  AddWire(prev, a, 0.0f, 0.0115f, 2, kCable, kMatCable);
}

// BUILD-P12: a lattice radio mast. Not decoration: a cell mast standing behind
// the pole line is one of the few things that puts the corridor at a real
// scale, and its open truss silhouette is legible at 80 m where a solid box
// would be a smear. Four legs plus zigzag bracing, 24 m.
//
// BUILD-P14 got this wrong TWICE, and BUILD-P15 replaces it outright.
//
// P13 blamed a slack coil (a real object, swept too coarsely); P14 blamed the
// lattice cell mast's legs (also real, drawn too thin at the time). Both fixes
// were correct about the geometry and neither changed the READ, because both
// were still asking one object to do a job whose silhouette is a row of thin
// verticals. Three separate passes thickening members and clustering whips
// did not converge, and that is the signal: the shape itself is wrong, not
// its gauge.
//
// The measurement that settled it: a debug render with the sky flattened and
// the mast's radome panels recoloured magenta. The panels -- 0.42 m wide and
// TEN METRES tall, hung 0.46 m off a truss whose 0.11 m stand-offs are
// sub-pixel at the 120-160 m these masts actually sit at -- were 4 pale
// 1-2 px bars per mast with visible sky between them and the truss. That IS
// "cylinders in the air", verbatim. P14 introduced them.
//
// So the answer is not another thickness pass. It is to replace the object
// with one whose silhouette cannot be mistaken for a tube at any distance:
//
//   * a SPLAYED A-FRAME base, 2 x height * 0.175 half-width (9 m of feet on
//     a 26 m pylon). At 60 m that is 24 px of foot, tapering to 6 px at the
//     shoulder: an unmistakable triangle, and the single strongest "this is
//     a transmission pylon" cue there is;
//   * TWO LONG HORIZONTAL CROSSARMS, 0.34 x height half-length. 18 m of
//     horizontal at 60 m is ~46 px across the frame. A horizontal bar is the
//     one shape a vertical tube can never be confused with, and insulator
//     strings hanging from the tips finish the read;
//   * no pale radome panels and no whip cluster -- they were the artifact.
//
// It is also the more truthful object: a lattice transmission pylon standing
// behind a Japanese distribution line is a far more characteristic sight than
// a cell mast, and it carries the scale P12 wanted without needing to be
// legible member by member.
static void AddPylon(float x, float z, float height) {
  // BUILD-P15: dark galvanised lattice steel. A pylon 110-170 m out is behind
  // the aerial-perspective ramp, which pulls everything toward the haze
  // colour, so a mid-grey (0.30) member arrives at the eye only a few levels
  // below the sky and the whole tower reads as a faint smudge. Real
  // galvanised steelwork silhouettes against a dusk sky as a DARK lattice:
  // dropping the albedo to 0.17 is what makes the crossarm an actual bar
  // rather than a slightly-cloudier patch of sky.
  const Vec3 steel{0.170f, 0.172f, 0.180f};
  const Vec3 galv{0.215f, 0.220f, 0.232f};
  const float footHalf = height * 0.175f;   // splayed feet
  const float kneeY = height * 0.34f;       // where the A-frame closes
  const float kneeHalf = footHalf * 0.46f;
  const float topHalf = height * 0.048f;

  // concrete pad footings, so the legs land on something
  for (int c = 0; c < 4; c++) {
    float sx = (c == 0 || c == 3) ? -1.0f : 1.0f;
    float sz = (c < 2) ? -1.0f : 1.0f;
    AddBox({x + sx * footHalf, 0.16f, z + sz * footHalf},
           {0.78f, 0.16f, 0.78f}, {0.255f, 0.250f, 0.238f}, kMatConcrete);
  }
  // the splayed lower legs: fat enough to survive 4x MSAA at 60 m
  for (int c = 0; c < 4; c++) {
    float sx = (c == 0 || c == 3) ? -1.0f : 1.0f;
    float sz = (c < 2) ? -1.0f : 1.0f;
    AddCylinder({x + sx * footHalf, 0.12f, z + sz * footHalf},
                {x + sx * kneeHalf, kneeY, z + sz * kneeHalf}, 0.195f,
                0.145f, 5, steel, kMatSteel);
  }
  // the near-vertical upper legs, carrying on the same taper
  for (int c = 0; c < 4; c++) {
    float sx = (c == 0 || c == 3) ? -1.0f : 1.0f;
    float sz = (c < 2) ? -1.0f : 1.0f;
    AddCylinder({x + sx * kneeHalf, kneeY, z + sz * kneeHalf},
                {x + sx * topHalf, height, z + sz * topHalf}, 0.145f, 0.095f,
                5, steel, kMatSteel);
  }
  // a wide horizontal knee tie closing the A-frame: more horizontal, and it
  // sits right at the height the corridor silhouettes against
  for (int s = 0; s < 4; s++) {
    float ax = (s == 0 || s == 3) ? -kneeHalf : kneeHalf;
    float az = (s < 2) ? -kneeHalf : kneeHalf;
    AddCylinder({x + ax, kneeY, z + az}, {x - ax, kneeY, z + az}, 0.105f,
                0.105f, 4, steel, kMatSteel);
    AddCylinder({x + ax, kneeY, z + az}, {x + ax, kneeY, z - az}, 0.105f,
                0.105f, 4, steel, kMatSteel);
  }
  // lattice web on the splayed section only: this is the one place a real
  // pylon's bracing is wide enough on screen to be worth drawing, and the
  // diagonals are the second cue that reads "lattice" rather than "post"
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

  // ---- the two crossarms: the horizontal that settles the silhouette -----
  // Each is a real lattice boom built as a chord pair with verticals between
  // them. At corridor distance the pair reads as one 1.4 m deep bar, which is
  // exactly what a pylon boom looks like at 60 m, and the verticals keep it
  // honest when the dolly gets close.
  auto crossarm = [&](float y, float halfLen) {
    AddBox({x, y + 0.38f, z}, {halfLen, 0.38f, 0.42f}, steel, kMatSteel);
    AddBox({x, y - 0.34f, z}, {halfLen, 0.34f, 0.38f}, steel, kMatSteel);
    int bays = (int)(halfLen / 1.9f);
    for (int b = -bays; b <= bays; b++) {
      float bx = b * 1.9f;
      AddCylinder({x + bx, y - 0.34f, z}, {x + bx, y + 0.38f, z}, 0.105f,
                  0.105f, 4, steel, kMatSteel);
      // the knee brace back to the shaft at the inboard ends
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

  // ---- insulator strings: the detail that says "transmission", not "cell" --
  // BUILD-P16: THESE WERE THE USER'S "FLYING CYLINDERS", and build P15 shipped
  // them. Each string was ONE smooth 2.29 m cylinder of pale glaze hung off a
  // 0.26 m yoke — a capsule dangling from a bar, five per pylon and two
  // pylons, i.e. ten of them. At 110-170 m a 0.31 m tube is about 2 px wide
  // and a string is 12-16 px tall, so the eye reads a row of little vertical
  // pipes with sky between them and the crossarm: precisely the P14 artifact,
  // rebuilt on a new object.
  //
  // Two things are wrong with that, and thinning the tube fixes neither:
  //   * THE SHAPE. An insulator string is not a cylinder. It is a stack of
  //     sheds on a core, and the serrated silhouette is the entire reason the
  //     eye reads ceramic instead of pipe. This is the P13 coil lesson again:
  //     a swept feature needs its own structure, not just a smaller gauge.
  //   * THE VALUE. kCeramic is a 0.78 near-white glaze, which is correct for
  //     a pole insulator six metres from the lens. At 110 m the aerial ramp
  //     pulls that up to within a few levels of the sky, and a PALE object on
  //     a BRIGHT sky is exactly the artifact we are chasing. Real HV strings
  //     at dusk are a dark beaded line. The pole-top insulators keep the
  //     glaze; only the distant pylon strings go dark.
  const Vec3 shed{0.300f, 0.288f, 0.262f};   // reads as a silhouette at range
  auto insulator = [&](float ix, float iy) {
    AddBox({ix, iy + 0.13f, z}, {0.24f, 0.19f, 0.24f}, galv, kMatSteel);
    const float drop = 1.55f;               // string length, was 2.29 m
    // the core: thin, dark, and continuous so the string still reads as one
    // hanging object even where the sheds drop below a pixel
    AddCylinder({ix, iy - 0.05f, z}, {ix, iy - drop - 0.05f, z}, 0.055f,
                0.046f, 5, steel, kMatSteel);
    const int sheds = 7;
    for (int i = 0; i < sheds; i++) {
      float t = (float)(i + 1) / (float)(sheds + 1);
      float y = iy - 0.05f - drop * t;
      float rr = 0.150f - 0.052f * t;      // tapers toward the conductor
      AddCylinder({ix, y - 0.042f, z}, {ix, y + 0.042f, z}, rr, rr, 6,
                  shed, kMatCeramic);
    }
    // the conductor stub the string actually carries
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
  // BUILD-P12: the shaft material is per-pole now. Line A is concrete (the
  // reference), line B stays hot-dip galvanized steel from build P9 — a real
  // street has both, and the material contrast is what stops fifteen identical
  // shafts reading as one repeated prop.
  if (concrete)
    AddCylinder(base, top, 0.17f, 0.115f, 10, kConcreteGrey, kMatConcrete);
  else
    AddCylinder(base, top, 0.17f, 0.115f, 10, kSteelGalv, kMatSteel);
  // welded base flange standing in the ring of dirt kicked up around it
  AddCylinder({base.x, kGroundY - 0.02f, base.z}, {base.x, 0.10f, base.z},
              0.52f, 0.34f, 8, {0.30f, 0.24f, 0.18f}, kMatGround);
  AddCylinder({base.x, 0.03f, base.z}, {base.x, 0.29f, base.z}, 0.235f, 0.205f,
              8, kSteelArm, kMatSteel);

  // main crossarm near the top + a shorter one below, both rolled angle iron:
  // a web plate with a flange top and bottom catches a bright edge from the
  // sun, which is the whole difference between "steel arm" and "plank".
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

  // ceramic insulators: three on the main arm, TWO on the lower arm (build P8
  // built that lower arm and then hung nothing off it — dead hardware), and one
  // standing on the pole top.
  for (float off : {-1.05f, 0.0f, 1.05f})
    AddInsulator({armC.x + off, armY + 0.087f, armC.z});
  for (float off : {-0.55f, 0.55f})
    AddInsulator({arm2C.x + off, arm2Y + 0.087f, arm2C.z});
  AddCylinder(top, Vec3Add(top, Vec3{0, 0.18f, 0}), 0.05f, 0.058f, 8, kCeramic,
              kMatCeramic);
  AddCylinder(Vec3Add(top, Vec3{0, 0.18f, 0}), Vec3Add(top, Vec3{0, 0.24f, 0}),
              0.058f, 0.038f, 8, kCeramic, kMatCeramic);
  // lightning rod, sleeved alongside the top insulator so it cannot foul the
  // conductor that ties off at the glaze
  AddCylinder({top.x + 0.06f, p.height - 0.20f, top.z},
              {top.x + 0.13f, p.height + 0.60f, top.z}, 0.021f, 0.010f, 5,
              kSteelGalv, kMatSteel);

  // earth wire: a bare galvanized strand clipped down the shaft and bonded to
  // the base flange. BUILD-P7 made it follow the LEANING axis hop by hop;
  // BUILD-P9 measures the stand-off off TrunkRadius as well — the old fixed
  // 0.13 m put the first hop 4 cm INSIDE a 0.17 m shaft, i.e. inside the steel,
  // where nothing can see it. It runs on the back face, clear of the step bolts
  // and the number plate.
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
      // the clip, so the strand reads as CLIPPED ON rather than floating off
      Vec3 mid{(prev.x + cur.x) * 0.5f, (prev.y + cur.y) * 0.5f,
               (prev.z + cur.z) * 0.5f};
      Vec3 clip0 = PoleAxisAt(p, mid.y);      // start ON the shaft, not 5 cm
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
  // BUILD-P12: SLACK CABLE. Deterministic scatter — a real street has slack
  // cable on some poles and none on others, and a uniformly clean line is the
  // giveaway that it was laid out by a loop.
  if (concrete && ((((int)(p.z * 0.5f)) % 3) == 0))
    AddSlackCoil(p, 5.2f, -side, 6, 0.30f, 1.15f);
  if (concrete && ((((int)(p.z * 0.7f)) % 4) == 1))
    AddSlackLoop(p, 6.5f, side, 0.85f);

  // guys on every fourth-ish pole: one back and one forward where there is room
  if ((((int)(p.z * 0.4f)) % 3) == 0) {
    AddGuyWire(p, side, 1.0f);
    if ((((int)(p.z * 0.8f)) % 5) == 0) AddGuyWire(p, side, -1.0f);
  }

  if (p.serviceSpool) {
    // secondary service spool on the other flank (double-attachment poles).
    // BUILD-P8: measured off the trunk surface, not a fixed offset — at the
    // old x-0.24 the spool hung ~6 cm clear of the bark. BUILD-P9 gives it the
    // two flanged cheeks and hub that make it a spool instead of a stud.
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

  // BUILD-P4 TELECOM ARM: a second, lower crossarm carrying the phone/cable
  // bundles (Japanese poles stack a communications arm under the power arm).
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

  // a couple of CableTV-style cylindrical boxes bolted to the trunk (some
  // poles, deterministic), now with a bolted lid seam so they read as steel
  // enclosures rather than pipes
  if (((int(p.z * 7.0f)) % 3) == 0) {
    // BUILD-P9: this can was pinned to base.x/base.z — the pole's GROUND
    // position — and to a flat +0.20 m standoff. Both are wrong on a leaning,
    // tapering shaft: the enclosure slid sideways off the side of the pole it
    // is bolted to (up to lean * 4.2/height ~ 8 cm) and its lid seam and label
    // block floated with it, which is exactly what a "flying cylinder" is.
    // Measure off the LEANED axis and off TrunkRadius at the height it is
    // mounted, embed it 3 cm into the shaft, and hang it on two flat straps
    // that visibly reach back to the wood.
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
    // BUILD-P9: the can hangs OFF the arm in z instead of standing in the arm's
    // own plane, where it used to run straight through the telecom arm below
    // it. Cooling fins, a tap-changer box and two cut-outs on the arm front
    // make it the densest piece of hardware on the pole.
    Vec3 tc{armC.x + 0.62f, armY - 1.42f, armC.z + 0.44f};
    // hanger straps: two flat bars carrying the can back to the crossarm
    for (float bz : {-0.13f, 0.13f})
      AddBox({tc.x + bz, armY - 0.44f, (tc.z + armC.z) * 0.5f},
             {0.045f, 0.47f, 0.24f}, kSteelArm, kMatSteel);
    AddCylinder(Vec3Add(tc, Vec3{-0.1f, -0.55f, 0}),
                Vec3Add(tc, Vec3{0.1f, 0.55f, 0}), 0.34f, 0.34f, 10, kMetal,
                kMatMetal);
    // radiator fins on the two faces the sun rakes across
    for (int fi = 0; fi < 5; fi++) {
      float fy = tc.y - 0.40f + 0.20f * fi;
      AddBox({tc.x, fy, tc.z + 0.34f}, {0.30f, 0.028f, 0.085f}, kSteelArm,
             kMatSteel);
      AddBox({tc.x, fy, tc.z - 0.34f}, {0.30f, 0.028f, 0.085f}, kSteelArm,
             kMatSteel);
    }
    AddBox({tc.x, tc.y + 0.30f, tc.z}, {0.40f, 0.16f, 0.16f}, kMetal, kMatMetal);
    AddBox({tc.x, tc.y - 0.34f, tc.z}, {0.10f, 0.22f, 0.10f}, kMetal, kMatMetal);
    // tap changer / terminal box on the side of the can
    AddBox({tc.x + 0.30f, tc.y - 0.10f, tc.z + 0.10f}, {0.10f, 0.16f, 0.13f},
           kSteelArm, kMatSteel);
    // two ceramic bushings on the can's crown + their drop leads.
    // BUILD-P8: the leads used to stop in mid-air 0.5 m above the can. They
    // now run up to the crossarm insulator they actually feed.
    for (float bz : {-0.12f, 0.12f}) {
      Vec3 bt{tc.x, tc.y + 0.46f, tc.z + bz};
      AddCylinder(bt, Vec3Add(bt, Vec3{0, 0.16f, 0}), 0.045f, 0.038f, 6,
                  kCeramic, kMatCeramic);
      AddWire(Vec3Add(bt, Vec3{0, 0.17f, 0}),
              ArmInsulatorTop(p, bz < 0.0f ? 0.0f : 1.05f), 0.05f, 0.011f, 6,
              kCable, kMatCable);
    }
    // BUILD-P9: cut-out fuses and lightning arresters on the arm front. They
    // hang clear of the arm plane (z + 0.17) so they do not intersect the knee
    // braces, and they are the small bright verticals that make the pole look
    // like it is doing real work.
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
// BUILD-P8: wall/roof palettes come DOWN a stop. At 0.29-0.52 albedo plus a
// bright dusk ambient, every house in the mid-distance clipped toward the
// same cream as the sky and the suburb read as blank white slabs.
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
  float streetX = face * w * 0.5f;           // the road-facing wall

  // concrete plinth: the house sits ON something instead of hovering
  AddBox({x, 0.09f, z}, {w * 0.5f + 0.07f, 0.09f, d * 0.5f + 0.07f},
         {0.150f, 0.142f, 0.132f}, kMatPaint);
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

  // ---- BUILD-P8 ROOF CLUTTER: the black plastic water tank on stilts, a TV
  // aerial and an aircon box. Every tiled roof in Japan carries some
  // combination, and their silhouettes are most of what makes a distant
  // roofline read as a house instead of a wedge.
  if (pick % 3 != 1) {
    float tx = x - w * 0.22f, tz = z + d * 0.10f;
    const Vec3 tank{0.055f, 0.058f, 0.062f};
    for (int l = 0; l < 4; l++) {
      float lx = tx + (l & 1 ? 0.26f : -0.26f);
      float lz = tz + (l & 2 ? 0.22f : -0.22f);
      // legs start well BELOW the roof plane: the roof slopes away from the
      // ridge, so a leg based at yRidge hovered in the air
      AddCylinder({lx, yRidge - 0.45f, lz}, {lx, yRidge + 0.52f, lz}, 0.045f,
                  0.045f, 4, {0.090f, 0.086f, 0.082f}, kMatMetal);
    }
    AddCylinder({tx, yRidge + 0.52f, tz}, {tx, yRidge + 1.02f, tz}, 0.34f, 0.31f,
                8, tank, kMatMetal);
  }
  {   // TV aerial: a mast with two crossbars
    float ax = x + w * 0.28f, az = z - d * 0.22f;
    AddCylinder({ax, yRidge - 0.40f, az}, {ax, yRidge + 0.95f, az}, 0.028f,
                0.020f, 4, {0.120f, 0.118f, 0.115f}, kMatMetal);
    for (int k = 0; k < 2; k++)
      AddBox({ax, yRidge + 0.62f + 0.24f * k, az}, {0.30f, 0.016f, 0.016f},
             {0.120f, 0.118f, 0.115f}, kMatMetal);
  }
  if (pick % 2 == 0) {   // aircon condenser, bracketed ON the wall
    float cx2 = x + streetX + face * 0.15f;
    AddBox({cx2, 1.85f, z + d * 0.12f}, {0.20f, 0.17f, 0.28f},
           {0.165f, 0.160f, 0.155f}, kMatMetal);
  }

  // ---- windows: frame + sill + glazing on the street flank and the gable
  const Vec3 frame{0.320f, 0.300f, 0.270f};
  const Vec3 glass{0.060f, 0.075f, 0.095f};
  for (int i = 0; i < 3; i++) {
    float wx = x + streetX + face * 0.03f;
    float wz = z + (float)(i - 1) * (d * 0.30f);
    float wy = 0.10f + h * 0.60f;
    AddBox({wx + face * 0.02f, wy, wz}, {0.05f, 0.26f, 0.34f}, frame, kMatPaint);
    // BUILD-P9: at dusk roughly a third of the windows are lit. Warm emissive
    // panes are the strongest "someone lives in this box" cue a suburban frame
    // has, and they break up the flat wall slabs that read as untextured grey
    // boxes in the user's screenshots.
    bool lit = ((i + pick) % 3) == 0;
    AddBox({wx + face * 0.06f, wy, wz}, {0.02f, 0.21f, 0.29f},
           lit ? Vec3{0.96f, 0.71f, 0.40f} : glass, lit ? kMatGlow : kMatGlass);
    if (lit) {   // glazing bars — a lit pane with no frame reads as a decal
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

// BUILD-P8: PROPERTY LINES. The most Japanese thing about a suburban street is
// that you never see the neighbours' gardens: every plot is walled off with a
// concrete block wall under a tiled cap, broken by a gate post now and then,
// with a hedge behind it. They also give the corridor the mid-ground rhythm
// the bare verges never had — without them the eye runs straight from the
// gravel to the house fronts with nothing in between.
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
    if ((i % 3) == 1) {                       // gate post + cap
      AddBox({x, 1.05f, z}, {0.17f, 1.05f, 0.17f}, cap, kMatPaint);
      AddBox({x, 2.14f, z}, {0.22f, 0.05f, 0.22f}, cap, kMatRoof);
    }
    if ((i % 2) == 0) {                       // hedge behind the wall
      float hx = x + mirror * (0.95f + 0.55f * (float)((i * 3) % 3) / 3.0f);
      AddBox({hx, 0.58f, cz}, {0.46f, 0.58f, seg * 0.40f},
             {0.048f, 0.078f, 0.040f}, kMatLeaf);
    }
    z += seg + 0.35f;
    i++;
  }
}

// BUILD-P8: a streetlight on the road side of a pole — arm, lamp head and a
// glowing lens. Real poles carry these; their glow is one of the few warm
// accents that reads at dusk against all that orange.
static void AddStreetLight(const PoleSpec &p, float side) {
  float armY = p.height - 2.35f;
  Vec3 a = PoleAxisAt(p, armY);
  float tipx = a.x + side * 2.15f;
  AddCylinder({a.x, armY, a.z}, {tipx, armY + 0.30f, a.z}, 0.055f, 0.045f, 6,
              {0.140f, 0.138f, 0.135f}, kMatMetal);
  AddBox({tipx + side * 0.12f, armY + 0.26f, a.z}, {0.26f, 0.07f, 0.15f},
         {0.150f, 0.148f, 0.145f}, kMatMetal);
  // lens sits just BELOW the housing (which spans armY+0.19..+0.33) instead
  // of intersecting it
  AddBox({tipx + side * 0.12f, armY + 0.145f, a.z}, {0.21f, 0.045f, 0.12f},
         {0.88f, 0.68f, 0.42f}, kMatGlow);
}

// BUILD-P8: a lit drinks machine by the kerb. Vending machines are lit at
// dusk in every Japanese street and throw the only cool light in the frame.
static void AddVendingMachine(float x, float z, float face) {
  AddBox({x, 0.78f, z}, {0.42f, 0.78f, 0.32f}, {0.185f, 0.150f, 0.120f},
         kMatMetal);
  // BUILD-P8: the lit front has to sit ON the cabinet's outer face (0.42 m),
  // not at 0.33 m where it was buried inside the metal — a hidden emissive
  // panel is why the machine rendered as nothing at all.
  AddBox({x + face * 0.46f, 0.80f, z}, {0.03f, 0.62f, 0.26f},
         {0.88f, 0.93f, 1.00f}, kMatGlow);
  AddBox({x + face * 0.45f, 0.18f, z}, {0.03f, 0.12f, 0.24f},
         {0.30f, 0.26f, 0.22f}, kMatMetal);
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
  for (const PoleSpec &p : lineA) AddPole(p, /*concrete=*/true);
  gLineA = lineA;
  // BUILD-P8: streetlights on alternate line A poles, reaching over the road
  for (int i = 0; i < (int)lineA.size(); i += 2) AddStreetLight(lineA[i], 1.0f);

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
              14, kCable, kMatCable);
    AddWire(PoleTopInsulatorTop(p), PoleTopInsulatorTop(q), sag * 0.8f, 0.032f,
            14, kCableOld, kMatCable);
    // BUILD-P9: two more tiers — the lower-arm conductors. Line A now carries
    // six spans of wire per bay (3 top arm + pole top + 2 lower arm) instead of
    // four, and it is that DENSITY, not the sag curve, that reads as a real
    // distribution corridor.
    for (float off : {-0.55f, 0.55f})
      AddWire(Arm2InsulatorTop(p, off), Arm2InsulatorTop(q, off), sag * 0.74f,
              0.024f, 12, kCable, kMatCable);
  }

  // ---- BUILD-P12: THE CABLE WEB. This is what the reference photograph is
  // actually about. Six neat distribution conductors read as a power line
  // DIAGRAM. What makes a real pole is the sixteen thin black telecom drops
  // and cross-connects strung between the same two poles at different
  // heights, most of them slack, none of them in a plane — the web that fills
  // every corner of the frame between the arms. One tube each, and the entire
  // silhouette of the upper half of the shot.
  for (int i = 0; i + 1 < (int)lineA.size(); i++) {
    const PoleSpec &p = lineA[i];
    const PoleSpec &q = lineA[i + 1];
    for (int k = 0; k < 16; k++) {
      float fk = (float)k;
      // pseudo-random but DETERMINISTIC offsets along the telecom bracket, so
      // the same web is generated every run and the benchmark stays comparable
      float u0 = fk * 0.37f;  u0 -= std::floor(u0);
      float u1 = fk * 0.61f + 0.23f; u1 -= std::floor(u1);
      float u2 = fk * 0.29f;  u2 -= std::floor(u2);
      float u3 = fk * 0.83f;  u3 -= std::floor(u3);
      Vec3 a = TelecomBracketTop(p, -0.66f + 1.32f * u0);
      Vec3 b = TelecomBracketTop(q, -0.66f + 1.32f * u1);
      float r = 0.010f + 0.007f * u3;
      float sag = 0.18f + 0.62f * u2;
      AddWire(a, b, sag, r, 7, kCableOld, kMatCable);
    }
  }

  // ---- BUILD-P12: something tall beyond the pole line. Pure scale cue, but
  // a street with no vertical past the distribution poles has no distance.
  //
  // BUILD-P14 moved the cell masts out of the corridor (the dolly runs
  // x = +2, z = 4..165, so a mast at (17.5, 101) passed 15 m off the lens).
  // BUILD-P15 replaces the object outright -- see the long note on AddPylon:
  // three passes of thickening members had not fixed the read, so the shape
  // had to change. Placement is now tuned to the dolly's ACTUAL working
  // range rather than its whole loop: the still and the GIF are shot at
  // t = 2..17 s, i.e. z = 9..48 m, and at z = 212 the pylons were 190-230 m
  // out — a 44 px crossarm washed almost to the haze. At z = 140/172 they are
  // 103-173 m away across the shot window, which puts the crossarm at 46-62 px
  // and the whole tower at 100-133 px: large enough to read as a pylon, small
  // enough to stay a scale cue. Both still clear the corridor laterally
  // (x = +48 and -60 against pole lines at -3.4 and +8.6).
  AddPylon(48.0f, 140.0f, 26.0f);
  AddPylon(-60.0f, 172.0f, 31.0f);

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
    for (float off : {-1.05f, 1.05f})
      AddWire(ArmInsulatorTop(p, off), ArmInsulatorTop(q, off), 0.88f, 0.026f,
              12, kCableOld, kMatCable);
    // BUILD-P9: line B was the sparse one — top-of-pole plus the two lower-arm
    // spans give it the same tiered look as the main line, which is what turns
    // the second row from wallpaper into a second plane of poles.
    AddWire(PoleTopInsulatorTop(p), PoleTopInsulatorTop(q), 0.70f, 0.030f, 12,
            kCableOld, kMatCable);
    for (float off : {-0.55f, 0.55f})
      AddWire(Arm2InsulatorTop(p, off), Arm2InsulatorTop(q, off), 0.62f, 0.024f,
              12, kCableOld, kMatCable);
  }

  // ---- the crossing spans: line B feeds into line A (the tangle). BUILD-P2
  // FIX: crossings now land on real insulator tops of both poles.
  for (int i = 0; i < 4; i++) {
    const PoleSpec &p = lineB[i];
    const PoleSpec &q = lineA[i + 1];
    AddWire(ArmInsulatorTop(p, 1.05f), ArmInsulatorTop(q, -1.05f), 1.30f,
            0.024f, 16, kCable, kMatCable);
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
  // BUILD-P8: the walled property lines between road and front gardens, plus a
  // couple of lit vending machines at the kerb.
  AddPropertyLine(-9.0f, -14.0f, 168.0f, -1.0f);
  AddPropertyLine(13.0f, -8.0f, 176.0f, 1.0f);
  AddVendingMachine(-7.4f, 30.0f, 1.0f);
  AddVendingMachine(11.6f, 96.0f, -1.0f);
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
    AddTelecomBundle(TelecomBracketTop(p, -0.70f), TelecomBracketTop(q, -0.70f),
                     i * 2 + 1, 0.022f, 12, kCable);
    AddTelecomBundle(TelecomBracketTop(p, 0.70f), TelecomBracketTop(q, 0.70f),
                     i * 2 + 2, 0.022f, 12, kCable);
    // BUILD-P9: the middle bracket was carrying nothing at all. One more
    // bundle per bay turns the telecom tier from two parallel cables into the
    // thick three-deep band every Japanese pole line has.
    AddTelecomBundle(TelecomBracketTop(p, 0.0f), TelecomBracketTop(q, 0.0f),
                     i * 2 + 13, 0.020f, 12, kCableOld);
  }
  for (int i = 0; i + 1 < (int)lineB.size(); i++) {
    const PoleSpec &p = lineB[i];
    const PoleSpec &q = lineB[i + 1];
    AddTelecomBundle(TelecomBracketTop(p, 0.0f), TelecomBracketTop(q, 0.0f),
                     i * 3 + 40, 0.020f, 10, kCableOld);
  }
  // slack cross-line telecom loops B -> A (the messy diagonal drips)
  for (int i = 0; i < 5; i++) {
    const PoleSpec &p = lineB[i];
    const PoleSpec &q = lineA[i + 1];
    AddTelecomBundle(TelecomBracketTop(p, -0.70f), TelecomBracketTop(q, -0.70f),
                     i * 5 + 77, 0.018f, 14, kCable);
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
    // junction cans stay mounted on the pole wall — BUILD-P8: braced against
    // the measured trunk radius instead of a fixed 0.30 m offset.
    Vec3 ja = PoleAxisAt(p, 3.05f);
    float jr = TrunkRadius(p, 3.05f);
    AddBox({ja.x - jr - 0.06f, 3.10f, ja.z}, {0.16f, 0.10f, 0.12f}, kMetal,
           kMatMetal);
    // BUILD-P10: the drop stub USED TO JUST STOP. It left the housing at 3.05 m,
    // angled down and out, and ended at 2.45 m in clear air — a 0.6 m metal
    // cylinder hanging off a pole with nothing on the end of it, which is
    // exactly what reads as a flying cylinder at a glance. A real service drop
    // ends in a weatherhead: a boot where the cable enters, and the cable
    // itself running away to a termination. Here it runs to the nearest house
    // eave bracket when one is in range, and otherwise into a drip loop back
    // onto the pole — which is what an unused drop actually does.
    Vec3 stubEnd{ja.x - jr - 0.10f, 2.45f, ja.z};
    AddCylinder({ja.x - jr, 3.05f, ja.z}, stubEnd, 0.09f, 0.09f, 8, kMetal,
                kMatMetal);
    AddCylinder(stubEnd, {stubEnd.x - 0.04f, 2.30f, stubEnd.z}, 0.075f, 0.105f,
                8, kMetal, kMatMetal);   // weatherhead boot
    if (best >= 0) {
      AddWire({stubEnd.x - 0.04f, 2.24f, stubEnd.z}, gDropAnchors[best], 0.42f,
              0.016f, 8, kCableOld, kMatCable);
    } else {
      AddWire({stubEnd.x - 0.04f, 2.24f, stubEnd.z},
              {ja.x - jr - 0.02f, 2.86f, ja.z}, 0.16f, 0.016f, 8, kCableOld,
              kMatCable);
    }
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
    // BUILD-P10: CAST KERBS. A road that meets the ground in a straight line is
    // the loudest non-photographic tell in any street shot — there is nothing
    // in it to catch light, nothing to cast a shadow, and the eye reads the
    // carriageway as a decal. The kerb is 0.32 m wide, 16 cm proud, and gets
    // its own material so the shader can put aggregate in the concrete and
    // standing water in the gutter beside it.
    for (int e = 0; e < 2; e++) {
      float sgn = e ? 1.0f : -1.0f;
      float xc = (e ? kRoadX + kRoadHalf : kRoadX - kRoadHalf) + sgn * 0.16f;
      AddBox({xc, kKerbTopY * 0.5f, (z0 + z1) * 0.5f}, {0.16f, kKerbTopY * 0.5f,
             (z1 - z0) * 0.5f},
             {0.245f, 0.238f, 0.225f}, kMatKerb);
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
  std::snprintf(line1, sizeof(line1), "FPS: %d   build P16   scene 4: LainBench", gFps);
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
  std::snprintf(hint, sizeof(hint), "LainBench score");

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
  gWindowWidth = winW;
  gWindowHeight = winH;

  // BUILD-P10: 4x MULTISAMPLE. Nothing else in this pass moves the image closer
  // to a photograph. The frame is a tangle of 3-4 cm cylinders crossing a
  // bright sky: without coverage antialiasing every wire is a hard stair-step
  // and the whole corridor reads as vector art. MSAA fixes exactly that and
  // costs nothing per fragment on any driver from the GMA 950 up, because it
  // runs at sample rate, not pixel rate.
  // Safety: a driver that cannot do MSAA is allowed to fail context creation,
  // so the whole window is rebuilt with the attributes off rather than
  // dropping the scene. The result is printed so the active mode is never a
  // guess.
  bool msaa = true;
  for (int attempt = 0; attempt < 2; attempt++) {
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, msaa ? 1 : 0);
    SDL_GL_SetAttribute(SDL_GL_SAMPLES, msaa ? 4 : 0);
    gWindow = SDL_CreateWindow(gName, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                               winW, winH,
                               SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!gWindow) {
      std::fprintf(stderr, "Window could not be created! SDL_Error: %s\n", SDL_GetError());
      return EXIT_FAILURE;
    }
    gContext = SDL_GL_CreateContext(gWindow);
    if (gContext) break;
    // this driver refused the multisample request: tear it down and go plain
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
  glViewport(0, 0, gWindowWidth, gWindowHeight);
  int samples = 0;
  SDL_GL_GetAttribute(SDL_GL_SAMPLES, &samples);
  std::printf("Antialiasing: %dx MSAA%s\n", samples, samples > 1 ? "" : " (unavailable)");
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
