// ElectroBench — scene 3 of the single ElectroBench binary: the "pool room"
// — an infinite checkerboard sky reflected on open water, lit only by a
// hidden light source, with a teapot that falls from the sky, splashes down,
// and bobs on the surface with realistic-ish physics (gravity, buoyancy,
// drag, damping) on OpenGL 3.3 core.
//
// Like src/scene2.cxx, this translation unit is NOT a program of its own.
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
//   * THE FLEET: nine teapots (assets/teapot.obj — the real Utah teapot, one
//     material, placeholder texture the user can swap) fall from the sky at
//     scattered positions, sizes and drop heights, staggered so the pool is
//     constantly alive. Each pot splashes on impact, then buoyancy + drag +
//     bob settles it, rocking to rest. Tumbles in flight, rights itself in
//     the water.
//   * Real planar reflections: the whole fleet is re-rendered mirrored about
//     the water plane, stencil-masked to the visible water pixels and alpha
//     blended over the water shading — genuine reflections that ripple with
//     the surface.
//   * Splash droplets: velocity-stretched water strands with per-droplet
//     gravity, torn from each pot's crown, picking up the checker sky's
//     colours as they fly.
//
// Controls: drag orbits the camera, wheel zooms, F toggles the auto camera,
// R re-drops the whole fleet, ESC quits.
//
// Headless flags shared with the other scenes: --screenshot, --shot-times,
// --width; scene-specific: --pool-only (run just this scene).

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

// BUILD-P20: 720p fullscreen is the default presentation; main.cxx clears
// this for headless capture so the screenshot framebuffer stays pinned.
extern bool gWindowedMode;
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
#define NAME "ElectroBench - Scene 3 (Pool Room)"
#define WIDTH 1366
#define HEIGHT 768
#define BENCH_MILLISECONDS 45000 // 45 s, same as the other scenes

#define MAX_RINGS 54             // must match water_frag.glsl; split into
                                 // private per-pot windows (54/18 = 3 each)
                                 // per-pot windows below
#define MAX_BUBBLES 48           // must match water_frag.glsl: subsurface
                                 // bubble plume slots (BUILD-D8); the scene
                                 // uploads all 48 in one glUniform4fv plus a
                                 // live count so idle frames loop zero times

// BUILD-P19: THIS GRID WAS PAYING FOR NOTHING. The water plane is FLAT at
// y = 0 and the vertex shader (shaders/pool/object_vert.glsl) does
// uModel * aPos with an identity model and NO displacement of any kind —
// every ripple, every ring, every reflection is computed analytically in the
// FRAGMENT shader from vWorld. So the tessellation bought nothing at all:
// 220x220 is 48,400 vertices and 95,922 triangles submitted every frame to
// produce exactly the same image as two triangles would, because
// perspective-correct interpolation of vWorld is exact no matter how large
// the primitive is. It was pure vertex-stage cost on the heaviest pass in
// the scene, in front of the heaviest fragment shader in the project.
//
// 32 keeps a coarse grid for anything that later wants to displace the
// surface, and drops the count to 1,922 triangles — a 50x reduction in the
// pass that scene 3 was measurably slowest on.
static const int kWaterResolution = 32;   // grid verts per side (display grid;
                                          // the lighting is analytic per pixel)
static const float kWaterSize = 300.0f;   // water patch half-size reaches the
                                          // horizon haze
static const int kDomeSeg = 48, kDomeRings = 28;
static const float kDomeRadius = 800.0f;  // inside the far plane

// palette — the WHITE & RED pool-room checker. Must match sky_frag.glsl's
// tileA/tileB (the water shader receives these as uniforms).
static const float kTileA[3] = {2.30f, 2.30f, 2.26f}; // hot white tile (linear)
static const float kTileB[3] = {1.50f, 0.008f, 0.010f}; // deep pure red tile
static const float kLightTint[3] = {0.86f, 0.95f, 1.05f};// cool pool-room glow

// hidden light: direction TOWARD the light, high and behind the default
// camera so the water specular path lands between camera and teapot
static const float kLightDir[3] = {-0.30f, 0.52f, -0.80f};

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

// TRS-ish model matrix for the teapot: translate * rotY(yaw) * rotX(pitch)
// * uniform scale — the pitch column is the HYPER-REAL falling tumble.
static void Mat4Model(Mat4 &m, const Vec3 &pos, float yaw, float scale,
                      float pitch = 0.0f) {
  float c = std::cos(yaw), s = std::sin(yaw);
  float cp = std::cos(pitch), sp = std::sin(pitch);
  Mat4Identity(m);
  // rotY then rotX: columns combine as R = Ry * Rx
  m[0] = c * scale;        m[4] = s * sp * scale;  m[8]  = -s * cp * scale;  m[12] = pos.x;
  m[1] = 0.0f;             m[5] = cp * scale;      m[9]  = sp * scale;       m[13] = pos.y;
  m[2] = s * scale;        m[6] = -c * sp * scale; m[10] = c * cp * scale;   m[14] = pos.z;
  m[3] = 0.0f;             m[7] = 0.0f;            m[11] = 0.0f;             m[15] = 1.0f;
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

  // Interleaved pos3/normal3/uv2 with a weld map so corners that share a
  // position share a smoothed normal AND a consistent UV — required by the
  // real Utah teapot (bare "f v v v" faces, no vt/vn at all).
  struct WeldKey {
    int vi;
    int uvGen;   // 0 = file UV, 1 = generated (needs vertex position)
    float u, v;  // generated UV (or file UV baked in)
    bool operator==(const WeldKey &o) const {
      return vi == o.vi && uvGen == o.uvGen && u == o.u && v == o.v;
    }
  };
  struct WeldKeyHash {
    size_t operator()(const WeldKey &k) const {
      size_t h = std::hash<int>()(k.vi * 31 + k.uvGen);
      h ^= std::hash<float>()(k.u) + 0x9e3779b9u + (h << 6) + (h >> 2);
      h ^= std::hash<float>()(k.v) + 0x9e3779b9u + (h << 6) + (h >> 2);
      return h;
    }
  };
  struct WeldVal { int slot; float nx, ny, nz; };
  std::unordered_map<WeldKey, WeldVal, WeldKeyHash> weld;

  std::vector<float> data; // interleaved pos3/normal3/uv2
  std::vector<unsigned> idx;
  data.reserve(64 * 1024);
  idx.reserve(32 * 1024);

  struct Corner { int vi, ti, ni; };
  std::vector<std::array<Corner, 3>> faceTris;
  while (std::fgets(line, sizeof(line), f)) {
    if (line[0] != 'f' || line[1] != ' ') continue;
    Corner c[16];
    int nc = 0;
    const char *p = line + 2;
    while (*p && nc < 16) {
      int vi = 0, ti = 0, ni = 0;
      int n = std::sscanf(p, "%d/%d/%d", &vi, &ti, &ni);
      if (n <= 0) break;
      if (n == 1) { ti = 0; ni = 0; }
      c[nc++] = {vi, ti, ni};
      while (*p && *p != ' ') p++;
      while (*p == ' ') p++;
    }
    if (nc < 3) continue;
    for (int k = 1; k + 1 < nc; k++) {
      faceTris.push_back({c[0], c[k], c[k + 1]});
    }
  }
  std::rewind(f);

  // pass 1: accumulate face normals into every welded corner
  auto accumulateNormals = [&]() {
    for (const auto &tri : faceTris) {
      const Corner &A = tri[0], &B = tri[1], &C = tri[2];
      if (A.vi <= 0 || B.vi <= 0 || C.vi <= 0) continue;
      if ((size_t)A.vi * 3 - 2 >= vp.size() || (size_t)B.vi * 3 - 2 >= vp.size() ||
          (size_t)C.vi * 3 - 2 >= vp.size())
        continue;
      const float *pa = &vp[(A.vi - 1) * 3];
      const float *pb = &vp[(B.vi - 1) * 3];
      const float *pc = &vp[(C.vi - 1) * 3];
      float ux = (pb[0] - pa[0]) * s, uy = (pb[1] - pa[1]) * s, uz = (pb[2] - pa[2]) * s;
      float wx = (pc[0] - pa[0]) * s, wy = (pc[1] - pa[1]) * s, wz = (pc[2] - pa[2]) * s;
      float fnx = uy * wz - uz * wy;
      float fny = uz * wx - ux * wz;
      float fnz = ux * wy - uy * wx;
      for (const Corner &cn : tri) {
        if (cn.vi <= 0 || (size_t)(cn.vi - 1) * 3 + 2 >= vp.size()) continue;
        float px = (vp[(cn.vi - 1) * 3 + 0] - ctrX) * s;
        float py = (vp[(cn.vi - 1) * 3 + 1] - baseY) * s;
        float pz = (vp[(cn.vi - 1) * 3 + 2] - ctrZ) * s;
        float u, v;
        int uvGen;
        if (cn.ti > 0 && (size_t)(cn.ti - 1) * 2 + 1 < vt.size()) {
          u = vt[(cn.ti - 1) * 2 + 0]; v = vt[(cn.ti - 1) * 2 + 1];
          uvGen = 0;
        } else {
          float ang = std::atan2(pz, px);
          u = (ang / 6.2831853f) + 0.5f;
          v = py / (maxDim * s) + 0.5f;
          uvGen = 1;
        }
        WeldKey key{cn.vi, uvGen, u, v};
        auto it = weld.find(key);
        if (it == weld.end()) continue;
        if (cn.ni > 0 && (size_t)(cn.ni - 1) * 3 + 2 < vn.size()) {
          it->second.nx += vn[(cn.ni - 1) * 3 + 0];
          it->second.ny += vn[(cn.ni - 1) * 3 + 1];
          it->second.nz += vn[(cn.ni - 1) * 3 + 2];
        } else {
          it->second.nx += fnx; it->second.ny += fny; it->second.nz += fnz;
        }
      }
    }
  };

  // build the weld entries first (positions + UVs) so pass 1 can find them
  auto ensureEntries = [&]() {
    for (const auto &tri : faceTris) {
      for (const Corner &cn : tri) {
        if (cn.vi <= 0 || (size_t)(cn.vi - 1) * 3 + 2 >= vp.size()) continue;
        float px = (vp[(cn.vi - 1) * 3 + 0] - ctrX) * s;
        float py = (vp[(cn.vi - 1) * 3 + 1] - baseY) * s;
        float pz = (vp[(cn.vi - 1) * 3 + 2] - ctrZ) * s;
        float u, v;
        int uvGen;
        if (cn.ti > 0 && (size_t)(cn.ti - 1) * 2 + 1 < vt.size()) {
          u = vt[(cn.ti - 1) * 2 + 0]; v = vt[(cn.ti - 1) * 2 + 1];
          uvGen = 0;
        } else {
          float ang = std::atan2(pz, px);
          u = (ang / 6.2831853f) + 0.5f;
          v = py / (maxDim * s) + 0.5f;
          uvGen = 1;
        }
        WeldKey key{cn.vi, uvGen, u, v};
        if (weld.find(key) == weld.end()) {
          weld.emplace(key, WeldVal{(int)(data.size() / 8), 0.0f, 1.0f, 0.0f});
          data.push_back(px); data.push_back(py); data.push_back(pz);
          data.push_back(0.0f); data.push_back(1.0f); data.push_back(0.0f);
          data.push_back(u); data.push_back(v);
        }
      }
    }
  };
  ensureEntries();
  accumulateNormals();

  // write the smoothed normals back into the interleaved buffer
  for (const auto &kv : weld) {
    float nx = kv.second.nx, ny = kv.second.ny, nz = kv.second.nz;
    float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    int slot = kv.second.slot;
    if (len > 1e-9f) {
      data[slot * 8 + 3] = nx / len;
      data[slot * 8 + 4] = ny / len;
      data[slot * 8 + 5] = nz / len;
    }
  }

  // pass 2: emit indices in original winding order (culling is disabled for
  // teapots, so mixed winding cannot punch holes)
  for (const auto &tri : faceTris) {
    for (const Corner &cn : tri) {
      if (cn.vi <= 0 || (size_t)(cn.vi - 1) * 3 + 2 >= vp.size()) { idx.push_back(0); continue; }
      float px = (vp[(cn.vi - 1) * 3 + 0] - ctrX) * s;
      float py = (vp[(cn.vi - 1) * 3 + 1] - baseY) * s;
      float pz = (vp[(cn.vi - 1) * 3 + 2] - ctrZ) * s;
      float u, v;
      int uvGen;
      if (cn.ti > 0 && (size_t)(cn.ti - 1) * 2 + 1 < vt.size()) {
        u = vt[(cn.ti - 1) * 2 + 0]; v = vt[(cn.ti - 1) * 2 + 1];
        uvGen = 0;
      } else {
        float ang = std::atan2(pz, px);
        u = (ang / 6.2831853f) + 0.5f;
        v = py / (maxDim * s) + 0.5f;
        uvGen = 1;
      }
      auto it = weld.find(WeldKey{cn.vi, uvGen, u, v});
      idx.push_back(it != weld.end() ? (unsigned)it->second.slot : 0);
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
// The teapot: dropped from the sky, splashes, then PARKS at the fall point
// just under the waterline (BUILD-D5: no buoyancy recovery, no bob, no
// righting — it stays in the position it landed in, hull visible above the
// surface, and never moves again). Semi-implicit Euler driven by the fixed
// 1/120 s substepper below — deterministic renders at any frame rate.
struct TeapotPhysics {
  Vec3 pos{0.0f, 8.0f, 0.0f};
  Vec3 vel{0.0f, 0.0f, 0.0f};
  float yaw = 0.0f;
  float yawVel = 1.1f;      // slow tumble while airborne
  float pitch = 0.0f;       // HYPER-REAL: forward tumble about the X axis —
  float pitchVel = 0.0f;    // a dropped pot doesn't just spin on the spot
  float radius = 0.55f;     // bounding radius (world units, scale 1.0)
  bool inWater = false;
  bool splashed = false;
  double nextBoil = 0.0;    // BUILD-D7: next post-impact boil ring
  bool settled = false;
  bool active = false;      // fleet pots spawn on a stagger
  double spawnAt = 0.0;
  double splashTime = -1.0;
};

// ---- the fleet ------------------------------------------------------------
// Two waves of pots at scattered deterministic positions, sizes and drop
// heights. Wave one (pots 0..8) rains down over the first ten seconds; wave
// two (pots 9..17) starts once wave one has settled and splashes the OUTER
// ring of the pool — fresh chaotic impact while the first wave sits sunken
// on the basin floor.
// Ripple-ring slots are partitioned per pot (a private window each), so all
// concurrent splashes never overwrite each other's rings.
static const int kFleetCount = 18;
static const int kRingsPerPot = MAX_RINGS / kFleetCount;

static const float kFleetPos[kFleetCount][2] = {
    // wave one: inner field
    {0.0f, 0.0f},   {-4.8f, 2.6f},  {4.2f, -3.1f},  {-2.6f, -4.4f},
    {5.4f, 3.3f},   {-6.1f, -1.8f}, {1.9f, 5.2f},   {6.8f, -0.7f},
    {-1.2f, -6.6f},
    // wave two: the outer ring, beyond wave one's splash field
    {-7.4f, 5.8f},  {7.9f, 4.6f},   {8.6f, -4.2f},  {-5.9f, -7.8f},
    {3.1f, 8.4f},   {9.3f, 0.9f},   {-9.0f, -2.7f}, {0.4f, -9.4f},
    {5.7f, -8.6f}};
static const float kFleetScale[kFleetCount] = {
    // fleet-wide +35% per the user: pots read bigger against the splash
    // crowns they spawn (crowns/jets scale with the same factor already)
    1.96f, 1.66f, 2.35f, 1.40f, 1.80f, 2.16f, 1.53f, 1.73f, 2.05f,
    1.86f, 1.57f, 2.23f, 1.34f, 2.00f, 1.77f, 1.65f, 2.12f, 1.49f};
static const float kFleetDrop[kFleetCount] = {
    8.0f, 10.0f, 9.0f, 11.5f, 8.6f, 10.6f, 9.4f, 12.0f, 11.0f,
    13.0f, 14.5f, 12.4f, 15.0f, 13.6f, 14.0f, 12.8f, 15.5f, 13.2f};
static const double kFleetDelay[kFleetCount] = {
    // wave one: one pot a second. Wave two starts at 11.2s — by then the
    // wave-one pots have impacted, crowned and sunk to the floor.
    0.0, 1.1, 2.2, 3.3, 4.4, 5.5, 6.6, 7.7, 8.8,
    11.2, 12.1, 13.0, 13.9, 14.8, 15.7, 16.6, 17.5, 18.4};

// rings are POD: x, z, radius, strength — one private window per pot
struct Ring { float x, z, radius, strength; };
static Ring gRings[MAX_RINGS];
static int gRingUsed[kFleetCount] = {};
static Ring *RingWindow(int pot) { return &gRings[pot * kRingsPerPot]; }

// BUILD-D8 SUBSURFACE BUBBLES: the impact cavity entrains air; a plume of
// bubbles rises under each impact point for a couple of seconds, wobbling,
// then pops at the surface into a micro-ring. depth = metres BELOW the
// surface (positive down); the water shader paints each bubble at its
// PARALLAX-CORRECTED apparent position so the specks slide correctly under
// the low grazing camera.
struct Bubble { float x, z, depth, radius, rise, phase; int owner; };
static std::vector<Bubble> gBubbles;

// The Worthington crown: a STEEP water sheet (BUILD-D4). The sheet is
// nearly vertical — crown walls point UP, not outward — the radius stays
// close to the pot's footprint (r ≈ 1.2x pot radius, growth capped hard)
// and the height ramps fast to a tall lip. The old wide expanding dome
// (0.24→0.79·scale radius over 0.95 s) is what read as the cloud bank.
// GPU-animated geometry (see shaders/pool/splash_*.glsl) — the CPU only
// streams radius/height/spike params. One per pot.
struct CrownSplash {
  bool active = false;
  Vec3 center{0.0f, 0.0f, 0.0f};
  float age = 0.0f;
  float radius = 0.0f;
  float height = 0.0f;
  float height0 = 0.0f; // the height the rim RALLIES to (set at spawn)
  float spike = 0.0f;   // spike amplitude 0..1
  float spike0 = 0.0f;  // peak tearing amplitude (set at spawn)
  float life = 1.0f;    // fades the sheet out
  float scale = 1.0f;   // pot scale — radius growth must stay pot-proportional
};
static CrownSplash gCrowns[kFleetCount];

// The central jet: the column of water that shoots up after the crown
// collapses (the Rayleigh jet). Rendered as a stretched droplet-style quad;
// the CPU simulates its rise + fall ballistic arc. One per pot.
struct JetColumn {
  bool active = false;
  Vec3 center{0.0f, 0.0f, 0.0f};
  float velY = 0.0f;
  float height = 0.0f;
  float radius = 0.0f;
  float life = 0.0f;
};
static JetColumn gJets[kFleetCount];

// per-pot jet punch 0..1: how violently the Rayleigh jet fires on cavity
// collapse (bigger pots hitting harder punch taller, thicker columns)
static float gJetPunch[kFleetCount];

struct Droplet {
  Vec3 pos;
  Vec3 vel;
  float radius;
  float life;     // seconds remaining
  float maxLife;
  float delay;    // BUILD-P15: seconds before this strand is released. A real
                  // crown does not eject all its water at once — it tears
                  // progressively as the lip pinches, so the spray builds
                  // from the first finger to the last over ~0.3 s.
  int owner;      // fleet pot that spawned it (owns the micro-ring window)
};
static std::vector<Droplet> gDroplets;

static TeapotPhysics gPots[kFleetCount];

static const float kGravity = -13.6f;   // slightly heavier than Earth for drama
static const float kWaterLevel = 0.0f;
static const float kBounce = 0.0f;      // no rebound: the cavity tears the
                                        // plunge away on entry
static const float kDragWater = 2.6f;   // /s velocity damping in water
// BUILD-D5 PARK-AT-IMPACT: the user's actual ask — "pots shouldn't recover
// from the fall, they stay in the same position when they fell". A pot that
// lands stops dead a little way below its entry point (restY below) and is
// parked there FOREVER: no buoyancy spring, no bobbing, no righting. The
// hull stays visible above the waterline (the D4 sink-to-floor version
// vanished under the opaque water and left the pool looking empty).

static void SpawnRing(int pot, float x, float z, float strength) {
  Ring *win = RingWindow(pot);
  if (gRingUsed[pot] < kRingsPerPot) {
    win[gRingUsed[pot]++] = {x, z, 0.15f, strength};
  } else {
    // reuse the weakest ring in this pot's private window
    int weakest = 0;
    for (int i = 1; i < kRingsPerPot; i++)
      if (win[i].strength < win[weakest].strength) weakest = i;
    win[weakest] = {x, z, 0.15f, strength};
  }
}

// BUILD-P2 BIGGER CROWNS: the user asked for larger, more real splashes.
//  * the crown is a nearly-VERTICAL sheet that RISES: radius ≈ 1.3x the pot
//    footprint (never a wide dome), height ramps to ~2.5x pot radius (cap
//    raised 2.8 -> 3.6 m) so the sheet towers over the pot
//  * the lip FLARES outward at the top (real Worthington profile: cavity
//    necks in, lip unfurls) — see splash_vert.glsl
//  * the film tears LATE (tear weights shifted to the last third of life)
//  * ejecta goes UP, not out: lateral velocity stays bounded and air drag
//    bleeds it back near the rim — no spray halo across the pool
//  * the Rayleigh jet stays a slender vertical column near the impact axis
static void SpawnSplash(int pot, float x, float z, float impactSpeed, float scale) {
  const bool waveTwo = pot >= 9;
  const float s = std::fmin(impactSpeed / 10.0f, 1.6f);
  SpawnRing(pot, x, z, std::fmin(1.0f, 0.55f + 0.45f * s));

  // ---- the crown: tight radius, tall lip. Height scales with the drop
  // energy but stays bounded; radius grows only slightly from the pot rim.
  CrownSplash &crown = gCrowns[pot];
  crown.active = true;
  crown.center = {x, kWaterLevel, z};
  crown.age = 0.0f;
  crown.scale = scale;
  crown.radius = 0.68f * scale;   // ≈ 1.3x the pot's footprint: the sheet
                                  // hugs the cavity rim and RISES from there
  // BUILD-P15: THE CROWN NOW *BUILDS* INSTEAD OF APPEARING.
  //
  // This is the single largest realism defect left in the scene and it is a
  // TIMING bug, not a shape bug. `height` was written once at spawn and
  // `spike` at full amplitude, so the very first frame a splash existed it
  // was already a fully-formed, fully-torn star at full height.
  //
  // High-speed footage of a real Worthington crown says otherwise. The rim
  // at t=0 is a SMOOTH, almost perfectly circular collar of water thrown
  // radially outward by the displaced volume. It rises as a clean cylinder
  // for the first 60-100 ms. The spikes do not exist yet: they appear only
  // when the cavity underneath pinches off and the collapsing sheet loses its
  // support, roughly 150-250 ms in, and they grow over the next 100 ms. Then
  // the fingers thin, bead, and fly apart.
  //
  // So the crown now runs a four-stage timeline driven from `age`:
  //   0 .. 0.09 s   smooth collar rises to full height, no tearing
  //   0.09 .. 0.30 s pinch-off: spikes ramp in, lip flares
  //   0.30 .. life   fingers decay and the sheet thins to nothing
  // The vertex shader gets `age` as a uniform and does the shaping, so the
  // per-frame CPU cost is the same four multiplies it already had.
  crown.height0 = std::fmin((0.85f + 1.20f * s) * scale * (waveTwo ? 1.10f : 1.0f),
                            3.6f); // steep cap: height ≈ 2.5x the radius — the
                                   // water points UP and TOWERS (reference
                                   // crowns are tall narrow sheets, never wide
                                   // domes)
  crown.height = crown.height0;
  crown.spike0 = std::fmin(1.0f, 0.45f + 0.4f * s);
  crown.spike = 0.0f;   // a smooth rim is all there is at t = 0
  crown.life = waveTwo ? 1.1f : 1.0f;

  // ---- droplets: torn from the crown spikes, STEEP ballistic strands.
  // Mostly vertical launch with a small inward-biased lateral bleed so the
  // spray falls back near the crown instead of painting a halo across the
  // pool (the literal source of the horizon bank on build D3).
  //
  // BUILD-P15 DROPLET SIZE. The old radii were 36-80 mm, i.e. 7-16 cm
  // ACROSS. That is not spray, that is a hailstone: a 16 cm ball of water
  // hanging over a 1 m crown reads as a balloon, and there were only 46-66 of
  // them so the eye counted every one. Real Worthington ejecta is 4-25 mm
  // across and there are HUNDREDS of it, distributed as a power law (many
  // tiny, a few large) rather than uniformly. So: radius 5-26 mm, a power-
  // law size draw, and roughly twice the count. The count is affordable
  // because the strands are 2-5 px at pool scale once they are the right
  // size — the old ones were large AND numerous, which is the worst of both.
  int n = (86 + (int)(46.0f * s)) * (waveTwo ? 2 : 1);
  for (int i = 0; i < n; i++) {
    // fixed pseudo-random spread (deterministic across runs like the rest
    // of the bench)
    float a = (float)((i * 137 + pot * 61) % 360) * 3.14159265f / 180.0f;
    float r01 = ((i * 89 + pot * 37) % 100) / 100.0f;
    float u01 = ((i * 53 + pot * 19) % 100) / 100.0f;
    bool fragment = waveTwo && ((i * 31 + pot * 17) % 7) == 0; // torn sheet chunk
    Droplet d;
    d.owner = pot;
    float rimR = crown.radius + 0.06f + 0.07f * r01;   // tight rim anchor
    // BUILD-P15: ejecta leaves from the RISING SHEET, not from a fixed band.
    // Position on the sheet tracks how far up the crown the strand tore off,
    // so the crown visibly sheds as it climbs instead of erupting pre-formed.
    float onSheet = 0.18f + 0.78f * r01;
    d.pos = {x + std::cos(a) * rimR * (1.0f - 0.22f * onSheet),
             kWaterLevel + 0.10f + onSheet * crown.height0 * 0.92f,
             z + std::sin(a) * rimR * (1.0f - 0.22f * onSheet)};
    // STEEP ejecta: strong vertical kick, small lateral component that air
    // drag eats quickly (see the drag term in UpdatePhysics). The launch
    // speed now RISES with height on the sheet (the fastest, biggest drops
    // come off the lip), which is what gives a real crown its graded corona
    // instead of a uniform shrapnel burst.
    float out = (0.45f + 0.80f * s * (0.30f + 0.70f * r01)) * scale;
    float up = (3.2f + 4.6f * s * (0.35f + 0.65f * onSheet));
    d.vel = {std::cos(a) * out, up, std::sin(a) * out};
    // POWER-LAW SIZE. u^3 clusters the population toward the small end with
    // a long thin tail of big drops, which is what a real splash curtain is:
    // a haze of fine mist plus a handful of fat beads near the axis.
    float sz = 0.0050f + 0.0210f * (u01 * u01 * u01);
    // the fattest drops are the ones torn from the highest, fastest part of
    // the sheet, so size correlates with launch height
    sz *= 0.72f + 0.85f * onSheet;
    d.radius = sz * scale;
    if (fragment) d.radius *= 1.6f; // torn sheet chunk, no cloud ballooning
    d.maxLife = d.life = (0.62f + 0.52f * ((i * 29) % 5) / 5.0f) *
                         (waveTwo ? 1.10f : 1.0f);
    // BUILD-P15: staggered release. The lip thins and pinches FIRST, so the
    // highest strands on the sheet leave first and the base tears last; the
    // release time therefore tracks height on the sheet. Without this the
    // whole corona appears in one frame at impact, which is the same
    // "already finished" tell as the pre-formed crown.
    d.delay = 0.11f + 0.30f * onSheet + 0.05f * u01;
    gDroplets.push_back(d);
  }

  // ---- the jet: delayed central column that erupts after the crown falls
  // (a real tank splash: cavity collapses -> Rayleigh jet shoots up)
  JetColumn &jet = gJets[pot];
  jet.active = true;
  jet.center = {x, kWaterLevel, z};
  jet.velY = 0.0f;      // delayed: starts moving when the crown collapses
  jet.height = 0.0f;
  jet.radius = (0.12f + 0.06f * s) * scale;
  jet.life = 0.0f;

  // ---- BUILD-D8: the cavity entrains air — a bubble plume rises under
  // the impact point. Bubbles start at staggered depths with different
  // rise rates so the plume thins out naturally over ~2 s.
  int bn = 10 + (int)(6.0f * s);
  for (int i = 0; i < bn && (int)gBubbles.size() < MAX_BUBBLES; i++) {
    float ba = (float)((i * 149 + pot * 73) % 360) * 0.0174532925f;
    float brr = (0.08f + 0.30f * ((i * 41 + pot * 17) % 10) / 10.0f) * scale;
    Bubble b;
    b.owner = pot;
    b.x = x + std::cos(ba) * brr;
    b.z = z + std::sin(ba) * brr;
    b.depth = 0.5f + 0.9f * ((i * 29 + pot * 11) % 10) / 10.0f;
    b.radius = (0.028f + 0.030f * ((i * 13 + pot * 5) % 7) / 7.0f) * scale;
    b.rise = 0.45f + 0.50f * ((i * 61 + pot * 7) % 10) / 10.0f;
    b.phase = (float)((i * 97 + pot * 31) % 628) * 0.01f;
    gBubbles.push_back(b);
  }
}

static void ResetFleet() {
  for (int i = 0; i < kFleetCount; i++) {
    gPots[i] = TeapotPhysics{};
    gPots[i].active = false;
    gPots[i].spawnAt = kFleetDelay[i];
    gCrowns[i] = CrownSplash{};
    gJets[i] = JetColumn{};
    gRingUsed[i] = 0;
    gJetPunch[i] = 0.5f + 0.5f * ((i * 41) % 7) / 6.0f;
    Ring *win = RingWindow(i);
    for (int j = 0; j < kRingsPerPot; j++) win[j] = Ring{};
  }
  gDroplets.clear();
  gBubbles.clear();
}

static double gStartTime = 0.0;  // set in RunPoolScene; UpdatePhysics reads it.

// BUILD-D4 fixed-timestep driver: the VM renders at 9-14 FPS, so advancing
// the whole sim by one ~0.1 s frame step made the integration mushy and let
// fast droplets tunnel through the water plane between frames. The real
// dynamics run in fixed 1/120 s substeps (identical trajectories at any
// frame rate, deterministic screenshots); ring bookkeeping stays per-frame.
static const float kPhysicsStep = 1.0f / 120.0f;
static double gPhysicsAccum = 0.0;
// BUILD-D8 harness fix: screenshot gates and the bench window used to run on
// the raw wall clock while the SIM advances by the CLAMPED frame dt — on a
// 1 FPS software renderer sim time crawls ~10x slower than wall time, so
// "--shot-times 15" captured a pool that had only simulated ~1.5 s (the
// D4/D5-era "fewer than 36 frames / empty-looking late frames" mystery).
// gSimTime tracks the physics timebase; --shot-times now mean SIM seconds
// (identical to wall seconds at 60 FPS, deterministic at any frame rate).
static double gSimTime = 0.0;

static void UpdatePhysicsStep(double now, float dt) {
  (void)now;

  // --- rings expand and fade (per-pot windows) ---
  for (int i = 0; i < kFleetCount; i++) {
    Ring *win = RingWindow(i);
    for (int j = 0; j < gRingUsed[i]; j++) {
      win[j].radius += (1.1f + 2.2f * win[j].strength) * dt;
      win[j].strength -= 0.42f * dt;
    }
    int w = 0;
    for (int j = 0; j < gRingUsed[i]; j++)
      if (win[j].strength > 0.02f) win[w++] = win[j];
    gRingUsed[i] = w;
  }

  // --- droplets: STEEP ballistic strands with air drag (BUILD-D4) ---
  // drag bleeds the small lateral component fast (the spray falls back
  // around the crown instead of painting a wide halo across the pool)
  // while the vertical arc stays clean.
  std::vector<Droplet> pending;   // BUILD-D7 secondary ejecta staging
  for (Droplet &d : gDroplets) {
    if (d.delay > 0.0f) { d.delay -= dt; continue; }   // BUILD-P15: staged
                                                            // ejection
    float sp = std::sqrt(d.vel.x * d.vel.x + d.vel.y * d.vel.y + d.vel.z * d.vel.z);
    float cd = std::fmin(0.55f * sp * dt, 0.9f);   // quadratic-ish air drag
    d.vel.x -= d.vel.x * cd;
    d.vel.z -= d.vel.z * cd;
    d.vel.y -= d.vel.y * cd * 0.35f;               // vertical keeps speed
    d.vel.y += kGravity * dt;
    d.pos = Vec3Add(d.pos, Vec3Scale(d.vel, dt));
    d.life -= dt;
    if (d.pos.y < kWaterLevel && d.vel.y < 0.0f) {
      // BUILD-P15: the landing gates used to be `radius > 0.03f`, which was
      // sized to the old 36-80 mm drops. With realistic 5-26 mm ejecta that
      // test can NEVER pass, so every micro-ring AND every secondary droplet
      // this scene is famous for would have silently switched off. The
      // threshold is now 0.009 m — the size at which one drop actually
      // punches a visible ring — and it is stated against the new size
      // distribution rather than inherited from it.
      const float kRingDrop = 0.0090f;
      // landing droplet raises a micro-ring in its owner's window
      if (d.radius > kRingDrop)
        SpawnRing(d.owner, d.pos.x, d.pos.z,
                  0.14f + 0.3f * std::fmin(-d.vel.y / 6.0f, 1.0f));
      // BUILD-D7 SECONDARY EJECTA: a fast droplet throws a couple of tiny
      // kids back up (real rain-on-water behaviour). Collected out-of-loop
      // (no push_back during iteration) and hard-capped so a droplet storm
      // can never avalanche.
      if (d.radius > kRingDrop && gDroplets.size() + pending.size() < 4200 &&
          ((d.owner * 7 + (int)(d.pos.x * 13.0f)) % 2) == 0) {
        for (int k = 0; k < 2; k++) {
          Droplet s;
          s.owner = d.owner;
          float aa = (float)(((int)(d.pos.z * 31.0f) + k * 137) % 360) *
                     0.0174532925f;
          float sp2 = 0.25f + 0.20f * (float)k;
          s.pos = {d.pos.x, 0.02f, d.pos.z};
          s.vel = {std::cos(aa) * sp2,
                   1.4f + 0.9f * (float)((k * 53) % 5) / 5.0f +
                       0.15f * std::fmin(-d.vel.y, 6.0f),
                   std::sin(aa) * sp2};
          // children are much finer than their parent — a splash-back crown
          // is mist, not a shrunken copy of the drop that made it
          s.radius = d.radius * 0.45f;
          s.maxLife = s.life = 0.42f;
          s.delay = 0.0f;
          pending.push_back(s);
        }
      }
      d.life = 0.0f;
    }
  }
  gDroplets.insert(gDroplets.end(), pending.begin(), pending.end());
  gDroplets.erase(std::remove_if(gDroplets.begin(), gDroplets.end(),
                                 [](const Droplet &d) { return d.life <= 0.0f; }),
                  gDroplets.end());

  // --- BUILD-D8: subsurface bubbles rise, wobble, pop into micro-rings ---
  // Wobble phase rides gSimTime (SIM seconds): identical plume shape at any
  // frame rate, and no float-precision loss from the huge uptime clock.
  for (Bubble &b : gBubbles) {
    b.depth -= b.rise * dt;
    b.x += std::sin(b.phase + (float)gSimTime * 2.6f) * 0.06f * dt;
    b.z += std::cos(b.phase * 1.3f + (float)gSimTime * 3.1f) * 0.06f * dt;
    if (b.depth <= 0.035f) {
      // the bubble breaks the surface: a tiny residual ripple
      SpawnRing(b.owner, b.x, b.z, 0.10f);
      b.depth = -1.0f;   // dead
    }
  }
  gBubbles.erase(std::remove_if(gBubbles.begin(), gBubbles.end(),
                                [](const Bubble &b) { return b.depth < 0.0f; }),
                 gBubbles.end());

  for (int i = 0; i < kFleetCount; i++) {
    CrownSplash &crown = gCrowns[i];
    JetColumn &jet = gJets[i];

    // --- crown: STEEP sheet, near-zero radial growth, late collapse ---
    if (crown.active) {
      crown.age += dt;
      // BUILD-P2: the sheet hugs the pot's footprint but breathes outward
      // a little further as the lip unfurls (0.68 -> 0.82 of scale)
      float t = crown.age;
      // BUILD-P15: THE WORTHINGTON TIMELINE. The rim RALLIES over the first
      // 90 ms (smooth, untilted), tears over the next 210 ms as the cavity
      // pinches off, and only then starts falling. `height0`/`spike0` are the
      // peaks; these two shapes are the whole animation.
      float rise = std::fmin(t / 0.09f, 1.0f);
      rise = rise * rise * (3.0f - 2.0f * rise);        // smoothstep
      float pinch = std::fmin(t / 0.30f, 1.0f);
      pinch = pinch * pinch * (3.0f - 2.0f * pinch);
      crown.height = crown.height0 * rise *
                     (1.0f - std::fmin(0.55f * std::fmax(0.0f, t - 0.30f), 0.85f));
      crown.spike = crown.spike0 * pinch;
      // the lip unfurls outward late, which is when the sheet is fastest and
      // most aerated — not linearly from the first frame as it used to
      crown.radius = (0.68f + 0.20f * std::fmin(t, 0.42f)) * crown.scale;
      crown.life = 1.0f - t / 1.18f;      // bigger crowns linger a beat longer
      // BUILD-P15: the collapse test must NOT run during the rise. The sheet
      // is deliberately near-zero height for its first 90 ms (it is a collar
      // of water leaving the surface, not yet a crown), so testing
      // `height < 0.04` unconditionally retires every crown on its FIRST
      // 1/120 s physics step — the rise ramp and the collapse test were
      // fighting each other. The sheet may only be declared collapsed once it
      // has actually had a chance to rise.
      if (crown.life <= 0.0f || (t > 0.10f && crown.height < 0.04f)) {
        crown.active = false;
        // jet launches as the crown collapses — a real Rayleigh jet fires on
        // the cavity's inertial collapse; bigger pots cavitate deeper and
        // punch a taller column.
        float s = gJetPunch[i];
        jet.velY = 5.6f + 2.4f * s;   // taller crowns cavitate deeper: a
                                      // stronger Rayleigh punch
        // slender Rayleigh column HUGGING the impact axis (BUILD-D4: the old
        // +0.9/+0.4 lateral offset threw every jet away from its pot,
        // widening the far-field cloud)
        jet.radius = std::fmin(jet.radius * (0.85f + 0.5f * s),
                               0.075f * crown.scale + 0.020f);
      }
    }

    // --- jet: ballistic rise + fall, thins as it climbs ---
    if (jet.active && jet.velY != 0.0f) {
      jet.velY += kGravity * dt;
      jet.height += jet.velY * dt;
      jet.life += dt;
      if (jet.height < 0.0f) {
        jet.height = 0.0f;
        jet.velY = 0.0f;
        jet.life = 0.0f;
        // jet impact: a modest final ring
        SpawnRing(i, jet.center.x, jet.center.z, 0.45f);
      }
    }

    // --- pot ---
    TeapotPhysics &p = gPots[i];
    if (!p.active) {
      // BUILD-D8 harness fix: the spawn gate used the WALL clock while the
      // sim advances by clamped frame dt — on a slow renderer the fleet's
      // two waves fell behind the sim-timebase (screenshot gate, bubble
      // plumes and physics all run on gSimTime). Pots now spawn on the
      // same SIM schedule as everything else: --shot-times and the wave
      // choreography stay in lockstep at any frame rate.
      if (gSimTime >= p.spawnAt) {
        p.active = true;
        float sc = kFleetScale[i];
        p.pos = {kFleetPos[i][0], kFleetDrop[i], kFleetPos[i][1]};
        // HYPER-REAL drop: a small horizontal drift (no pot falls perfectly
        // straight), a tumble about BOTH axes, and per-pot phases so the
        // fleet doesn't fall in lockstep
        float ph = (float)((i * 37) % 11) / 11.0f;
        p.vel = {(ph - 0.5f) * 0.9f, -1.2f, ((i * 53) % 7 - 3.0f) / 7.0f * 0.9f};
        p.yawVel = 1.1f * (0.6f + 0.8f * ((i * 7) % 5) / 5.0f);
        p.pitch = 0.15f * ((i % 3) - 1);
        p.pitchVel = 1.7f * (0.5f + 0.9f * ph);
        p.radius = 0.55f * sc;
      }
      continue;
    }

    // entry plane: the pot registers a splash once its centre passes a
    // shallow depth scaled to its size
    bool water = p.pos.y < kWaterLevel - 0.12f * p.radius;

    // gravity always; AIR DRAG while falling (a real pot has drag — the
    // old vacuum fall reached unrealistic speeds on the 15 m drops)
    p.vel.y += kGravity * dt;
    if (!water) {
      float ad = 1.0f - std::fmin(0.16f * dt, 0.2f);
      p.vel.x *= ad; p.vel.y *= ad; p.vel.z *= ad;
    }

    if (water) {
      if (!p.inWater) {
        // ---- impact ----
        float speed = -p.vel.y;
        p.inWater = true;
        p.splashed = true;
        p.splashTime = now;
        // Wave-two pots plunge in from much higher drops and hit harder:
        // their crowns are taller and their ejecta carries more energy.
        SpawnSplash(i, p.pos.x, p.pos.z, speed * (i >= 9 ? 1.15f : 1.0f),
                    kFleetScale[i]);
        // a real impact throws a second, broader ring a beat behind the first
        SpawnRing(i, p.pos.x, p.pos.z, 0.40f + 0.3f * std::fmin(speed / 9.0f, 1.0f));
        // no rebound — the water absorbs the plunge. The horizontal drift
        // dies on entry (the cavity grabs the pot) and the tumble mostly
        // ends there (BUILD-D4: pots keep a LAST-LANDED orientation: they
        // are only weakly righted, and they sink).
        p.vel.y = speed * kBounce;
        p.vel.x *= 0.4f; p.vel.z *= 0.4f;
        p.yawVel *= 0.25f;
        p.pitchVel *= 0.25f;
      }
      // BUILD-D5 — PARK, DON'T RECOVER: the plunge dies on entry (kBounce
      // = 0 above), then heavy water drag lets the pot glide the last few
      // centimetres down to its rest line and STOP. No buoyancy, no bob, no
      // righting: it keeps the orientation it landed in and never moves
      // again. The hull stays visibly parked at the fall position.
      // heavy water drag kills the plunge over a few cm of depth
      p.vel.y += -0.55f * std::fabs(p.vel.y) * p.vel.y * dt;
      p.vel.y *= 1.0f - std::fmin(4.5f * dt, 0.9f);
      // lateral drag: the cavity grabs the pot
      p.vel.x *= 1.0f - std::fmin(kDragWater * dt, 0.9f);
      p.vel.z *= 1.0f - std::fmin(kDragWater * dt, 0.9f);
      // slow residual yaw/pitch drift while settling, then rest (no spring:
      // no righting torque — the pot KEEPS its landed orientation)
      p.yawVel *= 1.0f - std::fmin(1.6f * dt, 0.9f);
      p.yaw += p.yawVel * dt;
      p.pitchVel *= 1.0f - std::fmin(1.6f * dt, 0.9f);
      p.pitch += p.pitchVel * dt;
    } else {
      p.inWater = false;
      p.yaw += p.yawVel * dt;      // tumble
      p.pitch += p.pitchVel * dt;  // forward tumble
    }

    p.pos = Vec3Add(p.pos, Vec3Scale(p.vel, dt));
    // rest line: a little deeper than the entry plane, so the pot visibly
    // settles INTO the water but keeps most of its hull above the surface
    // (mesh is base-normalized: its lowest vertex sits at pos.y, hull top
    // reaches ~+0.6x its scale above the waterline when parked)
    float restY = kWaterLevel - 0.45f * p.radius;
    if (p.pos.y < restY) {
      // parked: freeze at the fall position forever (BUILD-D5)
      p.pos.y = restY;
      if (p.vel.y < 0.0f) p.vel.y = 0.0f;
      p.vel.x = 0.0f;
      p.vel.z = 0.0f;
      p.yawVel = 0.0f;
      p.pitchVel = 0.0f;
      p.settled = true;
    }
    // BUILD-D7 POST-IMPACT BOIL: for ~2.5 s after the splash the collapsed
    // cavity keeps outgassing — small weak rings pop across the impact area
    // like the surface is boiling, then the pool goes glassy again.
    if (p.settled && now - p.splashTime < 2.5 && now >= p.nextBoil) {
      p.nextBoil = now + 0.22;
      float ba = (float)((int)(now * 137.0) % 360) * 0.0174532925f;
      float br = p.radius * (0.2f + 0.5f * (float)((int)(now * 89.0) % 7) / 7.0f);
      SpawnRing(i, p.pos.x + std::cos(ba) * br, p.pos.z + std::sin(ba) * br,
                0.16f + 0.10f * (float)((int)(now * 53.0) % 5) / 5.0f);
    }
  }
}

// per-frame entry: accumulate real time, advance fixed substeps.
// BUILD-D6 FIX: the substeps must carry REAL absolute timestamps — the pot
// spawn check (now - gStartTime >= spawnAt) compares against the scene
// clock, and feeding the steps a hardcoded 0.0 meant no teapot EVER spawned
// (empty pool, no splashes, no rings — the D4/D5 screenshots in a nutshell).
// Each substep now gets a monotonic timestamp ending exactly at `now`.
static void UpdatePhysics(double now, double frameDt) {
  if (frameDt > 0.1) frameDt = 0.1;
  gSimTime += frameDt;
  gPhysicsAccum += frameDt;
  while (gPhysicsAccum >= (double)kPhysicsStep) {
    gPhysicsAccum -= (double)kPhysicsStep;
    UpdatePhysicsStep(now - gPhysicsAccum, kPhysicsStep);
  }
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

// results screen + fused-run plumbing (same pattern as scene2.cxx)
static bool gResultsShown = false;
static double gResultsElapsed = 0.0, gResultsFps = 0.0, gResultsScore = 0.0;
static double gResultsShownAt = 0.0;
static const double kResultsScreenSeconds = 4.0;
static const char *gSceneName = "Scene 3";
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
  // Slow orbit focused on the splash field. The camera NEVER chases the
  // falling fleet: when wave two opens up on the outer ring (~11s) it drifts
  // gently back along a smooth ramp, but stays at plane level — the drops
  // fall INTO frame, the camera does not rise or pitch up after them.
  float a = 0.32f + t * 0.055f;
  float spread = t > 11.0f ? std::fmin((t - 11.0f) * 0.35f, 1.6f) : 0.0f;
  float radius = 7.6f + std::sin(t * 0.07f) * 1.1f + spread;
  gCamPos.x = std::cos(a) * radius;
  gCamPos.z = std::sin(a) * radius;
  gCamPos.y = 2.9f + std::sin(t * 0.045f) * 0.7f + spread * 0.10f;
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
  // build tag: on-screen proof of which splash code the exe runs (the splash
  // look changed massively across commits — stale-build screenshots must be
  // detectable at a glance)
  char line1[128];
  std::snprintf(line1, sizeof(line1), "FPS: %d   Scene 3   build D9", gFps);
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
  // gRings is exactly MAX_RINGS x (x, z, radius, strength) — upload it flat.
  // Unused slots carry strength 0 and the shader skips them.
  glUniform4fv(gWaterProg.loc("uRings"), MAX_RINGS, &gRings[0].x);
  // BUILD-D7: stream the ACTUAL hulls and live splashes so the water can
  // paint real contact foam + anchored reflections (see water_frag.glsl).
  // The old analytic ghost darkened grazing water without knowing where any
  // pot was; these uniforms carry per-pot truth instead.
  static GLfloat hullData[kFleetCount * 4];
  static GLfloat splashData[kFleetCount * 4];
  for (int i = 0; i < kFleetCount; i++) {
    const TeapotPhysics &p = gPots[i];
    float hs = 0.0f;
    if (p.active && p.inWater)
      hs = std::fmin(1.0f, (float)(timeSec - p.splashTime) * 1.2f);
    hullData[i * 4 + 0] = p.pos.x;
    hullData[i * 4 + 1] = p.pos.z;
    hullData[i * 4 + 2] = p.radius;
    hullData[i * 4 + 3] = hs;
    const CrownSplash &c = gCrowns[i];
    const JetColumn &j = gJets[i];
    float cx = 0.0f, cz = 0.0f, cr = 0.0f, cs = 0.0f;
    if (c.active) {
      cx = c.center.x; cz = c.center.z; cr = c.radius;
      cs = std::fmin(1.0f, c.height * c.life * 2.0f);
    } else if (j.active && j.height > 0.01f) {
      cx = j.center.x; cz = j.center.z; cr = j.radius;
      cs = std::fmin(0.8f, j.height * 0.8f);
    }
    splashData[i * 4 + 0] = cx;
    splashData[i * 4 + 1] = cz;
    splashData[i * 4 + 2] = cr;
    splashData[i * 4 + 3] = cs;
  }
  glUniform4fv(gWaterProg.loc("uHulls"), kFleetCount, hullData);
  glUniform4fv(gWaterProg.loc("uSplashes"), kFleetCount, splashData);
  // BUILD-D8: subsurface bubble plumes — xy = bubble xz, z = depth below
  // the surface (m), w = radius (m). Dead/empty slots carry depth 0 and
  // the shader skips them.
  static GLfloat bubbleData[MAX_BUBBLES * 4];
  for (int i = 0; i < MAX_BUBBLES; i++) {
    if (i < (int)gBubbles.size()) {
      const Bubble &b = gBubbles[i];
      bubbleData[i * 4 + 0] = b.x;
      bubbleData[i * 4 + 1] = b.z;
      bubbleData[i * 4 + 2] = b.depth;
      bubbleData[i * 4 + 3] = b.radius;
    } else {
      bubbleData[i * 4 + 0] = 0.0f;
      bubbleData[i * 4 + 1] = 0.0f;
      bubbleData[i * 4 + 2] = 0.0f;
      bubbleData[i * 4 + 3] = 0.0f;
    }
  }
  glUniform4fv(gWaterProg.loc("uBubbles"), MAX_BUBBLES, bubbleData);
  glUniform1i(gWaterProg.loc("uBubbleCount"),
              (int)std::fmin((float)gBubbles.size(), (float)MAX_BUBBLES));
  glDrawElements(GL_TRIANGLES, gWaterMesh.indexCount, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
}

static void DrawTeapot(const Mat4 &view, const Vec3 &eye, const TeapotPhysics &pot,
                       float scale, bool reflectionPass, const float *wobble,
                       float now = 0.0f) {
  if (!pot.active) return;
  Mat4 vp;
  Mat4Multiply(vp, gProj, view);
  Vec3 pos = pot.pos;
  if (reflectionPass) {
    pos.y = 2.0f * kWaterLevel - pos.y; // mirror the anchor about the plane
    pos.x += wobble[0];                 // ripple shear: the image wobbles as
    pos.z += wobble[1];                 // the pot's own rings pass under it
  }
  Mat4 model;
  Mat4Model(model, pos, pot.yaw, scale, pot.pitch);
  if (reflectionPass) {
    // Mirror the geometry itself: M' = M * diag(1,-1,1), i.e. negate the
    // second column (column-major). This flips winding — culling stays off.
    model[4] = -model[4];
    model[5] = -model[5];
    model[6] = -model[6];
    model[7] = -model[7];
  }
  float wetness = pot.splashed ? 1.0f : 0.0f;

  glUseProgram(gTeapotProg.handle);
  glBindVertexArray(gTeapotMesh.vao);
  // Culling disabled for the teapot: user-supplied OBJs often mix winding
  // orders, and a single flipped face punches a visible hole in the model.
  // The reflection pass needs this too (mirroring flips winding).
  glDisable(GL_CULL_FACE);
  glUniformMatrix4fv(gTeapotProg.loc("uViewProj"), 1, GL_FALSE, vp.data());
  glUniformMatrix4fv(gTeapotProg.loc("uModel"), 1, GL_FALSE, model.data());
  glUniform3f(gTeapotProg.loc("uEyePos"), eye.x, eye.y, eye.z);
  glUniform3f(gTeapotProg.loc("uLightDir"), kLightDir[0], kLightDir[1], kLightDir[2]);
  glUniform3f(gTeapotProg.loc("uLightTint"), kLightTint[0], kLightTint[1], kLightTint[2]);
  glUniform1f(gTeapotProg.loc("uWetness"), wetness);
  glUniform1f(gTeapotProg.loc("uWaterLine"), kWaterLevel);
  glUniform3f(gTeapotProg.loc("uWaterBody"), 0.030f, 0.180f, 0.320f);
  glUniform1f(gTeapotProg.loc("uTime"), now);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, gTeapotTex);
  glUniform1i(gTeapotProg.loc("uBaseColor"), 0);
  glDrawElements(GL_TRIANGLES, gTeapotMesh.indexCount, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
  if (!reflectionPass) glEnable(GL_CULL_FACE); // the reflection caller restores
}

static void DrawFleet(const Mat4 &view, const Vec3 &eye, bool reflectionPass,
                      double now = 0.0) {
  const float zero[2] = {0.0f, 0.0f};
  for (int i = 0; i < kFleetCount; i++)
    DrawTeapot(view, eye, gPots[i], kFleetScale[i], reflectionPass,
               reflectionPass ? nullptr : zero, (float)now);
}

// ---- reflected teapots: 2D black ghosts ----------------------------------
// The old pass re-rendered the fleet mirrored about the water plane and
// alpha-blended it — a 3D ceramic pot swimming underwater, which is NOT what
// a real reflection looks like. The water shader now paints each pot as a
// 2D black silhouette: a smeared upright ghost anchored at the pot's base,
// drowned by the water body and broken apart by the ripple field.

static void DrawCrowns(const Mat4 &view, double now) {
  bool any = false;
  for (int i = 0; i < kFleetCount; i++) any = any || gCrowns[i].active || gJets[i].active;
  if (!any) return;
  Mat4 vp;
  Mat4Multiply(vp, gProj, view);
  glUseProgram(gSplashProg.handle);
  glBindVertexArray(gCrownVao);
  glUniformMatrix4fv(gSplashProg.loc("uViewProj"), 1, GL_FALSE, vp.data());
  glUniform3f(gSplashProg.loc("uCamPos"), gCamPos.x, gCamPos.y, gCamPos.z);
  glUniform3f(gSplashProg.loc("uEyePos"), gCamPos.x, gCamPos.y, gCamPos.z);
  glUniform3f(gSplashProg.loc("uLightDir"), kLightDir[0], kLightDir[1], kLightDir[2]);
  glUniform3f(gSplashProg.loc("uLightTint"), kLightTint[0], kLightTint[1], kLightTint[2]);
  // splash films are POOL WATER now: bright surface blue / deep body blue
  glUniform3f(gSplashProg.loc("uWaterA"), 0.30f, 0.62f, 0.86f);
  glUniform3f(gSplashProg.loc("uWaterB"), 0.030f, 0.180f, 0.320f);
  glUniform3f(gSplashProg.loc("uTileA"), kTileA[0], kTileA[1], kTileA[2]);
  glUniform3f(gSplashProg.loc("uTileB"), kTileB[0], kTileB[1], kTileB[2]);
  glUniform1f(gSplashProg.loc("uTime"), (float)now);
  glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE); // the sheet is seen from both sides
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  // BUILD-D4: draw the translucent sheets BACK-TO-FRONT (18 unsorted
  // alpha sheets composite differently depending on draw order — the
  // old arbitrary order occasionally stacked near sheets over far ones
  // and thickened the far-field wash).
  int order[kFleetCount];
  float dist[kFleetCount];
  int n = 0;
  for (int i = 0; i < kFleetCount; i++) {
    if (!gCrowns[i].active && !gJets[i].active) continue;
    float dx = gCrowns[i].center.x - gCamPos.x;
    float dz = gCrowns[i].center.z - gCamPos.z;
    dist[n] = dx * dx + dz * dz;
    order[n++] = i;
  }
  for (int a = 1; a < n; a++) {                 // insertion sort: n<=18
    int v = order[a];
    float dv = dist[a];
    int b = a - 1;
    while (b >= 0 && dist[b] < dv) {
      order[b + 1] = order[b]; dist[b + 1] = dist[b];
      b--;
    }
    order[b + 1] = v; dist[b + 1] = dv;
  }
  for (int k = 0; k < n; k++) {
    const int i = order[k];
    const CrownSplash &c = gCrowns[i];
    if (!c.active) continue;
    glUniform3f(gSplashProg.loc("uCenter"), c.center.x, c.center.y, c.center.z);
    glUniform1f(gSplashProg.loc("uRadius"), c.radius);
    glUniform1f(gSplashProg.loc("uHeight"), c.height);
    glUniform1f(gSplashProg.loc("uSpike"), c.spike);
    // Per-pot spike animation phase so nine crowns don't pulse in lockstep.
    glUniform1f(gSplashProg.loc("uPhase"), (float)i * 1.7f);
    glUniform1f(gSplashProg.loc("uJet"), 0.0f);
    glDrawElements(GL_TRIANGLES, gCrownVertexCount, GL_UNSIGNED_INT, nullptr);
  }
  // WORTHINGTON JET CONES: the central column erupting through each crown's
  // middle — the element every real splash has that a lone ring lacks
  // (high-speed footage: crown first, then the Rayleigh jet spikes up
  // through it, shedding droplets). The crown mesh doubles as the jet cone
  // via uJet=1 in the fragment shader; the sprite jets below add the
  // droplet texture around it.
  for (int k = 0; k < n; k++) {
    const int i = order[k];
    const JetColumn &jet = gJets[i];
    if (!jet.active || jet.height <= 0.02f) continue;
    glUniform3f(gSplashProg.loc("uCenter"), jet.center.x, jet.center.y, jet.center.z);
    // DE-CLOUD: slim translucent column — the old +0.10/×1.15/spike-0.9
    // cone rendered as a fat envelope wider than the pot itself
    glUniform1f(gSplashProg.loc("uRadius"), jet.radius + 0.02f);
    glUniform1f(gSplashProg.loc("uHeight"), jet.height * 1.02f);
    glUniform1f(gSplashProg.loc("uSpike"), 0.55f);
    glUniform1f(gSplashProg.loc("uPhase"), (float)i * 1.7f);
    glUniform1f(gSplashProg.loc("uJet"), 1.0f);
    glDrawElements(GL_TRIANGLES, gCrownVertexCount, GL_UNSIGNED_INT, nullptr);
  }
  glUniform1f(gSplashProg.loc("uJet"), 0.0f);
  glBindVertexArray(0);
  glDisable(GL_BLEND);
  glDepthMask(GL_TRUE);
  glEnable(GL_CULL_FACE);
}

static void DrawJets(const Mat4 &view, const Vec3 &eye) {
  (void)eye;
  bool any = false;
  for (int i = 0; i < kFleetCount; i++)
    if (gJets[i].active && gJets[i].height > 0.01f) any = true;
  if (!any) return;
  // The Rayleigh jet: a thin vertical column rising from the collapse point.
  // Drawn as two crossed columns of overlapping droplet billboards, which
  // visually merge into a 3D column from every angle. Zero stretch (velocity
  // 0) so the sprite shader just stamps round water blobs.
  static std::vector<float> jbuf;
  jbuf.clear();
  for (int p = 0; p < kFleetCount; p++) {
    const JetColumn &jet = gJets[p];
    if (!jet.active || jet.height <= 0.01f) continue;
    Vec3 toCam = Vec3Normalize(Vec3Sub(eye, jet.center));
    float a = std::atan2(toCam.x, toCam.z);
    // BUILD-D4: dimmer sprite jet — the shader cone (DrawCrowns) carries the
    // column now; the billboards only add droplet texture at the base
    const float fade = std::fmin(1.0f, 1.3f - jet.life * 0.3f) * 0.50f;
    const float halfW = jet.radius;
    for (int pass = 0; pass < 2; pass++) {
      float aa = a + pass * 1.5707963f;
      (void)aa;
      // stack overlapping billboards up the column so the sprites merge into
      // a solid water column (2 crossed layers, anchors each = 96 tris)
      const int kSteps = 6;
      for (int i = 0; i <= kSteps; i++) {
        float sy = (float)i / kSteps;
        float taper = 1.0f - 0.35f * sy; // column thins as it rises
        float px = jet.center.x;
        float py = jet.center.y + sy * jet.height;
        float pz = jet.center.z;
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
  }
  if (jbuf.empty()) return;

  Mat4 vp;
  Mat4Multiply(vp, gProj, view);
  glUseProgram(gDropletProg.handle);
  glBindVertexArray(gJetVao);
  glBindBuffer(GL_ARRAY_BUFFER, gJetVbo);
  glBufferData(GL_ARRAY_BUFFER, jbuf.size() * sizeof(float), jbuf.data(), GL_STREAM_DRAW);
  glUniformMatrix4fv(gDropletProg.loc("uViewProj"), 1, GL_FALSE, vp.data());
  glUniform3f(gDropletProg.loc("uLightTint"), kLightTint[0], kLightTint[1], kLightTint[2]);
  glUniform3f(gDropletProg.loc("uWaterA"), 0.30f, 0.62f, 0.86f);
  glUniform3f(gDropletProg.loc("uWaterB"), 0.030f, 0.180f, 0.320f);
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
    // BUILD-P15: a strand still in its staging delay has NOT been torn off
    // the sheet yet. Drawing it would stamp a frozen blob at the release
    // point — i.e. exactly the "cylinders hanging in the air" class of bug,
    // in a different uniform. Staged strands are simply not submitted.
    if (d.delay > 0.0f) continue;
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
  glUniform3f(gDropletProg.loc("uSkyA"), kTileA[0], kTileA[1], kTileA[2]);
  glUniform3f(gDropletProg.loc("uSkyB"), kTileB[0], kTileB[1], kTileB[2]);
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

  // BUILD-P20: SCENE 3 IS NOW REPRODUCIBLE. BUILD-D8 moved the screenshot
  // gate onto the simulation clock but left every DRAW reading the wall
  // clock, so uTime — and therefore the dome's checker drift, its caustics,
  // the water's ripple phases and the hull-foam ramp — was a function of how
  // fast the machine happened to be. Two runs of the identical binary at the
  // identical flag came out differing on 96% of pixels, max channel
  // difference 248 (see REFERENCES.md BUILD-P19). That is not merely
  // untidy: it makes the scene impossible to A/B, so every realism change
  // here would have been unfalsifiable. Everything below the physics driver
  // now runs on the simulation clock; `now` stays only where real elapsed
  // time is genuinely wanted (frame dt, the results screen).
  const double simNow = gSimTime;
  UpdatePhysics(simNow, dt);

  if (gAutoCam) UpdateAutoCamera((float)gSimTime);  // SIM time: framing stays
                                                    // deterministic at any FPS
  Vec3 eye = gAutoCam ? gCamPos : OrbitCamPos();

  Mat4 view;
  {
    // Gaze: level with the splash field. Pot positions are counted in PLAN
    // VIEW ONLY (x/z, height flattened) — otherwise airborne pots drag the
    // whole view up to follow the drop and the surface falls out of frame.
    Vec3 acc{0.0f, 0.0f, 0.0f};
    int n = 0;
    for (int i = 0; i < kFleetCount; i++) {
      if (!gPots[i].active) continue;
      acc.x += gPots[i].pos.x;
      acc.z += gPots[i].pos.z;
      n++;
    }
    Vec3 look = n > 0 ? Vec3{acc.x / n, 0.0f, acc.z / n}
                      : Vec3{kFleetPos[0][0], 0.0f, kFleetPos[0][1]};
    look.y = 0.55f; // just above the waterline: splashes in, sky in view
    Mat4LookAt(view, eye, look, {0, 1, 0});
  }
  float aspect = (float)gWindowWidth / (float)gWindowHeight;
  Mat4Perspective(gProj, 45.0f, aspect, 0.1f, 2000.0f);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, gWindowWidth, gWindowHeight);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  DrawSky(view, eye, simNow);
  DrawFleet(view, eye, false, simNow);  // pots above the surface
  DrawWater(view, eye, simNow);   // opaque water covers the submerged parts;
                                    // its shader now paints the reflected pots
                                    // as 2D black ghost silhouettes
  DrawCrowns(view, simNow);
  DrawJets(view, eye);
  DrawDroplets(view, eye);
  RenderHUD();

  if (gScreenshotPath && gNextShot < gShotTimes.size() &&
      gSimTime >= (double)gShotTimes[gNextShot]) {   // SIM seconds (D8 harness fix)
    WriteScreenshotPPM(gScreenshotPath);   // %d targets advance per shot
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
  // BUILD-D8 harness fix: in screenshot mode the shot list IS the run length —
  // never cut the window at 45 wall-seconds while the sim is still crawling
  // (llvmpipe used to strand the last shots; the user's real GPU is unaffected).
  if (gShotTimes.empty() &&
      (now - gStartTime) * 1000.0 >= (double)BENCH_MILLISECONDS) {
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
    ResetFleet(); // re-drop the whole fleet
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
// BUILD-P21: --shot-time S (singular) is main.cxx's documented one-frame flag
// but only --shot-times ever reached this scene, so the singular form set the
// path with an EMPTY list and was silently ignored. See scene2.cxx.
void PoolSceneSetShotTime(float t) {
  if (gShotTimes.empty()) gShotTimes.push_back(t);
}
void PoolSceneSetStandalone(bool standalone) { gStandaloneScene = standalone; }

static void WriteScreenshotPPM(const char *path) {
  const int w = gWindowWidth, h = gWindowHeight;
  std::vector<unsigned char> rgb((size_t)w * h * 3);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
  char resolved[1024];
  if (std::strchr(path, '%')) {
    // printf-style frame-sequence target (e.g. frames/frame-%03d.ppm) so the
    // headless flags can also capture ANIMATED sequences: each call writes
    // frame 000, 001, 002... which assemble into GIF/MP4 showcase clips.
    std::snprintf(resolved, sizeof(resolved), path, gNextShot);
    path = resolved;
  }
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
  gTeapotTex = LoadTextureRGBA("assets/teapot_copper.png");

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
  gWindowWidth = winW;
  gWindowHeight = winH;

  // BUILD-P10: 4x MSAA. The room is full of thin geometry (crown walls, jets,
  // droplets, the teapot silhouette against a hard red/white tile edge) and
  // every one of those edges is a stair-step without coverage antialiasing.
  // A driver that refuses the request is retried without it rather than
  // dropping the scene.
  bool msaa = true;
  for (int attempt = 0; attempt < 2; attempt++) {
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, msaa ? 1 : 0);
    SDL_GL_SetAttribute(SDL_GL_SAMPLES, msaa ? 4 : 0);
    gWindow = SDL_CreateWindow(NAME, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, winW, winH,
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
    std::printf("Pool room scene: OpenGL 3.3 core context unavailable — skipping this scene\n");
    std::fflush(stdout);
    SDL_Quit();
    if (gaveUpOut) *gaveUpOut = true;
    return 1;
  }
  // BUILD-P24: FULLSCREEN_DESKTOP, not FULLSCREEN. Exclusive fullscreen hands the
  // display to the OpenGL driver and the window stops being an ordinary top-level
  // window, so the desktop compositor has nothing to composite -- and Win+PrtScr
  // captures the composited desktop, which came out black. Borderless
  // fullscreen still fills the screen but stays a normal window, so the capture
  // path works. It also sizes the window to the desktop rather than to the
  // requested 1280x720, so the drawing size has to be read back: the HUD and
  // the projection both use gWindowWidth/gWindowHeight, and left at 720p on a
  // 1080p desktop the scene would be drawn into one corner.
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
  // BUILD-P25: report the real presentation mode, so "the screenshot is black"
  // can be told apart into a stale build vs a capture tool that cannot grab the
  // compositor.
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
  ResetFleet();

  gStartTime = NowSeconds();
  gFpsTimer = gStartTime;
  gSimTime = 0.0;      // D8 harness fix: screenshot times are SIM seconds
  gPhysicsAccum = 0.0;

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
