// ElectroBench — scene 3 of the single ElectroBench binary: the "pool room"
// — an infinite checkerboard sky reflected on open water, lit only by a
// hidden light source, with a teapot that falls from the sky, splashes down,
// and bobs on the surface with realistic-ish physics (gravity, buoyancy,
// drag, damping) on OpenGL 3.3 core.
//
// Like src/tidebench.cxx, this translation unit is NOT a program of its own.
// It exports RunPoolScene(), which main.cxx calls as the third scene of the
// one and only ElectroBench executable.
//
// What is rendered:
//   * Checkerboard sky dome (shaders/pool/sky_*.glsl): planar-projected
//     checker with analytic antialiasing and a soft glow toward the hidden
//     light. No lamp, no sun disc — the light is only ever visible through
//     shading (the specular path on the water and the teapot).
//   * Open water (shaders/pool/water_frag.glsl): analytic mirror reflection
//     of the same checker function, hidden-light specular, and up to six
//     expanding splash rings driven from the CPU physics each frame.
//   * A teapot (assets/teapot.obj, one material, placeholder texture the
//     user can swap): normalised, dropped from ~8 m, splash on impact, then
//     buoyancy + drag + bob until it settles, rocking to rest. Tumbles in
//     flight, rights itself in the water.
//   * Splash droplets: billboard sprites with per-droplet gravity, spawned
//     on impact with randomized velocities, plus secondary drips while the
//     rings decay.
//
// Controls: drag orbits the camera, wheel zooms, F toggles the auto camera,
// R re-drops the teapot, ESC quits.
//
// Headless flags shared with the other scenes: --screenshot, --shot-times,
// --width; scene-specific: --pool-only (run just this scene).

#include <algorithm>
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

// lodepng.c is compiled into main.o (via lib/util.hxx) as C++ (its symbols
// are mangled), so declaring the prototype here without extern "C" links
// against those. Declaring it instead of including lodepng.h avoids pulling
// the header in twice, which would duplicate the C++ wrapper's inline
// definitions. (LCT_RGBA = 6, 8-bit depth.)
enum LodePNGColorType { LCT_GREY = 0, LCT_RGB = 2, LCT_PALETTE = 3,
                        LCT_GREY_ALPHA = 4, LCT_RGBA = 6 };
unsigned lodepng_decode_file(unsigned char **out, unsigned *w, unsigned *h,
                             const char *filename, LodePNGColorType colorType,
                             unsigned bitdepth);

// ------------------------------------------------------------------ constants
#define NAME "ElectroBench - Pool Room"
#define WIDTH 1366
#define HEIGHT 768
#define BENCH_MILLISECONDS 45000 // 45 s, same as the other scenes

#define MAX_RINGS 6              // must match water_frag.glsl

static const int kWaterResolution = 220;  // grid verts per side (display grid;
                                          // the lighting is analytic per pixel)
static const float kWaterSize = 300.0f;   // water patch half-size reaches the
                                          // horizon haze
static const int kDomeSeg = 48, kDomeRings = 28;
static const float kDomeRadius = 800.0f;  // inside the far plane

// palette — the COLOURED pool-room checker. Must match sky_frag.glsl's
// tileA/tileB (the water shader receives these as uniforms).
static const float kTileA[3] = {0.020f, 0.580f, 0.780f}; // turquoise / cyan tile
static const float kTileB[3] = {1.600f, 0.110f, 0.025f}; // hot coral / orange tile
static const float kLightTint[3] = {0.86f, 0.95f, 1.05f};// cool pool-room glow

// hidden light: direction TOWARD the light, high and behind the default
// camera so the water specular path lands between camera and teapot
static const float kLightDir[3] = {-0.32f, 0.80f, -0.50f};

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
  Vec3 f = Vec3Normalize(Vec3Sub(center, eye));
  Vec3 s = Vec3Normalize(Vec3Cross(f, up));
  Vec3 u = Vec3Cross(s, f);
  Mat4Identity(m);
  m[0] = s.x;   m[4] = s.y;   m[8]  = s.z;
  m[1] = u.x;   m[5] = u.y;   m[9]  = u.z;
  m[2] = -f.x;  m[6] = -f.y;  m[10] = -f.z;
  m[12] = -Vec3Dot(s, eye);
  m[13] = -Vec3Dot(u, eye);
  m[14] = Vec3Dot(f, eye);
}

// TRS-ish model matrix for the teapot: translate * rotY * uniform scale.
static void Mat4Model(Mat4 &m, const Vec3 &pos, float yaw, float scale) {
  float c = std::cos(yaw), s = std::sin(yaw);
  Mat4Identity(m);
  m[0] = c * scale;  m[4] = 0.0f;    m[8]  = -s * scale;  m[12] = pos.x;
  m[1] = 0.0f;       m[5] = scale;   m[9]  = 0.0f;        m[13] = pos.y;
  m[2] = s * scale;  m[6] = 0.0f;    m[10] = c * scale;   m[14] = pos.z;
  m[3] = 0.0f;       m[7] = 0.0f;    m[11] = 0.0f;        m[15] = 1.0f;
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

// ------------------------------------------------------------------ OBJ load
// Minimal v/vt/vn loader for the single-material teapot: fan-triangulates
// polygons, computes smooth normals when the file has none, re-centres the
// mesh on its base (y=min) and normalises the bounding radius.
struct TeapotMesh {
  GLuint vao = 0, vbo = 0;
  int indexCount = 0;
};

static TeapotMesh LoadObjMesh(const char *path, float targetRadius) {
  FILE *f = std::fopen(path, "rb");
  if (!f) {
    std::fprintf(stderr, "Cannot open OBJ %s\n", path);
    std::exit(EXIT_FAILURE);
  }
  std::vector<float> vp, vt, vn;
  char line[512];
  while (std::fgets(line, sizeof(line), f)) {
    if (line[0] == 'v' && line[1] == ' ') {
      float x, y, z;
      if (std::sscanf(line, "v %f %f %f", &x, &y, &z) == 3) {
        vp.push_back(x); vp.push_back(y); vp.push_back(z);
      }
    } else if (line[0] == 'v' && line[1] == 't') {
      float u, v;
      if (std::sscanf(line, "vt %f %f", &u, &v) == 2) vt.push_back(u), vt.push_back(v);
    } else if (line[0] == 'v' && line[1] == 'n') {
      float x, y, z;
      if (std::sscanf(line, "vn %f %f %f", &x, &y, &z) == 3) vn.push_back(x), vn.push_back(y), vn.push_back(z);
    }
  }
  std::rewind(f);

  // face parsing needs the bbox, so scan it first
  float minv[3] = {1e9f, 1e9f, 1e9f}, maxv[3] = {-1e9f, -1e9f, -1e9f};
  for (size_t i = 0; i + 2 < vp.size(); i += 3) {
    for (int k = 0; k < 3; k++) {
      if (vp[i + k] < minv[k]) minv[k] = vp[i + k];
      if (vp[i + k] > maxv[k]) maxv[k] = vp[i + k];
    }
  }
  float ctrX = (minv[0] + maxv[0]) * 0.5f;
  float ctrZ = (minv[2] + maxv[2]) * 0.5f;
  float baseY = minv[1];
  float ext[3] = {maxv[0] - minv[0], maxv[1] - minv[1], maxv[2] - minv[2]};
  float maxDim = ext[0];
  if (ext[1] > maxDim) maxDim = ext[1];
  if (ext[2] > maxDim) maxDim = ext[2];
  float s = targetRadius / (maxDim * 0.5f);

  auto emit = [&](std::vector<float> &data, std::vector<unsigned> &idx,
                  int vi, int ti, int ni) {
    idx.push_back((unsigned)(data.size() / 8));
    data.push_back((vp[(vi - 1) * 3 + 0] - ctrX) * s);
    data.push_back((vp[(vi - 1) * 3 + 1] - baseY) * s);
    data.push_back((vp[(vi - 1) * 3 + 2] - ctrZ) * s);
    if (ni > 0 && (size_t)(ni - 1) * 3 + 2 < vn.size()) {
      data.push_back(vn[(ni - 1) * 3 + 0]);
      data.push_back(vn[(ni - 1) * 3 + 1]);
      data.push_back(vn[(ni - 1) * 3 + 2]);
    } else {
      data.push_back(0); data.push_back(1); data.push_back(0);
    }
    if (ti > 0 && (size_t)(ti - 1) * 2 + 1 < vt.size()) {
      data.push_back(vt[(ti - 1) * 2 + 0]);
      data.push_back(vt[(ti - 1) * 2 + 1]);
    } else {
      data.push_back(0); data.push_back(0);
    }
  };

  std::vector<float> data; // interleaved pos3/normal3/uv2
  std::vector<unsigned> idx;
  while (std::fgets(line, sizeof(line), f)) {
    if (line[0] != 'f' || line[1] != ' ') continue;
    // parse every corner as v(/vt)(/vn), then fan-triangulate
    int cvi[16], cti[16], cni[16];
    int nc = 0;
    const char *p = line + 2;
    while (*p && nc < 16) {
      int vi = 0, ti = 0, ni = 0;
      int n = std::sscanf(p, "%d/%d/%d", &vi, &ti, &ni);
      if (n <= 0) break;
      if (n == 1) { ti = 0; ni = 0; }
      cvi[nc] = vi; cti[nc] = ti; cni[nc] = ni;
      nc++;
      while (*p && *p != ' ') p++;
      while (*p == ' ') p++;
    }
    if (nc < 3) continue;
    if (nc == 3 && vn.empty()) {
      // file without normals: accumulate face normals as we go is overkill
      // for the placeholder; fall back to up normals (matches the sphere caps)
    }
    for (int k = 1; k + 1 < nc; k++) {
      emit(data, idx, cvi[0], cti[0], cni[0]);
      emit(data, idx, cvi[k], cti[k], cni[k]);
      emit(data, idx, cvi[k + 1], cti[k + 1], cni[k + 1]);
    }
  }
  std::fclose(f);

  TeapotMesh m;
  glGenVertexArrays(1, &m.vao);
  glGenBuffers(1, &m.vbo);
  glBindVertexArray(m.vao);
  glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
  glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STATIC_DRAW);
  GLuint ebo;
  glGenBuffers(1, &ebo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned), idx.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(6 * sizeof(float)));
  glBindVertexArray(0);
  (void)ebo;
  m.indexCount = (int)idx.size();
  return m;
}

// ------------------------------------------------------------------- textures
static GLuint LoadTextureRGBA(const char *path) {
  std::string resolved = resolveAssetPath(path);
  unsigned char *img = nullptr;
  unsigned w = 0, h = 0;
  unsigned err = lodepng_decode_file(&img, &w, &h, resolved.c_str(), LCT_RGBA, 8);
  if (err) {
    std::fprintf(stderr, "Cannot load texture %s (lodepng error %u)\n", resolved.c_str(), err);
    std::exit(EXIT_FAILURE);
  }
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  std::free(img);
  return tex;
}

// ------------------------------------------------------------------- meshes
struct Mesh {
  GLuint vao = 0;
  GLuint vbo = 0;
  int indexCount = 0;
};

static Mesh gWaterMesh, gDomeMesh;
static TeapotMesh gTeapotMesh;
static GLuint gDropletVao = 0, gDropletVbo = 0;
static int gDropletVertexFloats = 0; // floats currently streamed
static GLuint gCrownVao = 0, gCrownVbo = 0;
static int gCrownVertexCount = 0;
static GLuint gJetVao = 0, gJetVbo = 0;
static int gJetVertexFloats = 0;

static void BuildWaterMesh() {
  const int res = kWaterResolution;
  const float size = kWaterSize;
  const int quads = res - 1;
  std::vector<float> verts;
  std::vector<unsigned int> idx;
  verts.reserve((size_t)res * res * 8);
  idx.reserve((size_t)quads * quads * 6);
  for (int z = 0; z < res; z++) {
    for (int x = 0; x < res; x++) {
      float fx = ((float)x / (float)quads - 0.5f) * 2.0f * size;
      float fz = ((float)z / (float)quads - 0.5f) * 2.0f * size;
      verts.push_back(fx);
      verts.push_back(0.0f);
      verts.push_back(fz);
      verts.push_back(0.0f); verts.push_back(1.0f); verts.push_back(0.0f); // normal
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
  gWaterMesh.indexCount = (int)idx.size();
  glGenVertexArrays(1, &gWaterMesh.vao);
  glGenBuffers(1, &gWaterMesh.vbo);
  glBindVertexArray(gWaterMesh.vao);
  glBindBuffer(GL_ARRAY_BUFFER, gWaterMesh.vbo);
  glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
  GLuint ebo;
  glGenBuffers(1, &ebo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(6 * sizeof(float)));
  glBindVertexArray(0);
  (void)ebo;
}

static void BuildDomeMesh() {
  std::vector<float> verts;
  std::vector<unsigned int> idx;
  for (int r = 0; r <= kDomeRings; r++) {
    float phi = (float)r / kDomeRings * 3.14159265f;
    float cy = std::cos(phi);
    float cr = std::sin(phi);
    for (int s = 0; s <= kDomeSeg; s++) {
      float th = (float)s / kDomeSeg * 3.14159265f * 2.0f;
      float nx = cr * std::cos(th), ny = cy, nz = cr * std::sin(th);
      verts.push_back(nx); verts.push_back(ny); verts.push_back(nz);
      verts.push_back(nx); verts.push_back(ny); verts.push_back(nz);
      verts.push_back(0.0f); verts.push_back(0.0f);
    }
  }
  for (int r = 0; r < kDomeRings; r++)
    for (int s = 0; s < kDomeSeg; s++) {
      unsigned int i0 = (unsigned int)(r * (kDomeSeg + 1) + s);
      unsigned int i1 = i0 + (unsigned int)(kDomeSeg + 1);
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
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(6 * sizeof(float)));
  glBindVertexArray(0);
  (void)ebo;
}

// ------------------------------------------------------------ physics state
// The teapot: dropped from the sky, splashes, sinks a little, pops back up
// via buoyancy and bobs to rest. A little tumble in the air, righting torque
// in the water, and everything damps to stillness. Realistic *feeling*
// semi-implicit Euler at 60+ steps/s — deterministic, no fixed step needed.
struct TeapotPhysics {
  Vec3 pos{0.0f, 8.0f, 0.0f};
  Vec3 vel{0.0f, 0.0f, 0.0f};
  float yaw = 0.0f;
  float yawVel = 1.1f;      // slow tumble while airborne
  float radius = 0.55f;     // bounding radius (world units)
  float restDepth = 0.16f;  // submerged depth at equilibrium
  bool inWater = false;
  bool splashed = false;
  bool settled = false;
  double splashTime = -1.0;
};

// rings are POD: x, z, radius, strength
struct Ring { float x, z, radius, strength; };
static Ring gRings[MAX_RINGS];
static int gRingCount = 0;

// The Worthington crown: a jagged water sheet that erupts around the impact
// point, expands while decelerating, spikes tear into droplets, then it
// collapses back. GPU-animated geometry (see shaders/pool/splash_*.glsl) —
// the CPU only streams radius/height/spike params.
struct CrownSplash {
  bool active = false;
  Vec3 center{0.0f, 0.0f, 0.0f};
  float age = 0.0f;
  float radius = 0.0f;
  float height = 0.0f;
  float spike = 0.0f;   // spike amplitude 0..1
  float life = 1.0f;    // fades the sheet out
};
static CrownSplash gCrown;

// The central jet: the column of water that shoots up after the crown
// collapses (the Rayleigh jet). Rendered as a stretched droplet-style quad;
// the CPU simulates its rise + fall ballistic arc.
struct JetColumn {
  bool active = false;
  Vec3 center{0.0f, 0.0f, 0.0f};
  float velY = 0.0f;
  float height = 0.0f;
  float radius = 0.0f;
  float life = 0.0f;
};
static JetColumn gJet;

struct Droplet {
  Vec3 pos;
  Vec3 vel;
  float radius;
  float life;     // seconds remaining
  float maxLife;
};
static std::vector<Droplet> gDroplets;

static TeapotPhysics gPot;

static const float kGravity = -13.6f;   // slightly heavier than Earth for drama
static const float kWaterLevel = 0.0f;
static const float kBounce = 0.18f;     // small rebound off the surface
static const float kDragWater = 2.6f;   // /s velocity damping in water
static const float kBuoyancy = 34.0f;   // upward accel when submerged

static void SpawnRing(float x, float z, float strength) {
  if (gRingCount < MAX_RINGS) {
    gRings[gRingCount++] = {x, z, 0.15f, strength};
  } else {
    // reuse the weakest
    int weakest = 0;
    for (int i = 1; i < MAX_RINGS; i++)
      if (gRings[i].strength < gRings[weakest].strength) weakest = i;
    gRings[weakest] = {x, z, 0.15f, strength};
  }
}

static void SpawnSplash(float x, float z, float impactSpeed) {
  const float s = std::fmin(impactSpeed / 10.0f, 1.6f);
  SpawnRing(x, z, std::fmin(1.0f, 0.55f + 0.45f * s));

  // ---- the crown: erupts around the impact rim, spikes tear outward ----
  gCrown.active = true;
  gCrown.center = {x, kWaterLevel, z};
  gCrown.age = 0.0f;
  gCrown.radius = 0.30f;
  gCrown.height = 0.22f + 0.55f * s;
  gCrown.spike = std::fmin(1.0f, 0.35f + 0.4f * s);
  gCrown.life = 1.0f;

  // ---- droplets: torn from the crown spikes, thrown ballistically. They
  // start AT the crown rim with mostly-outward velocities (a real crown
  // throws sheets/spears sideways-up, not a puff of spheres upward).
  int n = 26 + (int)(16.0f * s);
  for (int i = 0; i < n; i++) {
    // fixed pseudo-random spread (deterministic across runs like the rest
    // of the bench)
    float a = (float)((i * 137) % 360) * 3.14159265f / 180.0f;
    float r01 = ((i * 89) % 100) / 100.0f;
    Droplet d;
    float rimR = gCrown.radius + 0.08f + 0.10f * r01;
    d.pos = {x + std::cos(a) * rimR,
             kWaterLevel + 0.15f + 0.5f * gCrown.height * r01,
             z + std::sin(a) * rimR};
    // mostly outward + moderate up: real crown ejecta travels far sideways
    float out = 1.6f + 2.8f * s * (0.35f + 0.65f * r01);
    float up = 1.6f + 2.6f * s * r01;
    d.vel = {std::cos(a) * out, up, std::sin(a) * out};
    d.radius = 0.035f + 0.045f * ((i * 13) % 7) / 7.0f;
    d.maxLife = d.life = 0.9f + 0.6f * ((i * 29) % 5) / 5.0f;
    gDroplets.push_back(d);
  }

  // ---- the jet: delayed central column that erupts after the crown falls
  // (a real tank splash: cavity collapses -> Rayleigh jet shoots up)
  gJet.active = true;
  gJet.center = {x, kWaterLevel, z};
  gJet.velY = 0.0f;      // delayed: starts moving when the crown collapses
  gJet.height = 0.0f;
  gJet.radius = 0.10f + 0.06f * s;
  gJet.life = 0.0f;
}

static void ResetTeapot(double now) {
  gPot = TeapotPhysics{};
  gPot.pos = {0.0f, 8.0f, 0.0f};
  gPot.vel = {0.0f, -1.2f, 0.0f};
  (void)now;
  gRingCount = 0;
  gDroplets.clear();
  gCrown = CrownSplash{};
  gJet = JetColumn{};
}

// per-frame physics update
static void UpdatePhysics(double now, double dt) {
  // --- rings expand and fade ---
  for (int i = 0; i < gRingCount; i++) {
    Ring &r = gRings[i];
    r.radius += (1.1f + 2.2f * r.strength) * (float)dt;
    r.strength -= 0.42f * (float)dt;
  }
  // compact finished rings
  int w = 0;
  for (int i = 0; i < gRingCount; i++)
    if (gRings[i].strength > 0.02f) gRings[w++] = gRings[i];
  gRingCount = w;

  // --- droplets: ballistic strands, drag-lite (water drops barely slow) ---
  for (Droplet &d : gDroplets) {
    d.vel.y += kGravity * (float)dt;
    d.pos = Vec3Add(d.pos, Vec3Scale(d.vel, (float)dt));
    d.life -= (float)dt;
    if (d.pos.y < kWaterLevel && d.vel.y < 0.0f) {
      // landing droplet raises a micro-ring
      if (d.radius > 0.03f)
        SpawnRing(d.pos.x, d.pos.z, 0.14f + 0.3f * std::fmin(-d.vel.y / 6.0f, 1.0f));
      d.life = 0.0f;
    }
  }
  gDroplets.erase(std::remove_if(gDroplets.begin(), gDroplets.end(),
                                 [](const Droplet &d) { return d.life <= 0.0f; }),
                  gDroplets.end());

  // --- crown: expands fast, decelerates, then collapses ---
  if (gCrown.active) {
    gCrown.age += (float)dt;
    // radius grows decelerating: r ~ sqrt(t) (energy spread over the ring)
    float t = gCrown.age;
    gCrown.radius = 0.30f + 1.9f * std::sqrt(t) * 0.55f;
    gCrown.height *= 1.0f - std::fmin(1.6f * (float)dt, 0.9f); // falls back
    gCrown.spike *= 1.0f - std::fmin(0.8f * (float)dt, 0.9f);
    gCrown.life = 1.0f - t / 0.95f;
    if (gCrown.life <= 0.0f || gCrown.height < 0.03f) {
      gCrown.active = false;
      // jet launches as the crown collapses; the cavity is slightly off the
      // impact centre (the teapot floats to one side of it), so the column
      // rises beside the floating pot instead of hiding behind it
      gJet.velY = 4.6f;
      gJet.center.x += 0.9f;
      gJet.center.z += 0.4f;
    }
  }

  // --- jet: ballistic rise + fall, thins as it climbs ---
  if (gJet.active && gJet.velY != 0.0f) {
    gJet.velY += kGravity * (float)dt;
    gJet.height += gJet.velY * (float)dt;
    gJet.life += (float)dt;
    if (gJet.height < 0.0f) {
      gJet.height = 0.0f;
      gJet.velY = 0.0f;
      gJet.life = 0.0f;
      // jet impact: a modest final ring
      SpawnRing(gJet.center.x, gJet.center.z, 0.45f);
    }
  }

  // --- teapot ---
  TeapotPhysics &p = gPot;
  bool water = p.pos.y < kWaterLevel + p.restDepth;

  // gravity always
  p.vel.y += kGravity * (float)dt;

  if (water) {
    if (!p.inWater) {
      // ---- impact ----
      float speed = -p.vel.y;
      p.inWater = true;
      p.splashed = true;
      p.splashTime = now;
      SpawnSplash(p.pos.x, p.pos.z, speed);
      // small rebound then buoyancy takes over
      p.vel.y = speed * kBounce;
      p.yawVel *= 0.25f;
    }
    // buoyancy: stronger the deeper it sits, up to equilibrium
    float depth = kWaterLevel - p.pos.y;
    float buoy = kBuoyancy * (depth / p.restDepth) * (float)dt;
    if (buoy > 0.0f) p.vel.y += buoy;
    // water drag (quadratic-ish, clamped)
    float drag = 1.0f - std::fmin(kDragWater * (float)dt, 0.9f);
    p.vel.x *= drag; p.vel.y *= drag; p.vel.z *= drag;
    // righting: spin back to yaw 0 and settle
    p.yawVel *= 1.0f - std::fmin(3.0f * (float)dt, 0.9f);
    p.yaw += p.yawVel * (float)dt;
    // bob: damped spring around restDepth once the bounce has decayed
    if (now - p.splashTime > 0.55f) {
      float target = kWaterLevel - p.restDepth * 0.5f + 0.05f * std::sin((now - p.splashTime) * 2.1f);
      p.pos.y += (target - p.pos.y) * std::fmin(2.2f * (float)dt, 1.0f);
      if (std::fabs(target - p.pos.y) < 0.01f && std::fabs(p.vel.y) < 0.05f)
        p.settled = true;
    }
  } else {
    p.inWater = false;
    p.yaw += p.yawVel * (float)dt; // tumble
  }

  p.pos = Vec3Add(p.pos, Vec3Scale(p.vel, (float)dt));
  if (p.pos.y < kWaterLevel - 1.2f) p.pos.y = kWaterLevel - 1.2f; // hard floor of the sim
}

// ------------------------------------------------------------------- scene gl
static SDL_Window *gWindow = nullptr;
static SDL_GLContext gContext = nullptr;
static int gWindowWidth = WIDTH, gWindowHeight = HEIGHT;

static Program gSkyProg, gWaterProg, gTeapotProg, gDropletProg, gSplashProg, gHudProg;
static GLuint gTeapotTex = 0, gFontTex = 0;
static GLuint gEmptyVao = 0;

static Vec3 gCamPos{0.0f, 2.6f, 7.2f};
static float gCamYaw = 0.0f, gCamPitch = -0.28f;
static bool gAutoCam = true;
static bool gIsHoldingMouse = false;
static int gXOld = 0, gYOld = 0;
static float gCamDist = 7.2f;

static double gStartTime = 0.0;

// results screen + fused-run plumbing (same pattern as tidebench.cxx)
static bool gResultsShown = false;
static double gResultsElapsed = 0.0, gResultsFps = 0.0, gResultsScore = 0.0;
static double gResultsShownAt = 0.0;
static const double kResultsScreenSeconds = 4.0;
static const char *gSceneName = "Pool Room";
double gFusedPoolScore = 0.0;   // read by main.cxx for the combined screen
static bool gFusedDone = false;
static bool gStandaloneScene = false;
static int gFrame = 0, gFps = 0, gFrameAccum = 0;
static double gFpsTimer = 0.0;
static double gSmoothFps = 0.0;
static bool gQuit = false;

// headless visual-test state
static const char *gScreenshotPath = nullptr;
static std::vector<float> gShotTimes;
static size_t gNextShot = 0;
static int gWindowWidthOverride = 0;

static GLuint gHudVao = 0, gHudVbo = 0;
static int gHudVertexFloats = 0;

// ------------------------------------------------------------- camera path
static void UpdateAutoCamera(float t) {
  // slow orbit focused on the teapot's splash point, easing in height so the
  // drop, the splash and the bob are all framed
  float a = 0.32f + t * 0.055f;
  float radius = 7.6f + std::sin(t * 0.07f) * 1.1f;
  gCamPos.x = std::cos(a) * radius;
  gCamPos.z = std::sin(a) * radius;
  gCamPos.y = 2.9f + std::sin(t * 0.045f) * 0.7f;
  gCamYaw = std::atan2(-gCamPos.x, -gCamPos.z); // look at the centre
  gCamPitch = -0.30f + 0.06f * std::sin(t * 0.03f);
}

static Vec3 OrbitCamPos() {
  float cp = std::cos(gCamPitch), sp = std::sin(gCamPitch);
  Vec3 pos;
  pos.x = gCamPos.x + std::sin(gCamYaw) * cp * gCamDist;
  pos.y = gCamPos.y + sp * gCamDist;
  pos.z = gCamPos.z + std::cos(gCamYaw) * cp * gCamDist;
  if (pos.y < 0.35f) pos.y = 0.35f; // never dive under the water
  return pos;
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
  std::snprintf(line1, sizeof(line1), "FPS: %d", gFps);
  RenderText(16.0f, 16.0f, line1);
}

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

// -------------------------------------------------------------- render passes
static Mat4 gProj;

static void DrawSky(const Mat4 &view, const Vec3 &eye, double timeSec) {
  Mat4 vp;
  Mat4Multiply(vp, gProj, view);
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE); // camera is inside the dome
  glUseProgram(gSkyProg.handle);
  glBindVertexArray(gDomeMesh.vao);
  glUniformMatrix4fv(gSkyProg.loc("uViewProj"), 1, GL_FALSE, vp.data());
  // model = translate(eye) * scale(domeRadius)
  {
    Mat4 m;
    Mat4Identity(m);
    m[0] = kDomeRadius; m[5] = kDomeRadius; m[10] = kDomeRadius;
    m[12] = eye.x; m[13] = eye.y; m[14] = eye.z;
    glUniformMatrix4fv(gSkyProg.loc("uModel"), 1, GL_FALSE, m.data());
  }
  glUniform3f(gSkyProg.loc("uEyePos"), eye.x, eye.y, eye.z);
  glUniform3f(gSkyProg.loc("uCenter"), eye.x, eye.y, eye.z);
  glUniform1f(gSkyProg.loc("uRadius"), kDomeRadius);
  glUniform3f(gSkyProg.loc("uLightDir"), kLightDir[0], kLightDir[1], kLightDir[2]);
  glUniform3f(gSkyProg.loc("uLightTint"), kLightTint[0], kLightTint[1], kLightTint[2]);
  glUniform1f(gSkyProg.loc("uTime"), (float)timeSec);
  glDrawElements(GL_TRIANGLES, gDomeMesh.indexCount, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
  glEnable(GL_CULL_FACE);
  glDepthMask(GL_TRUE);
  glEnable(GL_DEPTH_TEST);
}

static void DrawWater(const Mat4 &view, const Vec3 &eye, double timeSec) {
  Mat4 vp;
  Mat4Multiply(vp, gProj, view);
  glUseProgram(gWaterProg.handle);
  glBindVertexArray(gWaterMesh.vao);
  glDepthMask(GL_TRUE);
  Mat4 model;
  Mat4Identity(model);
  glUniformMatrix4fv(gWaterProg.loc("uViewProj"), 1, GL_FALSE, vp.data());
  glUniformMatrix4fv(gWaterProg.loc("uModel"), 1, GL_FALSE, model.data());
  glUniform3f(gWaterProg.loc("uEyePos"), eye.x, eye.y, eye.z);
  glUniform3f(gWaterProg.loc("uLightDir"), kLightDir[0], kLightDir[1], kLightDir[2]);
  glUniform3f(gWaterProg.loc("uLightTint"), kLightTint[0], kLightTint[1], kLightTint[2]);
  glUniform3f(gWaterProg.loc("uTileA"), kTileA[0], kTileA[1], kTileA[2]);
  glUniform3f(gWaterProg.loc("uTileB"), kTileB[0], kTileB[1], kTileB[2]);
  glUniform1f(gWaterProg.loc("uTime"), (float)timeSec);
  glUniform1i(gWaterProg.loc("uRingCount"), gRingCount);
  float rings[MAX_RINGS * 4];
  for (int i = 0; i < MAX_RINGS; i++) {
    if (i < gRingCount) {
      rings[i * 4 + 0] = gRings[i].x;
      rings[i * 4 + 1] = gRings[i].z;
      rings[i * 4 + 2] = gRings[i].radius;
      rings[i * 4 + 3] = gRings[i].strength;
    } else {
      rings[i * 4 + 0] = 0.0f; rings[i * 4 + 1] = 0.0f;
      rings[i * 4 + 2] = 0.0f; rings[i * 4 + 3] = 0.0f;
    }
  }
  glUniform4fv(gWaterProg.loc("uRings"), MAX_RINGS, rings);
  glDrawElements(GL_TRIANGLES, gWaterMesh.indexCount, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
}

static void DrawTeapot(const Mat4 &view, const Vec3 &eye) {
  Mat4 vp;
  Mat4Multiply(vp, gProj, view);
  Mat4 model;
  Mat4Model(model, gPot.pos, gPot.yaw, 1.0f);
  float wetness = gPot.splashed ? 1.0f : 0.0f;

  glUseProgram(gTeapotProg.handle);
  glBindVertexArray(gTeapotMesh.vao);
  // Culling disabled for the teapot: user-supplied OBJs often mix winding
  // orders, and a single flipped face punches a visible hole in the model.
  glDisable(GL_CULL_FACE);
  glUniformMatrix4fv(gTeapotProg.loc("uViewProj"), 1, GL_FALSE, vp.data());
  glUniformMatrix4fv(gTeapotProg.loc("uModel"), 1, GL_FALSE, model.data());
  glUniform3f(gTeapotProg.loc("uEyePos"), eye.x, eye.y, eye.z);
  glUniform3f(gTeapotProg.loc("uLightDir"), kLightDir[0], kLightDir[1], kLightDir[2]);
  glUniform3f(gTeapotProg.loc("uLightTint"), kLightTint[0], kLightTint[1], kLightTint[2]);
  glUniform1f(gTeapotProg.loc("uWetness"), wetness);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, gTeapotTex);
  glUniform1i(gTeapotProg.loc("uBaseColor"), 0);
  glDrawElements(GL_TRIANGLES, gTeapotMesh.indexCount, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
  glEnable(GL_CULL_FACE);
}

static void DrawCrown(const Mat4 &view, double now) {
  if (!gCrown.active) return;
  Mat4 vp;
  Mat4Multiply(vp, gProj, view);
  glUseProgram(gSplashProg.handle);
  glBindVertexArray(gCrownVao);
  glUniformMatrix4fv(gSplashProg.loc("uViewProj"), 1, GL_FALSE, vp.data());
  glUniform3f(gSplashProg.loc("uCenter"), gCrown.center.x, gCrown.center.y, gCrown.center.z);
  glUniform3f(gSplashProg.loc("uCamPos"), gCamPos.x, gCamPos.y, gCamPos.z);
  glUniform1f(gSplashProg.loc("uRadius"), gCrown.radius);
  glUniform1f(gSplashProg.loc("uHeight"), gCrown.height);
  glUniform1f(gSplashProg.loc("uSpike"), gCrown.spike);
  glUniform1f(gSplashProg.loc("uTime"), (float)now);
  glUniform3f(gSplashProg.loc("uEyePos"), gCamPos.x, gCamPos.y, gCamPos.z);
  glUniform3f(gSplashProg.loc("uLightDir"), kLightDir[0], kLightDir[1], kLightDir[2]);
  glUniform3f(gSplashProg.loc("uLightTint"), kLightTint[0], kLightTint[1], kLightTint[2]);
  // life fades the sheet: bake into vParam via uHeight? No — pass life as
  // uSpike-independent uniform via the w channel of vParam in the shader.
  // Simplest: scale the alpha by reusing uSpike? Keep it clean: multiply
  // uHeight toward 0 collapses the crown, which naturally fades it.
  glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE); // the sheet is seen from both sides
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDrawElements(GL_TRIANGLES, gCrownVertexCount, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
  glDisable(GL_BLEND);
  glDepthMask(GL_TRUE);
  glEnable(GL_CULL_FACE);
}

static void DrawJet(const Mat4 &view, const Vec3 &eye) {
  if (!gJet.active || gJet.height <= 0.01f) return;
  // The Rayleigh jet: a thin vertical column rising from the collapse point.
  // Drawn as two crossed columns of overlapping droplet billboards (8
  // anchor points, each a soft round sprite of the column's half-width),
  // which visually merge into a 3D column from every angle. Zero stretch
  // (velocity 0) so the sprite shader just stamps round water blobs.
  Vec3 toCam = Vec3Normalize(Vec3Sub(eye, gJet.center));
  float a = std::atan2(toCam.x, toCam.z);
  const float fade = std::fmin(1.0f, 1.3f - gJet.life * 0.3f);
  static std::vector<float> jbuf;
  jbuf.clear();
  const float halfW = gJet.radius;
  for (int pass = 0; pass < 2; pass++) {
    float aa = a + pass * 1.5707963f;
    // stack overlapping billboards up the column so the sprites merge into
    // a solid water column (2 crossed layers, 11 anchors each = 132 tris)
    const int kSteps = 10;
    for (int i = 0; i <= kSteps; i++) {
      float sy = (float)i / kSteps;
      float taper = 1.0f - 0.35f * sy; // column thins as it rises
      float px = gJet.center.x;
      float py = gJet.center.y + sy * gJet.height;
      float pz = gJet.center.z;
      const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
      const int quadIdx[6] = {0, 1, 2, 0, 2, 3};
      for (int ci = 0; ci < 6; ci++) {
        const float *c = corners[quadIdx[ci]];
        jbuf.push_back(px); jbuf.push_back(py); jbuf.push_back(pz);
        jbuf.push_back(0.0f); jbuf.push_back(0.0f); jbuf.push_back(0.0f); // no stretch
        jbuf.push_back(c[0]); jbuf.push_back(c[1]);
        jbuf.push_back(halfW * taper);
        jbuf.push_back(fade);
      }
    }
  }

  Mat4 vp;
  Mat4Multiply(vp, gProj, view);
  glUseProgram(gDropletProg.handle);
  glBindVertexArray(gJetVao);
  glBindBuffer(GL_ARRAY_BUFFER, gJetVbo);
  glBufferData(GL_ARRAY_BUFFER, jbuf.size() * sizeof(float), jbuf.data(), GL_STREAM_DRAW);
  glUniformMatrix4fv(gDropletProg.loc("uViewProj"), 1, GL_FALSE, vp.data());
  glUniform3f(gDropletProg.loc("uLightTint"), kLightTint[0], kLightTint[1], kLightTint[2]);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE);
  glDrawArrays(GL_TRIANGLES, 0, (int)(jbuf.size() / 10));
  glBindVertexArray(0);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
}

static void DrawDroplets(const Mat4 &view, const Vec3 &eye) {
  (void)eye;
  if (gDroplets.empty()) return;

  // stream per-droplet 10 floats: pos3, vel3, corner uv2, radius+bright2
  static std::vector<float> buf;
  buf.clear();
  for (const Droplet &d : gDroplets) {
    float bright = std::fmin(1.0f, d.life / d.maxLife * 1.4f);
    const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    const int quadIdx[6] = {0, 1, 2, 0, 2, 3};
    for (int ci : quadIdx) {
      buf.push_back(d.pos.x); buf.push_back(d.pos.y); buf.push_back(d.pos.z);
      buf.push_back(d.vel.x); buf.push_back(d.vel.y); buf.push_back(d.vel.z);
      buf.push_back(corners[ci][0]); buf.push_back(corners[ci][1]);
      buf.push_back(d.radius); buf.push_back(bright);
    }
  }
  gDropletVertexFloats = (int)buf.size();

  Mat4 vp;
  Mat4Multiply(vp, gProj, view);
  glUseProgram(gDropletProg.handle);
  glBindVertexArray(gDropletVao);
  glBindBuffer(GL_ARRAY_BUFFER, gDropletVbo);
  glBufferData(GL_ARRAY_BUFFER, buf.size() * sizeof(float), buf.data(), GL_STREAM_DRAW);
  glUniformMatrix4fv(gDropletProg.loc("uViewProj"), 1, GL_FALSE, vp.data());
  glUniform3f(gDropletProg.loc("uLightTint"), kLightTint[0], kLightTint[1], kLightTint[2]);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE);
  glDrawArrays(GL_TRIANGLES, 0, gDropletVertexFloats / 10);
  glBindVertexArray(0);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
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
  if (dt > 0.1) dt = 0.1; // clamp hitches so physics never tunnels

  float t = (float)(now - gStartTime);
  UpdatePhysics(now, dt);

  if (gAutoCam) UpdateAutoCamera(t);
  Vec3 eye = gAutoCam ? gCamPos : OrbitCamPos();

  Mat4 view;
  {
    Vec3 look = {gPot.pos.x, std::fmax(gPot.pos.y, 0.0f) + 0.4f, gPot.pos.z};
    Mat4LookAt(view, eye, look, {0, 1, 0});
  }
  float aspect = (float)gWindowWidth / (float)gWindowHeight;
  Mat4Perspective(gProj, 45.0f, aspect, 0.1f, 2000.0f);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, gWindowWidth, gWindowHeight);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  DrawSky(view, eye, now);
  DrawWater(view, eye, now);
  DrawTeapot(view, eye);
  DrawCrown(view, now);
  DrawJet(view, eye);
  DrawDroplets(view, eye);
  RenderHUD();

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

  gFrame++;
  gFrameAccum++;
  if (now - gFpsTimer >= 1.0) {
    double inst = gFrameAccum / (now - gFpsTimer);
    gFps = (int)std::lround(inst);
    gSmoothFps = gSmoothFps > 0.0 ? gSmoothFps * 0.8 + inst * 0.2 : inst;
    gFrameAccum = 0;
    gFpsTimer = now;
    char title[256];
    std::snprintf(title, sizeof(title), "%s - FPS : %d", NAME, gFps);
    SDL_SetWindowTitle(gWindow, title);
  }
  if ((now - gStartTime) * 1000.0 >= (double)BENCH_MILLISECONDS) {
    double elapsed = now - gStartTime;
    double fps = (double)gFrame / elapsed;
    double score = fps * fps * 2.0;
    std::printf("Benchmark Results - Time : %.1fs, Average FPS : %.1f, Score : %.0f\n",
                elapsed, fps, score);
    std::fflush(stdout);
    gFusedPoolScore = score;
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
  } else if (event.key.keysym.sym == SDLK_r) {
    ResetTeapot(NowSeconds()); // re-drop the teapot
  } else if (event.key.keysym.sym == SDLK_LEFT) {
    gCamYaw -= 0.05f;
  } else if (event.key.keysym.sym == SDLK_RIGHT) {
    gCamYaw += 0.05f;
  } else if (event.key.keysym.sym == SDLK_UP) {
    gCamPitch = std::fmin(gCamPitch + 0.03f, -0.05f);
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
    if (gCamDist > 40.0f) gCamDist = 40.0f;
  }
}

static void HandleMouseMotion(const SDL_Event &event) {
  if (gIsHoldingMouse) {
    gCamYaw -= (event.motion.x - gXOld) * 0.005f;
    gXOld = event.motion.x;
    gCamPitch = std::fmin(std::fmax(gCamPitch + (event.motion.y - gYOld) * 0.004f, -1.2f), -0.05f);
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

int PoolSceneParseArgs(int argc, char **argv) {
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

void PoolSceneSetScreenshot(const char *path) { gScreenshotPath = path; }
void PoolSceneSetStandalone(bool standalone) { gStandaloneScene = standalone; }

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
  BuildWaterMesh();
  BuildDomeMesh();
  BuildFontAtlas();
  gTeapotMesh = LoadObjMesh(resolveAssetPath("assets/teapot.obj").c_str(), 0.55f);
  gTeapotTex = LoadTextureRGBA("assets/teapot_placeholder.png");

  glGenVertexArrays(1, &gEmptyVao);
  glGenVertexArrays(1, &gHudVao);
  glGenBuffers(1, &gHudVbo);
  glBindVertexArray(gHudVao);
  glBindBuffer(GL_ARRAY_BUFFER, gHudVbo);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
  glBindVertexArray(0);

  glGenVertexArrays(1, &gDropletVao);
  glGenBuffers(1, &gDropletVbo);
  glBindVertexArray(gDropletVao);
  glBindBuffer(GL_ARRAY_BUFFER, gDropletVbo);
  glEnableVertexAttribArray(0); // centre pos
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(1); // velocity (for stretch)
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(2); // uv corner
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void *)(6 * sizeof(float)));
  glEnableVertexAttribArray(3); // radius+bright
  glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void *)(8 * sizeof(float)));
  glBindVertexArray(0);

  // ---- crown splash mesh: a unit ring sheet (angle x height-param grid) ----
  {
    const int kSeg = 96, kRows = 5;
    std::vector<float> ring;
    std::vector<unsigned> ridx;
    for (int r = 0; r <= kRows; r++) {
      float hp = (float)r / kRows;
      for (int s = 0; s <= kSeg; s++) {
        ring.push_back((float)s / kSeg); // angle 0..1
        ring.push_back(hp);              // height param
        ring.push_back(1.0f);            // radius scale (unused slot)
        ring.push_back(0.0f);
      }
    }
    for (int r = 0; r < kRows; r++)
      for (int s = 0; s < kSeg; s++) {
        unsigned i0 = (unsigned)(r * (kSeg + 1) + s);
        unsigned i1 = i0 + (unsigned)(kSeg + 1);
        ridx.push_back(i0); ridx.push_back(i0 + 1); ridx.push_back(i1);
        ridx.push_back(i0 + 1); ridx.push_back(i1 + 1); ridx.push_back(i1);
      }
    gCrownVertexCount = (int)ridx.size();
    glGenVertexArrays(1, &gCrownVao);
    glGenBuffers(1, &gCrownVbo);
    glBindVertexArray(gCrownVao);
    glBindBuffer(GL_ARRAY_BUFFER, gCrownVbo);
    glBufferData(GL_ARRAY_BUFFER, ring.size() * sizeof(float), ring.data(), GL_STATIC_DRAW);
    GLuint cebo;
    glGenBuffers(1, &cebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, ridx.size() * sizeof(unsigned), ridx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); // angle + heightParam
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(1); // scale (unused)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
    glBindVertexArray(0);
    (void)cebo;
  }

  // ---- jet VAO (streamed, same 10-float layout as the droplets) ----
  glGenVertexArrays(1, &gJetVao);
  glGenBuffers(1, &gJetVbo);
  glBindVertexArray(gJetVao);
  glBindBuffer(GL_ARRAY_BUFFER, gJetVbo);
  glEnableVertexAttribArray(0); // centre pos
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(1); // velocity (0 for the jet: no stretch)
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(2); // uv corner
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void *)(6 * sizeof(float)));
  glEnableVertexAttribArray(3); // radius+bright
  glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void *)(8 * sizeof(float)));
  glBindVertexArray(0);

  gSkyProg = LinkProgram(resolveAssetPath("shaders/pool/object_vert.glsl").c_str(),
                         resolveAssetPath("shaders/pool/sky_frag.glsl").c_str());
  gWaterProg = LinkProgram(resolveAssetPath("shaders/pool/object_vert.glsl").c_str(),
                           resolveAssetPath("shaders/pool/water_frag.glsl").c_str());
  gTeapotProg = LinkProgram(resolveAssetPath("shaders/pool/object_vert.glsl").c_str(),
                            resolveAssetPath("shaders/pool/teapot_frag.glsl").c_str());
  gDropletProg = LinkProgram(resolveAssetPath("shaders/pool/droplet_vert.glsl").c_str(),
                             resolveAssetPath("shaders/pool/droplet_frag.glsl").c_str());
  gSplashProg = LinkProgram(resolveAssetPath("shaders/pool/splash_vert.glsl").c_str(),
                            resolveAssetPath("shaders/pool/splash_frag.glsl").c_str());
  gHudProg = LinkProgram(resolveAssetPath("shaders/ps14/hud_vert.glsl").c_str(),
                         resolveAssetPath("shaders/pool/hud_frag.glsl").c_str());

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
}

// ------------------------------------------------------ scene entry point
int RunPoolScene(bool *gaveUpOut) {
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

  gWindow = SDL_CreateWindow(NAME, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, winW, winH,
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
    std::printf("Pool room scene: OpenGL 3.3 core context unavailable — skipping this scene\n");
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
  ResetTeapot(0.0);

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
  if (gQuit) return 2;
  return 0;
}
