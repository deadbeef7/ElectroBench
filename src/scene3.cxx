// ElectroBench — scene 3 of the single ElectroBench binary: the "pool room"
// — an infinite checkerboard sky reflected on open water, lit only by a
// hidden light source, with a teapot that falls from the sky, splashes down,
// and bobs on the surface with realistic-ish physics (gravity, buoyancy,
// drag, damping) on OpenGL 3.3 core.
//
// Like src/scene2.cxx, this translation unit is NOT a program of its own.
// It exports RunPoolScene(), which main.cxx calls as the third scene of the
// one and only ElectroBench executable.


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

enum LodePNGColorType { LCT_GREY = 0, LCT_RGB = 2, LCT_PALETTE = 3,
                        LCT_GREY_ALPHA = 4, LCT_RGBA = 6 };
unsigned lodepng_decode_file(unsigned char **out, unsigned *w, unsigned *h,
                             const char *filename, LodePNGColorType colorType,
                             unsigned bitdepth);

// ------------------------------------------------------------------ constants
#define NAME "ElectroBench - Scene 3 (Pool Room)"
#define WIDTH 1366
#define HEIGHT 768
#define BENCH_MILLISECONDS 45000 

#define MAX_RINGS 54             
#define MAX_BUBBLES 48     
                      
static const int kWaterResolution = 32;   
static const float kWaterSize = 300.0f;   
static const int kDomeSeg = 48, kDomeRings = 28;
static const float kDomeRadius = 800.0f; 


static const float kTileA[3] = {2.30f, 2.30f, 2.26f}; 
static const float kTileB[3] = {1.50f, 0.008f, 0.010f};
static const float kLightTint[3] = {0.86f, 0.95f, 1.05f};


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
static void Mat4Model(Mat4 &m, const Vec3 &pos, float yaw, float scale,
                      float pitch = 0.0f) {
  float c = std::cos(yaw), s = std::sin(yaw);
  float cp = std::cos(pitch), sp = std::sin(pitch);
  Mat4Identity(m);
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

// minimal obj loading
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

  struct WeldKey {
    int vi;
    int uvGen;  
    float u, v;  
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


struct TeapotPhysics {
  Vec3 pos{0.0f, 8.0f, 0.0f};
  Vec3 vel{0.0f, 0.0f, 0.0f};
  float yaw = 0.0f;
  float yawVel = 1.1f;      
  float pitch = 0.0f;      
  float pitchVel = 0.0f;    
  float radius = 0.55f;     
  bool inWater = false;
  bool splashed = false;
  double nextBoil = 0.0;    
  bool settled = false;
  bool active = false;      
  double spawnAt = 0.0;
  double splashTime = -1.0;
};

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

struct Ring { float x, z, radius, strength; };
static Ring gRings[MAX_RINGS];
static int gRingUsed[kFleetCount] = {};
static Ring *RingWindow(int pot) { return &gRings[pot * kRingsPerPot]; }
struct Bubble { float x, z, depth, radius, rise, phase; int owner; };
static std::vector<Bubble> gBubbles;

struct CrownSplash {
  bool active = false;
  Vec3 center{0.0f, 0.0f, 0.0f};
  float age = 0.0f;
  float radius = 0.0f;
  float height = 0.0f;
  float height0 = 0.0f; 
  float spike = 0.0f;   
  float spike0 = 0.0f; 
  float life = 1.0f;   
  float scale = 1.0f;  
};
static CrownSplash gCrowns[kFleetCount];

struct JetColumn {
  bool active = false;
  Vec3 center{0.0f, 0.0f, 0.0f};
  float velY = 0.0f;
  float height = 0.0f;
  float radius = 0.0f;
  float life = 0.0f;
};
static JetColumn gJets[kFleetCount];

static float gJetPunch[kFleetCount];

struct Droplet {
  Vec3 pos;
  Vec3 vel;
  float radius;
  float life;    
  float maxLife;
  float delay;   
  int owner;      
};
static std::vector<Droplet> gDroplets;

static TeapotPhysics gPots[kFleetCount];

static const float kGravity = -13.6f;   // slightly heavier than Earth
static const float kWaterLevel = 0.0f;
static const float kBounce = 0.0f;     
static const float kDragWater = 2.6f;  


static void SpawnRing(int pot, float x, float z, float strength) {
  Ring *win = RingWindow(pot);
  if (gRingUsed[pot] < kRingsPerPot) {
    win[gRingUsed[pot]++] = {x, z, 0.15f, strength};
  } else {
    int weakest = 0;
    for (int i = 1; i < kRingsPerPot; i++)
      if (win[i].strength < win[weakest].strength) weakest = i;
    win[weakest] = {x, z, 0.15f, strength};
  }
}

static void SpawnSplash(int pot, float x, float z, float impactSpeed, float scale) {
  const bool waveTwo = pot >= 9;
  const float s = std::fmin(impactSpeed / 10.0f, 1.6f);
  SpawnRing(pot, x, z, std::fmin(1.0f, 0.55f + 0.45f * s));
  CrownSplash &crown = gCrowns[pot];
  crown.active = true;
  crown.center = {x, kWaterLevel, z};
  crown.age = 0.0f;
  crown.scale = scale;
  crown.radius = 0.68f * scale;  
  crown.height0 = std::fmin((0.85f + 1.20f * s) * scale * (waveTwo ? 1.10f : 1.0f),
                            3.6f);
  crown.height = crown.height0;
  crown.spike0 = std::fmin(1.0f, 0.45f + 0.4f * s);
  crown.spike = 0.0f;  
  crown.life = waveTwo ? 1.1f : 1.0f;
  int n = (86 + (int)(46.0f * s)) * (waveTwo ? 2 : 1);
  for (int i = 0; i < n; i++) {
    float a = (float)((i * 137 + pot * 61) % 360) * 3.14159265f / 180.0f;
    float r01 = ((i * 89 + pot * 37) % 100) / 100.0f;
    float u01 = ((i * 53 + pot * 19) % 100) / 100.0f;
    bool fragment = waveTwo && ((i * 31 + pot * 17) % 7) == 0; 
    Droplet d;
    d.owner = pot;
    float rimR = crown.radius + 0.06f + 0.07f * r01;   
    float onSheet = 0.18f + 0.78f * r01;
    d.pos = {x + std::cos(a) * rimR * (1.0f - 0.22f * onSheet),
             kWaterLevel + 0.10f + onSheet * crown.height0 * 0.92f,
             z + std::sin(a) * rimR * (1.0f - 0.22f * onSheet)};
    float out = (0.45f + 0.80f * s * (0.30f + 0.70f * r01)) * scale;
    float up = (3.2f + 4.6f * s * (0.35f + 0.65f * onSheet));
    d.vel = {std::cos(a) * out, up, std::sin(a) * out};
    float sz = 0.0050f + 0.0210f * (u01 * u01 * u01);
    sz *= 0.72f + 0.85f * onSheet;
    d.radius = sz * scale;
    if (fragment) d.radius *= 1.6f; 
    d.maxLife = d.life = (0.62f + 0.52f * ((i * 29) % 5) / 5.0f) *
                         (waveTwo ? 1.10f : 1.0f);
    d.delay = 0.11f + 0.30f * onSheet + 0.05f * u01;
    gDroplets.push_back(d);
  }
  JetColumn &jet = gJets[pot];
  jet.active = true;
  jet.center = {x, kWaterLevel, z};
  jet.velY = 0.0f;    
  jet.height = 0.0f;
  jet.radius = (0.12f + 0.06f * s) * scale;
  jet.life = 0.0f;
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
static void ResetFleet(double t0 = 0.0) {
  for (int i = 0; i < kFleetCount; i++) {
    gPots[i] = TeapotPhysics{};
    gPots[i].active = false;
    gPots[i].spawnAt = t0 + kFleetDelay[i];
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

static double gStartTime = 0.0;
static const float kPhysicsStep = 1.0f / 120.0f;
static double gPhysicsAccum = 0.0;
static double gSimTime = 0.0;
static const float kPullbackPerPot = 0.55f;  
static const float kPullbackMax = 9.5f;    
static const double kReturnSeconds = 3.0;  
static const double kResplashHold = 3.2;   
static const float kResplashSpeed = 8.5f;    

static float gCamPullback = 0.0f; 
static int gPhase = 0;           
static double gPhaseT = 0.0;      
static int FleetSpawned() {
  int n = 0;
  for (int i = 0; i < kFleetCount; i++)
    if (gPots[i].active) n++;
  return n;
}

static bool FleetAllDown() {
  for (int i = 0; i < kFleetCount; i++)
    if (!gPots[i].active || !gPots[i].settled) return false;
  return true;
}

static void ResplashAll() {
  for (int i = 0; i < kFleetCount; i++) {
    TeapotPhysics &p = gPots[i];
    if (!p.active) continue;
    SpawnSplash(i, p.pos.x, p.pos.z, kResplashSpeed, kFleetScale[i]);
    SpawnRing(i, p.pos.x, p.pos.z, 0.55f);
    p.splashTime = gSimTime;  
    p.nextBoil = gSimTime + 0.25;
  }
}

static void UpdateChoreography(double simNow, double dt) {
  float target = 0.0f;

  if (gPhase == 0) {
    target = std::fmin((float)FleetSpawned() * kPullbackPerPot, kPullbackMax);
    if (FleetAllDown()) {
      gPhase = 1;
      gPhaseT = simNow;
    }
  } else if (gPhase == 1) {
    target = 0.0f;
    if (simNow - gPhaseT >= kReturnSeconds && gCamPullback <= 0.30f) {
      ResplashAll();
      gPhase = 2;
      gPhaseT = simNow;
    }
  } else {
    target = 0.0f;
    if (simNow - gPhaseT >= kResplashHold) {
      ResetFleet(simNow);
      gCamPullback = 0.0f;
      gPhase = 0;
      gPhaseT = simNow;
    }
  }

  float k = 1.0f - std::exp(-1.6f * (float)dt);
  gCamPullback += (target - gCamPullback) * k;
}

static void UpdatePhysicsStep(double now, float dt) {
  (void)now;
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
  std::vector<Droplet> pending;   
  for (Droplet &d : gDroplets) {
    if (d.delay > 0.0f) { d.delay -= dt; continue; }   
                                                            
    float sp = std::sqrt(d.vel.x * d.vel.x + d.vel.y * d.vel.y + d.vel.z * d.vel.z);
    float cd = std::fmin(0.55f * sp * dt, 0.9f);   
    d.vel.x -= d.vel.x * cd;
    d.vel.z -= d.vel.z * cd;
    d.vel.y -= d.vel.y * cd * 0.35f;             
    d.vel.y += kGravity * dt;
    d.pos = Vec3Add(d.pos, Vec3Scale(d.vel, dt));
    d.life -= dt;
    if (d.pos.y < kWaterLevel && d.vel.y < 0.0f) {
      const float kRingDrop = 0.0090f;
      if (d.radius > kRingDrop)
        SpawnRing(d.owner, d.pos.x, d.pos.z,
                  0.14f + 0.3f * std::fmin(-d.vel.y / 6.0f, 1.0f));
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

  for (Bubble &b : gBubbles) {
    b.depth -= b.rise * dt;
    b.x += std::sin(b.phase + (float)gSimTime * 2.6f) * 0.06f * dt;
    b.z += std::cos(b.phase * 1.3f + (float)gSimTime * 3.1f) * 0.06f * dt;
    if (b.depth <= 0.035f) {
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
    if (crown.active) {
      crown.age += dt;
      float t = crown.age;
      float rise = std::fmin(t / 0.09f, 1.0f);
      rise = rise * rise * (3.0f - 2.0f * rise);       
      float pinch = std::fmin(t / 0.30f, 1.0f);
      pinch = pinch * pinch * (3.0f - 2.0f * pinch);
      crown.height = crown.height0 * rise *
                     (1.0f - std::fmin(0.55f * std::fmax(0.0f, t - 0.30f), 0.85f));
      crown.spike = crown.spike0 * pinch;
      crown.radius = (0.68f + 0.20f * std::fmin(t, 0.42f)) * crown.scale;
      crown.life = 1.0f - t / 1.18f;   
      if (crown.life <= 0.0f || (t > 0.10f && crown.height < 0.04f)) {
        crown.active = false;
        float s = gJetPunch[i];
        jet.velY = 5.6f + 2.4f * s;   
        jet.radius = std::fmin(jet.radius * (0.85f + 0.5f * s),
                               0.075f * crown.scale + 0.020f);
      }
    }
    if (jet.active && jet.velY != 0.0f) {
      jet.velY += kGravity * dt;
      jet.height += jet.velY * dt;
      jet.life += dt;
      if (jet.height < 0.0f) {
        jet.height = 0.0f;
        jet.velY = 0.0f;
        jet.life = 0.0f;
        SpawnRing(i, jet.center.x, jet.center.z, 0.45f);
      }
    }

    TeapotPhysics &p = gPots[i];
    if (!p.active) {
      if (gSimTime >= p.spawnAt) {
        p.active = true;
        float sc = kFleetScale[i];
        p.pos = {kFleetPos[i][0], kFleetDrop[i], kFleetPos[i][1]};
        float ph = (float)((i * 37) % 11) / 11.0f;
        p.vel = {(ph - 0.5f) * 0.9f, -1.2f, ((i * 53) % 7 - 3.0f) / 7.0f * 0.9f};
        p.yawVel = 1.1f * (0.6f + 0.8f * ((i * 7) % 5) / 5.0f);
        p.pitch = 0.15f * ((i % 3) - 1);
        p.pitchVel = 1.7f * (0.5f + 0.9f * ph);
        p.radius = 0.55f * sc;
      }
      continue;
    }
    bool water = p.pos.y < kWaterLevel - 0.12f * p.radius;
    p.vel.y += kGravity * dt;
    if (!water) {
      float ad = 1.0f - std::fmin(0.16f * dt, 0.2f);
      p.vel.x *= ad; p.vel.y *= ad; p.vel.z *= ad;
    }

    if (water) {
      if (!p.inWater) {
        float speed = -p.vel.y;
        p.inWater = true;
        p.splashed = true;
        p.splashTime = now;
        SpawnSplash(i, p.pos.x, p.pos.z, speed * (i >= 9 ? 1.15f : 1.0f),
                    kFleetScale[i]);
        SpawnRing(i, p.pos.x, p.pos.z, 0.40f + 0.3f * std::fmin(speed / 9.0f, 1.0f));
        p.vel.y = speed * kBounce;
        p.vel.x *= 0.4f; p.vel.z *= 0.4f;
        p.yawVel *= 0.25f;
        p.pitchVel *= 0.25f;
      }
      p.vel.y += -0.55f * std::fabs(p.vel.y) * p.vel.y * dt;
      p.vel.y *= 1.0f - std::fmin(4.5f * dt, 0.9f);
      p.vel.x *= 1.0f - std::fmin(kDragWater * dt, 0.9f);
      p.vel.z *= 1.0f - std::fmin(kDragWater * dt, 0.9f);
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
    float restY = kWaterLevel - 0.45f * p.radius;
    if (p.pos.y < restY) {
      p.pos.y = restY;
      if (p.vel.y < 0.0f) p.vel.y = 0.0f;
      p.vel.x = 0.0f;
      p.vel.z = 0.0f;
      p.yawVel = 0.0f;
      p.pitchVel = 0.0f;
      p.settled = true;
    }
    if (p.settled && now - p.splashTime < 2.5 && now >= p.nextBoil) {
      p.nextBoil = now + 0.22;
      float ba = (float)((int)(now * 137.0) % 360) * 0.0174532925f;
      float br = p.radius * (0.2f + 0.5f * (float)((int)(now * 89.0) % 7) / 7.0f);
      SpawnRing(i, p.pos.x + std::cos(ba) * br, p.pos.z + std::sin(ba) * br,
                0.16f + 0.10f * (float)((int)(now * 53.0) % 5) / 5.0f);
    }
  }
}
static void UpdatePhysics(double now, double frameDt) {
  if (frameDt > 0.1) frameDt = 0.1;
  gSimTime += frameDt;
  gPhysicsAccum += frameDt;
  while (gPhysicsAccum >= (double)kPhysicsStep) {
    gPhysicsAccum -= (double)kPhysicsStep;
    UpdatePhysicsStep(now - gPhysicsAccum, kPhysicsStep);
  }
}

static SDL_Window *gWindow = nullptr;
static SDL_GLContext gContext = nullptr;
static int gWindowWidth = WIDTH, gWindowHeight = HEIGHT;

static Program gSkyProg, gWaterProg, gTeapotProg, gDropletProg, gSplashProg, gHudProg;
static GLuint gTeapotTex = 0, gFontTex = 0;
static GLuint gEmptyVao = 0;

static Vec3 gCamPos{0.0f, 2.6f, 7.2f};
static float gCamYaw = 0.0f, gCamPitch = -0.28f;
static bool gAutoCam = true;
static float gCamDist = 7.2f;
static bool gResultsShown = false;
static double gResultsElapsed = 0.0, gResultsFps = 0.0, gResultsScore = 0.0;
static double gResultsShownAt = 0.0;
static const double kResultsScreenSeconds = 4.0;
static const char *gSceneName = "Scene 3";
double gFusedPoolScore = 0.0; 
static bool gFusedDone = false;
static bool gStandaloneScene = false;
static int gFrame = 0, gFps = 0, gFrameAccum = 0;
static double gFpsTimer = 0.0;
static double gSmoothFps = 0.0;
static bool gQuit = false;
static const char *gScreenshotPath = nullptr;
static std::vector<float> gShotTimes;
static size_t gNextShot = 0;
static int gWindowWidthOverride = 0;

static GLuint gHudVao = 0, gHudVbo = 0;
static int gHudVertexFloats = 0;

// ------------------------------------------------------------- camera path
static void UpdateAutoCamera(float t) {
  float a = 0.32f + t * 0.055f;
  float spread = t > 11.0f ? std::fmin((t - 11.0f) * 0.35f, 1.6f) : 0.0f;
  float radius = 7.6f + std::sin(t * 0.07f) * 1.1f + spread + gCamPullback;
  gCamPos.x = std::cos(a) * radius;
  gCamPos.z = std::sin(a) * radius;
  gCamPos.y = 2.9f + std::sin(t * 0.045f) * 0.7f + spread * 0.10f +
              gCamPullback * 0.12f;
  gCamYaw = std::atan2(-gCamPos.x, -gCamPos.z);
  gCamPitch = -0.30f + 0.06f * std::sin(t * 0.03f);
}

static Vec3 OrbitCamPos() {
  float cp = std::cos(gCamPitch), sp = std::sin(gCamPitch);
  Vec3 pos;
  pos.x = gCamPos.x + std::sin(gCamYaw) * cp * gCamDist;
  pos.y = gCamPos.y + sp * gCamDist;
  pos.z = gCamPos.z + std::cos(gCamYaw) * cp * gCamDist;
  if (pos.y < 0.35f) pos.y = 0.35f;
  return pos;
}

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

static Mat4 gProj;

static void DrawSky(const Mat4 &view, const Vec3 &eye, double timeSec) {
  Mat4 vp;
  Mat4Multiply(vp, gProj, view);
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE);
  glUseProgram(gSkyProg.handle);
  glBindVertexArray(gDomeMesh.vao);
  glUniformMatrix4fv(gSkyProg.loc("uViewProj"), 1, GL_FALSE, vp.data());
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
  glUniform4fv(gWaterProg.loc("uRings"), MAX_RINGS, &gRings[0].x);
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
    pos.y = 2.0f * kWaterLevel - pos.y; 
    pos.x += wobble[0];                
    pos.z += wobble[1];                
  }
  Mat4 model;
  Mat4Model(model, pos, pot.yaw, scale, pot.pitch);
  if (reflectionPass) {
    model[4] = -model[4];
    model[5] = -model[5];
    model[6] = -model[6];
    model[7] = -model[7];
  }
  float wetness = pot.splashed ? 1.0f : 0.0f;

  glUseProgram(gTeapotProg.handle);
  glBindVertexArray(gTeapotMesh.vao);
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
  if (!reflectionPass) glEnable(GL_CULL_FACE);
}

static void DrawFleet(const Mat4 &view, const Vec3 &eye, bool reflectionPass,
                      double now = 0.0) {
  const float zero[2] = {0.0f, 0.0f};
  for (int i = 0; i < kFleetCount; i++)
    DrawTeapot(view, eye, gPots[i], kFleetScale[i], reflectionPass,
               reflectionPass ? nullptr : zero, (float)now);
}
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
  glUniform3f(gSplashProg.loc("uWaterA"), 0.30f, 0.62f, 0.86f);
  glUniform3f(gSplashProg.loc("uWaterB"), 0.030f, 0.180f, 0.320f);
  glUniform3f(gSplashProg.loc("uTileA"), kTileA[0], kTileA[1], kTileA[2]);
  glUniform3f(gSplashProg.loc("uTileB"), kTileB[0], kTileB[1], kTileB[2]);
  glUniform1f(gSplashProg.loc("uTime"), (float)now);
  glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
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
  for (int a = 1; a < n; a++) {                 
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
    glUniform1f(gSplashProg.loc("uPhase"), (float)i * 1.7f);
    glUniform1f(gSplashProg.loc("uJet"), 0.0f);
    glDrawElements(GL_TRIANGLES, gCrownVertexCount, GL_UNSIGNED_INT, nullptr);
  }
  for (int k = 0; k < n; k++) {
    const int i = order[k];
    const JetColumn &jet = gJets[i];
    if (!jet.active || jet.height <= 0.02f) continue;
    glUniform3f(gSplashProg.loc("uCenter"), jet.center.x, jet.center.y, jet.center.z);
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
  static std::vector<float> jbuf;
  jbuf.clear();
  for (int p = 0; p < kFleetCount; p++) {
    const JetColumn &jet = gJets[p];
    if (!jet.active || jet.height <= 0.01f) continue;
    Vec3 toCam = Vec3Normalize(Vec3Sub(eye, jet.center));
    float a = std::atan2(toCam.x, toCam.z);
    const float fade = std::fmin(1.0f, 1.3f - jet.life * 0.3f) * 0.50f;
    const float halfW = jet.radius;
    for (int pass = 0; pass < 2; pass++) {
      float aa = a + pass * 1.5707963f;
      (void)aa;
      const int kSteps = 6;
      for (int i = 0; i <= kSteps; i++) {
        float sy = (float)i / kSteps;
        float taper = 1.0f - 0.35f * sy;
        float px = jet.center.x;
        float py = jet.center.y + sy * jet.height;
        float pz = jet.center.z;
        const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        const int quadIdx[6] = {0, 1, 2, 0, 2, 3};
        for (int ci = 0; ci < 6; ci++) {
          const float *c = corners[quadIdx[ci]];
          jbuf.push_back(px); jbuf.push_back(py); jbuf.push_back(pz);
          jbuf.push_back(0.0f); jbuf.push_back(0.0f); jbuf.push_back(0.0f);
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
  static std::vector<float> buf;
  buf.clear();
  for (const Droplet &d : gDroplets) {
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
  if (dt > 0.1) dt = 0.1;
  const double simNow = gSimTime;
  UpdatePhysics(simNow, dt);
  UpdateChoreography(simNow, dt); 

  if (gAutoCam) UpdateAutoCamera((float)gSimTime);  
  Vec3 eye = gAutoCam ? gCamPos : OrbitCamPos();

  Mat4 view;
  {
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
  DrawFleet(view, eye, false, simNow);  
  DrawWater(view, eye, simNow);  
  DrawCrowns(view, simNow);
  DrawJets(view, eye);
  DrawDroplets(view, eye);
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
    gSmoothFps = gSmoothFps > 0.0 ? gSmoothFps * 0.8 + inst * 0.2 : inst;
    gFrameAccum = 0;
    gFpsTimer = now;
    char title[256];
    std::snprintf(title, sizeof(title), "%s - FPS : %d", NAME, gFps);
    SDL_SetWindowTitle(gWindow, title);
  }
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
// ESC is the only control the scene keeps: the camera is fully automatic.
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

  {
    const int kSeg = 96, kRows = 5;
    std::vector<float> ring;
    std::vector<unsigned> ridx;
    for (int r = 0; r <= kRows; r++) {
      float hp = (float)r / kRows;
      for (int s = 0; s <= kSeg; s++) {
        ring.push_back((float)s / kSeg); 
        ring.push_back(hp);              
        ring.push_back(1.0f);            
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
  ResetFleet();

  gStartTime = NowSeconds();
  gFpsTimer = gStartTime;
  gSimTime = 0.0;      
  gPhysicsAccum = 0.0;
  gCamPullback = 0.0f;
  gPhase = 0;
  gPhaseT = 0.0;

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
