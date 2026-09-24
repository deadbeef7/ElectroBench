// ElectroBench — the ocean scene of the single ElectroBench binary: a
// recreation of the 3DMark2001 SE "Nature" pixel shader 1.4 workload
// (sea + sky + clouds + reflections) on OpenGL 3.3 core.
//
// This translation unit is NOT a program of its own. It exports
// RunOceanScene(), which main.cxx calls as the second scene of the one and
// only ElectroBench executable (the same in-process SDL session; no second
// binary, no child process).
//
// What is rendered, in the spirit of the 2001 original:
//   * Procedural sky dome with two drifting fBm cloud layers, captured once
//     per frame into a cubemap (the PS1.4-era trick to get dynamic
//     reflections without true reflectors).
//   * A large ocean grid displaced by a 6-octave wave function in the vertex
//     shader (PS1.4 had no vertex textures; this is the GPU-age upgrade).
//   * The water fragment shader mirrors the phases of an asm ps_1_4 shader:
//       phase 1 (addressing)      : two scrolled ripple-gradient lookups
//       phase 2 (dependent read)  : perturbed ray -> environment cubemap
//       phase 3 (address + blend) : sun glitter, fresnel blend, haze
//   * Default resolution 1366x768 like the original ElectroBench, 45 s run
//     and a final score printed to the console.
//
// Controls: long-click + move to orbit the camera, mouse wheel to zoom,
// F to toggle the fly-through camera path, ESC to quit.
//
// Debug flags:
//   --screenshot FILE   capture the framebuffer to FILE (PPM) at the times in
//                       --shot-times (default "4,20") and exit; used by the
//                       headless visual test (llvmpipe has no real GPU sync).

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include "../lib/asset_path.hxx"
#include "font_atlas.hxx" // shared HUD font data and atlas layout

// ------------------------------------------------------------------ constants
#define NAME "ElectroBench - Dusk Ocean"
#define WIDTH 1366
#define HEIGHT 768
#define BENCH_MILLISECONDS 45000 // 45 s like the original ElectroBench

static const int kSeaResolution = 384;   // vertices per side of the ocean grid (2.25x the
                                         // vertex count of 256 — pushed for realism)
static const float kSeaSize = 4096.0f;   // world size of the ocean patch: reaches past the
                                         // visible horizon in every direction the camera can look
static const int kEnvMapSize = 768;      // cubemap face resolution — higher fidelity reflections
static const int kNoiseSize = 256;       // fBm noise texture size
static const int kRippleSize = 256;      // ripple gradient texture size
#define MAX_CLOUDS 6                     // must match sky_frag.glsl
// Long low cumulus banks: azimuth, centre elevation, angular half-HEIGHT and
// azimuthal stretch (>1 = elongated). Small radii + stretch give the wide,
// shallow masses the reference shows; the mid elevations keep them in the
// upper sky instead of hugging the horizon.
static const float kCloudAzim[MAX_CLOUDS] = {0.35f, 0.78f, 5.92f, 2.20f, 3.95f, 4.60f};
static const float kCloudElev[MAX_CLOUDS] = {0.245f, 0.330f, 0.200f, 0.290f, 0.360f, 0.150f};
static const float kCloudRad[MAX_CLOUDS]  = {0.042f, 0.034f, 0.038f, 0.033f, 0.031f, 0.020f};
static const float kCloudStretch[MAX_CLOUDS] = {2.8f, 2.5f, 2.6f, 2.3f, 2.2f, 3.6f};
static const int kFoamSize = 256;        // foam texture size

// Slow wind drift: cloud azimuths crawl a little every second so the banks
// slide across the sky. Both the sky pass and the sea's cloud shadows upload
// the same drifted array, so shadows always sit exactly under their clouds.
// Signs chosen so no bank drifts into the sun's azimuth (~0.54 rad): the
// glitter path and sun disc must survive the whole run.
static const float kCloudDrift[MAX_CLOUDS] = {-0.004f, 0.004f, -0.003f, 0.003f, 0.0035f};
static float gCloudAzimDrift[MAX_CLOUDS];
static void UpdateCloudAzim(float t) {
  for (int i = 0; i < MAX_CLOUDS; i++)
    gCloudAzimDrift[i] = kCloudAzim[i] + kCloudDrift[i] * t;
}

// ------------------------------------------------------------ tiny math utils
struct Vec3 {
  float x, y, z;
};

static inline Vec3 Vec3Add(const Vec3 &a, const Vec3 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static inline Vec3 Vec3Sub(const Vec3 &a, const Vec3 &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static inline Vec3 Vec3Scale(const Vec3 &a, float s) { return {a.x * s, a.y * s, a.z * s}; }
static inline float Vec3Dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3 Vec3Cross(const Vec3 &a, const Vec3 &b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static inline Vec3 Vec3Normalize(const Vec3 &v) {
  float len = std::sqrt(Vec3Dot(v, v));
  if (len <= 1e-8f) return {0.0f, 0.0f, 0.0f};
  return {v.x / len, v.y / len, v.z / len};
}

// Column-major 4x4 matrices (OpenGL convention).
using Mat4 = std::array<float, 16>;

static void Mat4Identity(Mat4 &m) { m.fill(0.0f); m[0] = m[5] = m[10] = m[15] = 1.0f; }

static void Mat4Multiply(Mat4 &out, const Mat4 &a, const Mat4 &b) {
  Mat4 r;
  for (int c = 0; c < 4; c++)
    for (int row = 0; row < 4; row++) {
      float s = 0.0f;
      for (int k = 0; k < 4; k++) s += a[k * 4 + row] * b[c * 4 + k];
      r[c * 4 + row] = s;
    }
  out = r;
}

static void Mat4Perspective(Mat4 &m, float fovYDeg, float aspect, float zNear, float zFar) {
  m.fill(0.0f);
  float f = 1.0f / std::tan(fovYDeg * 3.14159265358979f / 360.0f);
  m[0] = f / aspect;
  m[5] = f;
  m[10] = (zFar + zNear) / (zNear - zFar);
  m[11] = -1.0f;
  m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
}

static void Mat4LookAt(Mat4 &m, const Vec3 &eye, const Vec3 &center, const Vec3 &up) {
  Vec3 f = Vec3Normalize(Vec3Sub(center, eye)); // forward
  Vec3 s = Vec3Normalize(Vec3Cross(f, up));     // right
  Vec3 u = Vec3Cross(s, f);                     // corrected up basis
  Mat4Identity(m);

  // Column-major OpenGL look-at matrix:
  // columns are right, up, -forward, translation.
  m[0] = s.x;   m[4] = s.y;   m[8]  = s.z;
  m[1] = u.x;   m[5] = u.y;   m[9]  = u.z;
  m[2] = -f.x;  m[6] = -f.y;  m[10] = -f.z;
  m[12] = -Vec3Dot(s, eye);
  m[13] = -Vec3Dot(u, eye);
  m[14] = Vec3Dot(f, eye);
}

// --------------------------------------------------------------- file helpers
static char *ReadTextFile(const char *filename) {
  FILE *f = std::fopen(filename, "rb");
  if (!f) return nullptr;
  std::fseek(f, 0, SEEK_END);
  long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  char *buf = (char *)std::malloc((size_t)size + 1);
  size_t rd = std::fread(buf, 1, (size_t)size, f);
  buf[rd] = '\0';
  std::fclose(f);
  return buf;
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
  GLint ok = GL_FALSE;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
    std::fprintf(stderr, "%s shader (%s) failed to compile:\n%s\n",
                 type == GL_VERTEX_SHADER ? "Vertex" : "Fragment", name, log);
  }
  return sh;
}

static Program LinkProgram(const char *vsPath, const char *fsPath) {
  Program p;
  char *vs = ReadTextFile(vsPath);
  char *fs = ReadTextFile(fsPath);
  if (!vs || !fs) {
    std::fprintf(stderr, "Cannot read shaders %s / %s (run from the repo root)\n", vsPath, fsPath);
    std::exit(EXIT_FAILURE);
  }
  GLuint v = CompileShader(GL_VERTEX_SHADER, vs, vsPath);
  GLuint f = CompileShader(GL_FRAGMENT_SHADER, fs, fsPath);
  p.handle = glCreateProgram();
  glAttachShader(p.handle, v);
  glAttachShader(p.handle, f);
  glLinkProgram(p.handle);
  GLint ok = GL_FALSE;
  glGetProgramiv(p.handle, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetProgramInfoLog(p.handle, sizeof(log), nullptr, log);
    std::fprintf(stderr, "Program %s/%s failed to link:\n%s\n", vsPath, fsPath, log);
  }
  glDeleteShader(v);
  glDeleteShader(f);
  std::free(vs);
  std::free(fs);
  return p;
}

// ------------------------------------------------------------- procedural tex
// Value-noise helpers used to generate all the textures on the CPU at startup.
static float xf_mod(float x, float m);
static float yf_mod(float y, float m);

static float Hash01(int x, int y, int seed) {
  unsigned int h = (unsigned int)(x * 374761393u + y * 668265263u + seed * 1442695041u);
  h = (h ^ (h >> 13)) * 1274126177u;
  h = h ^ (h >> 16);
  return (h & 0x7fffffffu) / (float)0x7fffffffu;
}

static float ValueNoise(float u, float v, int seed) {
  float x = u, y = v;
  int xi = (int)std::floor(x), yi = (int)std::floor(y);
  float xf = x - (float)xi, yf = y - (float)yi;
  float u2 = xf * xf * (3.0f - 2.0f * xf);
  float v2 = yf * yf * (3.0f - 2.0f * yf);
  float a = Hash01(xi, yi, seed),     b = Hash01(xi + 1, yi, seed);
  float c = Hash01(xi, yi + 1, seed), d = Hash01(xi + 1, yi + 1, seed);
  return a + (b - a) * u2 + (c - a) * v2 + (a - b - c + d) * u2 * v2;
}

static float TileableFbm(float u, float v, int octaves, float baseFreq, int seed) {
  // Tileable in [0,1) by sampling the noise on a torus through 4 blends.
  float sum = 0.0f, amp = 0.5f, freq = baseFreq;
  for (int o = 0; o < octaves; o++) {
    float x = u * freq, y = v * freq;
    float n00 = ValueNoise(x, y, seed);
    float n10 = ValueNoise(x - freq, y, seed);
    float n01 = ValueNoise(x, y - freq, seed);
    float n11 = ValueNoise(x - freq, y - freq, seed);
    float bx = xf_mod(x, freq), by = yf_mod(y, freq);
    float nx = n00 + (n10 - n00) * bx;
    float ny = n01 + (n11 - n01) * bx;
    float n = nx + (ny - nx) * by;
    sum += n * amp;
    amp *= 0.5f;
    freq *= 2.0f;
  }
  return sum;
}

static float xf_mod(float x, float m) { float t = x / m; return (t - std::floor(t)) * m; }
static float yf_mod(float y, float m) { float t = y / m; return (t - std::floor(t)) * m; }

// Tileable ripple gradient texture: two phase-shifted RG noise channels.
static GLuint CreateRippleTexture() {
  std::vector<unsigned char> px(kRippleSize * kRippleSize * 2);
  for (int y = 0; y < kRippleSize; y++) {
    for (int x = 0; x < kRippleSize; x++) {
      float u = x / (float)kRippleSize, v = y / (float)kRippleSize;
      float a = TileableFbm(u, v, 4, 3.0f, 11) - 0.5f;
      float b = TileableFbm(u, v, 4, 3.0f, 71) - 0.5f;
      float a2 = TileableFbm(u + 0.37f, v + 0.19f, 4, 6.0f, 23) - 0.5f;
      float b2 = TileableFbm(u + 0.61f, v + 0.44f, 4, 6.0f, 37) - 0.5f;
      px[(y * kRippleSize + x) * 2 + 0] = (unsigned char)std::lround(std::fmin(1.0f, std::fmax(0.0f, 0.5f + a + a2 * 0.5f)) * 255.0f);
      px[(y * kRippleSize + x) * 2 + 1] = (unsigned char)std::lround(std::fmin(1.0f, std::fmax(0.0f, 0.5f + b + b2 * 0.5f)) * 255.0f);
    }
  }
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, kRippleSize, kRippleSize, 0, GL_RG, GL_UNSIGNED_BYTE, px.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glGenerateMipmap(GL_TEXTURE_2D);
  return tex;
}

// RGBA fBm noise for the cloud shader (R/G used as two independent fields).
static GLuint CreateSkyNoiseTexture() {
  std::vector<unsigned char> px(kNoiseSize * kNoiseSize * 4);
  for (int y = 0; y < kNoiseSize; y++) {
    for (int x = 0; x < kNoiseSize; x++) {
      float u = x / (float)kNoiseSize, v = y / (float)kNoiseSize;
      float r = TileableFbm(u, v, 2, 4.0f, 101);
      float g = TileableFbm(u, v, 2, 6.0f, 202);
      px[(y * kNoiseSize + x) * 4 + 0] = (unsigned char)std::lround(r * 255.0f);
      px[(y * kNoiseSize + x) * 4 + 1] = (unsigned char)std::lround(g * 255.0f);
      px[(y * kNoiseSize + x) * 4 + 2] = (unsigned char)std::lround(r * g * 255.0f);
      px[(y * kNoiseSize + x) * 4 + 3] = 255;
    }
  }
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kNoiseSize, kNoiseSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glGenerateMipmap(GL_TEXTURE_2D);
  return tex;
}

// Grayscale foam / wave-detail texture.
static GLuint CreateFoamTexture() {
  std::vector<unsigned char> px(kFoamSize * kFoamSize * 4);
  for (int y = 0; y < kFoamSize; y++) {
    for (int x = 0; x < kFoamSize; x++) {
      float u = x / (float)kFoamSize, v = y / (float)kFoamSize;
      float n = TileableFbm(u, v, 5, 8.0f, 303);
      float streak = TileableFbm(u * 0.4f, v, 4, 5.0f, 404);
      float val = std::fmin(1.0f, std::fmax(0.0f, n * 0.65f + streak * 0.55f));
      unsigned char c = (unsigned char)std::lround(val * 255.0f);
      px[(y * kFoamSize + x) * 4 + 0] = c;
      px[(y * kFoamSize + x) * 4 + 1] = c;
      px[(y * kFoamSize + x) * 4 + 2] = c;
      px[(y * kFoamSize + x) * 4 + 3] = 255;
    }
  }
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kFoamSize, kFoamSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glGenerateMipmap(GL_TEXTURE_2D);
  return tex;
}

// The font data, atlas packing, and half-texel UV calculation live in
// font_atlas.hxx so the OG fixed-function renderer and this GL 3.3 shader
// renderer consume the exact same 8x8 atlas.

// ------------------------------------------------------------ geometry + glbin
struct Mesh {
  GLuint vao = 0;
  GLuint vbo = 0;
  int indexCount = 0;
  GLenum mode = GL_TRIANGLES;
};

static Mesh gSeaMesh, gDomeMesh;
static GLuint gEmptyVao = 0;

static void BuildSeaMesh() {
  const int res = kSeaResolution;
  const float size = kSeaSize;
  const int quads = res - 1;
  std::vector<float> verts;
  std::vector<unsigned int> idx;
  verts.reserve((size_t)res * res * 4);
  idx.reserve((size_t)quads * quads * 6);
  for (int z = 0; z < res; z++) {
    for (int x = 0; x < res; x++) {
      float fx = ((float)x / (float)quads - 0.5f) * size;
      float fz = ((float)z / (float)quads - 0.5f) * size;
      verts.push_back(fx);
      verts.push_back(fz);
      verts.push_back(x / (float)quads); // uv
      verts.push_back(z / (float)quads);
    }
  }
  for (int z = 0; z < quads; z++)
    for (int x = 0; x < quads; x++) {
      unsigned int i0 = (unsigned int)(z * res + x);
      idx.push_back(i0); idx.push_back(i0 + res); idx.push_back(i0 + 1);
      idx.push_back(i0 + 1); idx.push_back(i0 + res); idx.push_back(i0 + res + 1);
    }
  gSeaMesh.indexCount = (int)idx.size();
  glGenVertexArrays(1, &gSeaMesh.vao);
  glGenBuffers(1, &gSeaMesh.vbo);
  glBindVertexArray(gSeaMesh.vao);
  glBindBuffer(GL_ARRAY_BUFFER, gSeaMesh.vbo);
  glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
  GLuint ebo;
  glGenBuffers(1, &ebo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0); // aXZ
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(1); // aUV
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
  glBindVertexArray(0);
  (void)ebo; // owned by the VAO
}

static void BuildDomeMesh() {
  const int seg = 72, rings = 36; // full sphere; enough for the sun disc
  std::vector<float> verts;
  std::vector<unsigned int> idx;
  for (int r = 0; r <= rings; r++) {
    // TRUE full sphere: dir.y = cos(phi) sweeps +1 -> -1. The old mesh used
    // sin(phi) over 0..pi, which is only the upper hemisphere — the dome
    // stopped dead at the horizon line and the background showed through as
    // a thin gap band between the sky and the sea's far edge.
    float phi = (float)r / rings * 3.14159265f;
    float cy = std::cos(phi);   // +1 (top) -> -1 (bottom)
    float cr = std::sin(phi);   // horizontal radius, 0 -> 1 -> 0
    for (int s = 0; s <= seg; s++) {
      float th = (float)s / seg * 3.14159265f * 2.0f;
      verts.push_back(cr * std::cos(th));
      verts.push_back(cy);
      verts.push_back(cr * std::sin(th));
    }
  }
  for (int r = 0; r < rings; r++)
    for (int s = 0; s < seg; s++) {
      unsigned int i0 = (unsigned int)(r * (seg + 1) + s);
      unsigned int i1 = i0 + (unsigned int)(seg + 1);
      idx.push_back(i0); idx.push_back(i0 + 1); idx.push_back(i1);
      idx.push_back(i0 + 1); idx.push_back(i1 + 1); idx.push_back(i1);
    }
  gDomeMesh.indexCount = (int)idx.size();
  glGenVertexArrays(1, &gDomeMesh.vao);
  glGenBuffers(1, &gDomeMesh.vbo);
  glBindVertexArray(gDomeMesh.vao);
  glBindBuffer(GL_ARRAY_BUFFER, gDomeMesh.vbo);
  glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
  GLuint ebo;
  glGenBuffers(1, &ebo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
  glBindVertexArray(0);
  (void)ebo;
}

// ----------------------------------------------------------- camera + state
static SDL_Window *gWindow = nullptr;
static SDL_GLContext gContext = nullptr;
static int gWindowWidth = WIDTH, gWindowHeight = HEIGHT;

static Program gSeaProg, gSkyProg, gHudProg;
static GLuint gRippleTex = 0, gNoiseTex = 0, gFoamTex = 0, gFontTex = 0;
static GLuint gEnvCube = 0, gEnvFbo = 0, gEnvDepth = 0;

static Vec3 gSunDir = {0.824f, 0.287f, 0.489f}; // sun well above the horizon (3DMark Nature framing):
                                                // warm setting sun ~17 deg up, compact glitter path

static Vec3 gCamPos = {0.0f, 7.0f, 0.0f};
static float gCamYaw = 0.0f, gCamPitch = -0.05f;
static bool gAutoCam = true;

static bool gIsHoldingMouse = false;
static int gXOld = 0, gYOld = 0;
static int gCurrentScroll = 10;

static double gStartTime = 0.0;

// Results screen: when the run ends the scene is cleared and the final score
// is drawn on the window for a few seconds (ESC skips the wait).
static bool gResultsShown = false;
static double gResultsElapsed = 0.0, gResultsFps = 0.0, gResultsScore = 0.0;
static double gResultsShownAt = 0.0;
// The combined per-scene + average screen is shown right after this one, so
// keep the single-scene display short.
static const double kResultsScreenSeconds = 4.0;
// Label shown on this scene's results screen (distinguishes it from the OG's).
static const char *gSceneName = "Dusk Ocean";
// Final score of this scene, read by main.cxx for the combined per-scene +
// average results screen.
double gFusedTideScore = 0.0;
// When the results screen is done, hand control back to main.cxx instead of
// exiting the process (the OG scene then shows the combined screen).
static bool gFusedDone = false;
// --scene-only: this scene runs on its own (no OG scene first), so its own
// results screen is the last thing the user sees and it exits the process.
static bool gStandaloneScene = false;
static int gFrame = 0, gFps = 0, gFrameAccum = 0;
static double gFpsTimer = 0.0;
static double gSmoothFps = 0.0;

static bool gQuit = false;

// Headless visual-test state (see the debug flags in the header comment).
static const char *gScreenshotPath = nullptr; // current capture target
static std::vector<float> gShotTimes;         // seconds at which to capture
static size_t gNextShot = 0;
static int gWindowWidthOverride = 0;          // --width, 0 = full resolution
static bool gDumpEnv = false;                 // --dump-env: write cubemap faces to /tmp once
static bool gDumpEnvDone = false;

static GLuint gHudVao = 0, gHudVbo = 0;
static int gHudQuadCount = 0;

// ------------------------------------------------------------- camera path
// A gentle banking fly-over: forward glide plus a slow orbit, like the
// Nature camera drifting over the ocean. The view faces the sun azimuth with
// a slow sway so the dusk reference framing — sun disc above the horizon with
// its glitter path running toward the camera — dominates the benchmark,
// occasionally drifting away for variety.
static void UpdateAutoCamera(float t) {
  float a = t * 0.10f;                                   // orbit: 2x faster than before
  float radius = 42.0f + std::sin(t * 0.042f) * 10.0f;   // breathe in/out at 2x

  Vec3 eye;
  eye.x = std::cos(a) * radius;
  eye.z = std::sin(a) * radius * 0.7f;
  eye.y = 7.5f + std::sin(t * 0.086f) * 2.2f;            // bob: 2x faster
  gCamPos = eye;

  // face the sun, swaying so the framing breathes during the run.
  // Tamed sway (±0.18 rad) keeps the glitter path in the middle of the frame
  // with dark off-path sea on both sides — the reference composition.
  // All sway frequencies doubled so the whole motion reads quicker.
  float yawSun = std::atan2(gSunDir.x, gSunDir.z);
  float yaw = yawSun + 0.14f * std::sin(t * 0.026f) + 0.04f * std::sin(t * 0.082f);
  float pitch = 0.030f + 0.022f * std::sin(t * 0.034f); // horizon high in frame:
                                                        // sky ~25%, sea ~75% like
                                                        // the reference shot
  Vec3 fwd = {std::sin(yaw) * std::cos(pitch), std::sin(pitch),
              std::cos(yaw) * std::cos(pitch)};
  gCamYaw = std::atan2(fwd.x, fwd.z);
  gCamPitch = std::asin(fwd.y);
}

static Vec3 OrbitCamPos() {
  float cx = gCamPitch * 3.14159265f / 180.0f;
  float cy = gCamYaw;
  float d = (float)gCurrentScroll * 1.6f + 8.0f;
  Vec3 pos;
  pos.x = gCamPos.x + std::sin(cy) * std::cos(cx) * d;
  pos.y = gCamPos.y + std::sin(cx) * d + 3.0f;
  pos.z = gCamPos.z + std::cos(cy) * std::cos(cx) * d;
  if (pos.y < 2.5f) pos.y = 2.5f; // never dive under the bigger swell
  return pos;
}

// --------------------------------------------------------------- env cubemap
static void CreateEnvResources() {
  glGenTextures(1, &gEnvCube);
  glBindTexture(GL_TEXTURE_CUBE_MAP, gEnvCube);
  for (int i = 0; i < 6; i++) {
    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA16F, kEnvMapSize, kEnvMapSize, 0,
                 GL_RGBA, GL_FLOAT, nullptr);
  }
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

  glGenFramebuffers(1, &gEnvFbo);
  glGenRenderbuffers(1, &gEnvDepth);
  glBindRenderbuffer(GL_RENDERBUFFER, gEnvDepth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, kEnvMapSize, kEnvMapSize);
  glBindFramebuffer(GL_FRAMEBUFFER, gEnvFbo);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, gEnvDepth);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// --------------------------------------------------------------- env cubemap
// Shared sky-dome uniforms so the cubemap capture and the on-screen dome draw
// the exact same sky (same palette, same clouds, same time).
static void BindSkyUniforms(const Mat4 &vp) {
  glUseProgram(gSkyProg.handle);
  glUniformMatrix4fv(gSkyProg.loc("uViewProj"), 1, GL_FALSE, vp.data());
  glUniform3f(gSkyProg.loc("uSunDir"), gSunDir.x, gSunDir.y, gSunDir.z);
  glUniform1f(gSkyProg.loc("uTime"), (float)NowSeconds());
  // Dusk palette (HDR, linear): deep blue-black zenith shading into a warm
  // horizon band around the setting sun (3DMark Nature look).
  glUniform3f(gSkyProg.loc("uZenithColor"), 0.012f, 0.016f, 0.048f);  // deep blue-black overhead
  glUniform3f(gSkyProg.loc("uMidColor"), 0.028f, 0.022f, 0.048f);     // dark slate-mauve mid sky
  glUniform3f(gSkyProg.loc("uHorizonColor"), 0.115f, 0.055f, 0.062f); // warm maroon horizon band
  glUniform3f(gSkyProg.loc("uSunColor"), 1.55f, 0.72f, 0.30f);        // deeper orange sun

  // Explicit cloud masses: a FEW fat cumulus with real clear-sky gaps between
  // them (the 3DMark Nature look). Hand-placed, time-static — uploaded once.
  // Coverage is exact by construction; nothing depends on noise-texture
  // statistics, which is what previously mottled the whole sky on real GPUs.
  static bool cloudsUploaded = false;
  if (!cloudsUploaded) {
    // 5 clouds spread around the horizon ring; three sit in the camera's
    // sun-facing view, two populate the rest of the sky for the orbit.
    glUniform1i(gSkyProg.loc("uCloudCount"), MAX_CLOUDS);
    glUniform1fv(gSkyProg.loc("uCloudAzim"), MAX_CLOUDS, gCloudAzimDrift);
    glUniform1fv(gSkyProg.loc("uCloudElev"), MAX_CLOUDS, kCloudElev);
    glUniform1fv(gSkyProg.loc("uCloudRadius"), MAX_CLOUDS, kCloudRad);
    glUniform1fv(gSkyProg.loc("uCloudStretch"), MAX_CLOUDS, kCloudStretch);
    cloudsUploaded = true;
  }
}

static void DrawSkyToEnvMap(const Mat4 &proj) {
  static const GLenum faces[6] = {GL_TEXTURE_CUBE_MAP_POSITIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
                                  GL_TEXTURE_CUBE_MAP_POSITIVE_Y, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
                                  GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z};
  static const Vec3 fwd[6] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
  static const Vec3 up[6] = {{0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0}};

  glViewport(0, 0, kEnvMapSize, kEnvMapSize);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE); // the dome is seen from the inside
  glBindFramebuffer(GL_FRAMEBUFFER, gEnvFbo);
  glUseProgram(gSkyProg.handle);
  glBindVertexArray(gDomeMesh.vao);

  Mat4 view;
  for (int i = 0; i < 6; i++) {
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, faces[i], gEnvCube, 0);
    Mat4Identity(view);
    // camera at the dome centre (origin), looking along each face
    Mat4LookAt(view, {0, 0, 0}, fwd[i], up[i]);
    Mat4 vp;
    Mat4Multiply(vp, proj, view);
    BindSkyUniforms(vp);
    // env capture: dome centred at the origin, small radius, HDR output
    glUniform3f(gSkyProg.loc("uCenter"), 0.0f, 0.0f, 0.0f);
    glUniform1f(gSkyProg.loc("uRadius"), 10.0f);
    glUniform1f(gSkyProg.loc("uTonemap"), 0.0f);
    glDrawElements(GL_TRIANGLES, gDomeMesh.indexCount, GL_UNSIGNED_INT, nullptr);
  }
  // mip chain for the cube: LINEAR_MIPMAP_LINEAR on an unmipped texture is
  // incomplete (black) on strict drivers, and mips also smooth the magnified
  // sky when the sea reflects it
  glBindTexture(GL_TEXTURE_CUBE_MAP, gEnvCube);
  glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glEnable(GL_CULL_FACE);
}

static void DumpEnvFacesOnce() {
  if (!gDumpEnv || gDumpEnvDone) return;
  gDumpEnvDone = true;
  static const GLenum faces[6] = {GL_TEXTURE_CUBE_MAP_POSITIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
                                  GL_TEXTURE_CUBE_MAP_POSITIVE_Y, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
                                  GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z};
  static const char names[6] = {'x','X','y','Y','z','Z'};
  glBindFramebuffer(GL_FRAMEBUFFER, gEnvFbo);
  glViewport(0, 0, kEnvMapSize, kEnvMapSize);
  std::vector<unsigned char> rgb((size_t)kEnvMapSize * kEnvMapSize * 3);
  for (int i = 0; i < 6; i++) {
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, faces[i], gEnvCube, 0);
    glReadPixels(0, 0, kEnvMapSize, kEnvMapSize, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    char path[64];
    std::snprintf(path, sizeof(path), "/tmp/env_%c.ppm", names[i]);
    FILE *f = std::fopen(path, "wb");
    if (f) {
      std::fprintf(f, "P6\n%d %d\n255\n", kEnvMapSize, kEnvMapSize);
      for (int row = kEnvMapSize - 1; row >= 0; row--) // GL rows are bottom-up
        std::fwrite(&rgb[(size_t)row * kEnvMapSize * 3], 1, (size_t)kEnvMapSize * 3, f);
      std::fclose(f);
    }
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  std::printf("env faces dumped\n");
  std::fflush(stdout);
}

// ------------------------------------------------------------------- HUD
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
  // Append one quad per glyph to a streaming buffer (position xy + uv).
  // Glyph quads are CCW in pixel space; the HUD vertex shader maps that to CW
  // in clip space (y-down pixel -> y-up NDC), so face culling must be off
  // while drawing text or every glyph is discarded.
  static std::vector<float> buf;
  buf.clear();
  float pen = x; // atlas cells scaled up (default 2 -> 16px on screen)
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
    // two triangles, CCW in screen space (y down)
    auto push = [&](float px, float py, float u, float v) {
      buf.push_back(px); buf.push_back(py); buf.push_back(u); buf.push_back(v);
    };
    push(x0, y0, u0, v0); push(x1, y0, u1, v0); push(x1, y1, u1, v1);
    push(x0, y0, u0, v0); push(x1, y1, u1, v1); push(x0, y1, u0, v1);
    pen += (float)kFontAtlasCell * scale;
  }
  gHudQuadCount = (int)buf.size() / 4;
  if (!gHudQuadCount) return;
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
  glDisable(GL_CULL_FACE); // glyph quads are CW in clip space
  glBindVertexArray(gHudVao);
  glDrawArrays(GL_TRIANGLES, 0, gHudQuadCount);
  glBindVertexArray(0);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
}

// ------------------------------------------------------------------- setup
static void Setup() {
  BuildSeaMesh();
  BuildDomeMesh();
  BuildFontAtlas();

  glGenVertexArrays(1, &gEmptyVao); // for the fullscreen triangle pass
  glGenVertexArrays(1, &gHudVao);
  glGenBuffers(1, &gHudVbo);
  glBindVertexArray(gHudVao);
  glBindBuffer(GL_ARRAY_BUFFER, gHudVbo);
  glEnableVertexAttribArray(0); // pixel pos
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(1); // uv
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
  glBindVertexArray(0);

  gRippleTex = CreateRippleTexture();
  gNoiseTex = CreateSkyNoiseTexture();
  gFoamTex = CreateFoamTexture();
  CreateEnvResources();

  gSeaProg = LinkProgram(resolveAssetPath("shaders/ps14/sea_vert.glsl").c_str(), resolveAssetPath("shaders/ps14/sea_frag.glsl").c_str());
  gSkyProg = LinkProgram(resolveAssetPath("shaders/ps14/sky_vert.glsl").c_str(), resolveAssetPath("shaders/ps14/sky_frag.glsl").c_str());
  gHudProg = LinkProgram(resolveAssetPath("shaders/ps14/hud_vert.glsl").c_str(), resolveAssetPath("shaders/ps14/hud_frag.glsl").c_str());

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glClearColor(0.055f, 0.035f, 0.075f, 1.0f); // dusk background (only visible if geometry ever gaps)
}

// -------------------------------------------------------------- render passes
static Mat4 gProj;

static void DrawSea(const Mat4 &view, double timeSec, const Vec3 &eye) {
  Mat4 vp;
  Mat4Multiply(vp, gProj, view);
  glUseProgram(gSeaProg.handle);
  glBindVertexArray(gSeaMesh.vao);
  glDepthMask(GL_TRUE);
  glUniformMatrix4fv(gSeaProg.loc("uViewProj"), 1, GL_FALSE, vp.data());
  glUniform1f(gSeaProg.loc("uTime"), (float)timeSec);
  glUniform3f(gSeaProg.loc("uEyePos"), eye.x, eye.y, eye.z);
  glUniform3f(gSeaProg.loc("uSunDir"), gSunDir.x, gSunDir.y, gSunDir.z);
  glUniform3f(gSeaProg.loc("uHorizonColor"), 0.10f, 0.052f, 0.062f); // dark maroon haze, melts into the sky band
  glUniform3f(gSeaProg.loc("uWaterColor"), 0.034f, 0.020f, 0.052f); // near-black indigo body like the dusk reference
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, gRippleTex);
  glUniform1i(gSeaProg.loc("uRippleTex"), 0);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_CUBE_MAP, gEnvCube);
  glUniform1i(gSeaProg.loc("uSkyEnvTex"), 1);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, gFoamTex);
  glUniform1i(gSeaProg.loc("uFoamTex"), 2);

  // cloud layout, shared with the sky shader: the sea projects fragments to
  // the sky along sun rays and evaluates the SAME analytic puffs, so the
  // cloud shadows on the water land exactly under the clouds that cast them.
  glUniform1i(gSeaProg.loc("uCloudCount"), MAX_CLOUDS);
  glUniform1fv(gSeaProg.loc("uCloudAzim"), MAX_CLOUDS, gCloudAzimDrift);
  glUniform1fv(gSeaProg.loc("uCloudElev"), MAX_CLOUDS, kCloudElev);
  glUniform1fv(gSeaProg.loc("uCloudRadius"), MAX_CLOUDS, kCloudRad);
  glUniform1fv(gSeaProg.loc("uCloudStretch"), MAX_CLOUDS, kCloudStretch);
  glDrawElements(GL_TRIANGLES, gSeaMesh.indexCount, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
}

static void DrawSkyScreen(const Mat4 &view, const Vec3 &eye) {
  // The visible sky is the procedural dome drawn DIRECTLY to the screen at
  // full framebuffer resolution. (It used to be reconstructed from the env
  // cubemap, which magnified each texel into the blocky squares the user saw
  // on real hardware.) The cubemap is now only used for sea reflections and
  // the sea haze target, where its low resolution is an advantage (blur).
  Mat4 vp;
  Mat4Multiply(vp, gProj, view);

  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE); // sky is background; it must not occlude the sea
  glDisable(GL_CULL_FACE); // camera is inside the dome sphere
  glBindVertexArray(gDomeMesh.vao);
  BindSkyUniforms(vp);
  // dome centred at the eye, radius kept inside the far plane
  glUniform3f(gSkyProg.loc("uCenter"), eye.x, eye.y, eye.z);
  glUniform1f(gSkyProg.loc("uRadius"), 5000.0f);
  glUniform1f(gSkyProg.loc("uTonemap"), 1.0f); // LDR out on screen
  glDrawElements(GL_TRIANGLES, gDomeMesh.indexCount, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
  glEnable(GL_CULL_FACE);
  glDepthMask(GL_TRUE);
  glEnable(GL_DEPTH_TEST);
}

static void RenderHUD() {
  // clean single readout: just the live FPS. The score belongs to the final
  // results line, not on screen during the run.
  char line1[128];
  std::snprintf(line1, sizeof(line1), "FPS: %d", gFps);
  RenderText(16.0f, 16.0f, line1);
}

// Results screen: clear the window and show the final score big and centred.
static void RenderResults() {
  glViewport(0, 0, gWindowWidth, gWindowHeight);
  glClearColor(0.012f, 0.012f, 0.022f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  char big[96], timeLine[128], hint[96];
  std::snprintf(big, sizeof(big), "SCORE : %.0f", gResultsScore);
  std::snprintf(timeLine, sizeof(timeLine), "Time : %.1fs   Average FPS : %.1f",
                gResultsElapsed, gResultsFps);
  std::snprintf(hint, sizeof(hint), "%s score", gSceneName);

  float cx = 0.5f * (float)gWindowWidth;
  float cy = 0.5f * (float)gWindowHeight;
  RenderText(cx - (float)std::strlen(big) * 8.0f * 2.0f, cy - 42.0f, big, 4.0f);
  RenderText(cx - (float)std::strlen(timeLine) * 8.0f, cy + 30.0f, timeLine);
  RenderText(cx - (float)std::strlen(hint) * 8.0f, cy + 64.0f, hint);
}

// Renders the scene and calculates FPS (same pattern as the original bench).
static void WriteScreenshotPPM(const char *path);

static void RenderScene() {
  double now = NowSeconds();

  // ---- results screen: scene cleared, score on the window, then exit ----
  if (gResultsShown) {
    RenderResults();
    SDL_GL_SwapWindow(gWindow);
    if (now - gResultsShownAt >= kResultsScreenSeconds) {
      if (gStandaloneScene) {
        SDL_Quit();
        std::exit(0);
      }
      gFusedDone = true; // back to the OG's combined results screen
    }
    return;
  }

  float t = (float)(now - gStartTime); // camera time is benchmark-relative so --shot-times are deterministic
  UpdateCloudAzim(t);

  // ---- camera ----
  if (gAutoCam) {
    UpdateAutoCamera(t);
  }
  Vec3 eye = gAutoCam ? gCamPos : OrbitCamPos();

  Mat4 view;
  {
    float cy = std::cos(gCamYaw), sy = std::sin(gCamYaw);
    float cp = std::cos(gCamPitch), sp = std::sin(gCamPitch);
    Vec3 fwd = {sy * cp, sp, cy * cp};
    Vec3 target = Vec3Add(eye, Vec3Scale(fwd, 50.0f));
    Mat4LookAt(view, eye, target, {0, 1, 0});
  }

  float aspect = (float)gWindowWidth / (float)gWindowHeight;
  Mat4Perspective(gProj, 45.0f, aspect, 0.5f, 6000.0f); // far plane past the 4096 m sea patch diagonal

  // ---- pass 1: sky -> env cubemap (90 deg per face so every cube face is
  // fully covered by the dome) ----
  Mat4 envProj;
  Mat4Perspective(envProj, 90.0f, 1.0f, 0.1f, 20.0f);
  DrawSkyToEnvMap(envProj);
  DumpEnvFacesOnce();

  // ---- pass 2: main framebuffer ----
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, gWindowWidth, gWindowHeight);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  DrawSkyScreen(view, eye);
  DrawSea(view, now, eye);
  RenderHUD();

    // Visual-test captures: read the framebuffer back before the swap so the
    // pixels we analyse are exactly what this frame rendered.
    if (gScreenshotPath && gNextShot < gShotTimes.size() &&
        now - gStartTime >= (double)gShotTimes[gNextShot]) {
      WriteScreenshotPPM(gScreenshotPath);
      gNextShot++;
      if (gNextShot >= gShotTimes.size()) {
        SDL_Quit();
        std::exit(0);
      }
    }

    SDL_GL_SwapWindow(gWindow);

  // ---- fps + benchmark timer ----
  gFrame++;
  gFrameAccum++;
  double dt = now - gFpsTimer;
  if (dt >= 1.0) {
    gFps = (int)std::lround(gFrameAccum / dt);
    double inst = gFrameAccum / dt;
    gSmoothFps = gSmoothFps > 0.0 ? gSmoothFps * 0.8 + inst * 0.2 : inst;
    gFrameAccum = 0;
    gFpsTimer = now;
    char title[256];
    std::snprintf(title, sizeof(title), "%s - FPS : %d", NAME, gFps);
    SDL_SetWindowTitle(gWindow, title);
  }
  if ((now - gStartTime) * 1000.0 >= (double)BENCH_MILLISECONDS) {
    double elapsed = now - gStartTime;
    // Score from ALL frames of the run (not the last smoothing window),
    // matching the original bench: score = fps^2 * 2.
    double fps = (double)gFrame / elapsed;
    double score = fps * fps * 2.0;
    std::printf("Benchmark Results - Time : %.1fs, Average FPS : %.1f, Score : %.0f\n",
                elapsed, fps, score);
    std::fflush(stdout);
    gFusedTideScore = score; // for the combined results screen in main.cxx
    // Hand over to the results screen: the scene is cleared and the score is
    // drawn on the window for kResultsScreenSeconds (ESC exits immediately).
    gResultsElapsed = elapsed;
    gResultsFps = fps;
    gResultsScore = score;
    gResultsShownAt = now;
    gResultsShown = true;
  }
}

// ------------------------------------------------------------------ input
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
    gCamPitch = std::fmin(gCamPitch + 0.03f, 0.35f);
  } else if (event.key.keysym.sym == SDLK_DOWN) {
    gCamPitch = std::fmax(gCamPitch - 0.03f, -0.35f);
  }
}

static void HandleMouseEvent(const SDL_Event &event) {
  if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
    gXOld = event.button.x;
    gYOld = event.button.y;
    gIsHoldingMouse = true;
    gAutoCam = false; // grabbing the camera takes control, like the original
  } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
    gIsHoldingMouse = false;
  } else if (event.type == SDL_MOUSEWHEEL) {
    if (event.wheel.y > 0) gCurrentScroll++;
    else if (event.wheel.y < 0) gCurrentScroll--;
    if (gCurrentScroll < 1) gCurrentScroll = 1;
  }
}

static void HandleMouseMotion(const SDL_Event &event) {
  if (gIsHoldingMouse) {
    gCamYaw -= (event.motion.x - gXOld) * 0.005f;
    gXOld = event.motion.x;
    gCamPitch = std::fmin(std::fmax(gCamPitch + (event.motion.y - gYOld) * 0.004f, -0.45f), 0.35f);
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
    else if (*p != '\0') return false;
  }
  return !gShotTimes.empty();
}

// ------------------------------------------------ option plumbing for main()
// There is one executable now, so main.cxx owns the command line and forwards
// the ocean scene's own flags here. Returns EXIT_FAILURE on a bad option.
int OceanSceneParseArgs(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--shot-times") && i + 1 < argc) {
      if (!ParseShotTimes(argv[++i])) {
        std::fprintf(stderr, "Bad --shot-times list: %s\n", argv[i]);
        return EXIT_FAILURE;
      }
    } else if (!std::strcmp(argv[i], "--width") && i + 1 < argc) {
      gWindowWidthOverride = std::atoi(argv[++i]);
    } else if (!std::strcmp(argv[i], "--dump-env")) {
      gDumpEnv = true;
    }
  }
  return EXIT_SUCCESS;
}

// Shares main.cxx's --screenshot target with this scene.
void OceanSceneSetScreenshot(const char *path) { gScreenshotPath = path; }

// --scene-only: this scene runs (and ends) on its own.
void OceanSceneSetStandalone(bool standalone) { gStandaloneScene = standalone; }

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
  for (int y = h - 1; y >= 0; y--) // GL rows are bottom-up; PPM is top-down
    std::fwrite(&rgb[(size_t)y * w * 3], 1, (size_t)w * 3, f);
  std::fclose(f);
  std::printf("Screenshot written: %s\n", path);
  std::fflush(stdout);
}

// ------------------------------------------------------ scene entry point
// Runs the ocean scene on the SDL session handed over by main.cxx (either
// straight after the OG gun scene, or alone with --scene-only). Sets
// *gaveUp = true when the GL 3.3 core context could not be created.
int RunOceanScene(bool *gaveUpOut) {
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
    // Test mode: fixed-width window, height follows the 16:9 benchmark aspect.
    winW = gWindowWidthOverride;
    winH = (gWindowWidthOverride * HEIGHT + WIDTH / 2) / WIDTH;
  }

  gWindow = SDL_CreateWindow(NAME, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, winW, winH,
                             SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
  if (gWindowWidthOverride > 0) {
    gWindowWidth = winW;   // a headless WM may never send a RESIZED event
    gWindowHeight = winH;
    glViewport(0, 0, winW, winH);
  }
  if (!gWindow) {
    std::fprintf(stderr, "Window could not be created! SDL_Error: %s\n", SDL_GetError());
    return EXIT_FAILURE;
  }
  gContext = SDL_GL_CreateContext(gWindow);
  if (!gContext) {
    // No GL 3.3 core context on this device: skip the scene, keep the OG result.
    std::printf("Dusk ocean scene: OpenGL 3.3 core context unavailable — skipping this scene\n");
    std::fflush(stdout);
    SDL_Quit();
    if (gaveUpOut) *gaveUpOut = true;
    return 1;
  }
  SDL_GL_SetSwapInterval(0); // unclamped, like a benchmark should be

  if (glewInit() != GLEW_OK) {
    std::fprintf(stderr, "glewInit failed\n");
    return EXIT_FAILURE;
  }

  std::printf("Renderer: %s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

  Setup();

  gStartTime = NowSeconds();
  gFpsTimer = gStartTime;

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
  if (gQuit) return 2; // user quit during this scene: exit the whole bench
  return 0;
}
