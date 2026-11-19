// ElectroBench -- scene 1 of the single ElectroBench binary, rehauled.
//
// "ELECTRO RANGE": a dark hangar at night. The 110 UZIs are the only assets in
// the scene and the only textures it uses: they are laid out in five rings on a
// polished, wet concrete carousel that turns under three colour-gelled light
// bars, with real planar reflections in the slab, a shadow-mapped key light and
// a full PBR material built from the model's six maps (base colour, normal,
// metallic, height, AO, roughness). Everything else -- slab, wall, light bars,
// haze cones -- is procedural geometry and shading.
//
// This translation unit is NOT a program of its own. It exports RunGunScene(),
// which main.cxx calls as the first scene of the one and only ElectroBench
// executable. It creates its own OpenGL 3.3 core context, exactly like scenes
// 2-4 do.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#ifndef SDL_GL_SAMPLES
#define SDL_GL_SAMPLES SDL_GL_MULTISAMPLESAMPLES
#endif

#include "../lib/asset_path.hxx"
#include "post.hxx"

extern bool gWindowedMode;
#include "font_atlas.hxx"

// lodepng.c is pulled in by lib/lodepng.h, which lib/process_image.h includes;
// main.cxx already owns those definitions, so this unit only uses the decoder
// (the same trick src/scene3.cxx uses for its PNG loading).
enum LodePNGColorType { LCT_GREY = 0, LCT_RGB = 2, LCT_PALETTE = 3,
                        LCT_GREY_ALPHA = 4, LCT_RGBA = 6 };
unsigned lodepng_decode_file(unsigned char **out, unsigned *w, unsigned *h,
                             const char *filename, LodePNGColorType colorType,
                             unsigned bitdepth);

// ------------------------------------------------------------------ constants
#define NAME "ElectroBench - Scene 1 (Electro Range)"
#define WIDTH 1366
#define HEIGHT 768
#define BENCH_MILLISECONDS 60000

static const int kShadowSize = 2048;
static const int kGunCount = 110;
static const float kPi = 3.14159265358979f;
static const float kCarouselRadius = 4.40f;

// ------------------------------------------------------------ tiny math utils
struct Vec3 {
  float x, y, z;
};

static inline Vec3 V3(float x, float y, float z) { return {x, y, z}; }
static inline Vec3 V3Add(const Vec3 &a, const Vec3 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static inline Vec3 V3Sub(const Vec3 &a, const Vec3 &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static inline Vec3 V3Scale(const Vec3 &a, float s) { return {a.x * s, a.y * s, a.z * s}; }
static inline float V3Dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3 V3Cross(const Vec3 &a, const Vec3 &b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static inline Vec3 V3Norm(const Vec3 &v) {
  float len = std::sqrt(V3Dot(v, v));
  if (len <= 1e-8f) return {0.0f, 0.0f, 0.0f};
  return {v.x / len, v.y / len, v.z / len};
}
static inline Vec3 V3Lerp(const Vec3 &a, const Vec3 &b, float t) {
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
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
  float f = 1.0f / std::tan(fovYDeg * kPi / 360.0f);
  m[0] = f / aspect;
  m[5] = f;
  m[10] = (zFar + zNear) / (zNear - zFar);
  m[11] = -1.0f;
  m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
}

static void Mat4Ortho(Mat4 &m, float l, float r, float b, float t, float n, float f) {
  m.fill(0.0f);
  m[0] = 2.0f / (r - l);
  m[5] = 2.0f / (t - b);
  m[10] = -2.0f / (f - n);
  m[12] = -(r + l) / (r - l);
  m[13] = -(t + b) / (t - b);
  m[14] = -(f + n) / (f - n);
  m[15] = 1.0f;
}

static void Mat4LookAt(Mat4 &m, const Vec3 &eye, const Vec3 &center, const Vec3 &up) {
  Vec3 f = V3Norm(V3Sub(center, eye));
  Vec3 s = V3Norm(V3Cross(f, up));
  Vec3 u = V3Cross(s, f);
  Mat4Identity(m);
  m[0] = s.x;   m[4] = s.y;   m[8]  = s.z;
  m[1] = u.x;   m[5] = u.y;   m[9]  = u.z;
  m[2] = -f.x;  m[6] = -f.y;  m[10] = -f.z;
  m[12] = -V3Dot(s, eye);
  m[13] = -V3Dot(u, eye);
  m[14] = V3Dot(f, eye);
}

// Translation * rotation about Y * rotation about X * scale.
static void Mat4Model(Mat4 &m, const Vec3 &pos, float yaw, float roll,
                      const Vec3 &scale) {
  float c = std::cos(yaw), s = std::sin(yaw);
  float cr = std::cos(roll), sr = std::sin(roll);
  Mat4Identity(m);
  m[0] = c * scale.x;
  m[4] = s * sr * scale.y;
  m[8] = -s * cr * scale.z;
  m[12] = pos.x;
  m[1] = 0.0f;
  m[5] = cr * scale.y;
  m[9] = sr * scale.z;
  m[13] = pos.y;
  m[2] = s * scale.x;
  m[6] = -c * sr * scale.y;
  m[10] = c * cr * scale.z;
  m[14] = pos.z;
  m[15] = 1.0f;
}

// ------------------------------------------------------------------- helpers
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
  std::vector<std::pair<std::string, GLint>> cache;

  GLint loc(const char *name) {
    for (auto &u : cache)
      if (u.first == name) return u.second;
    GLint l = glGetUniformLocation(handle, name);
    cache.emplace_back(name, l);
    return l;
  }
  void f1(const char *n, float v) { glUniform1f(loc(n), v); }
  void i1(const char *n, int v) { glUniform1i(loc(n), v); }
  void v2(const char *n, float a, float b) { glUniform2f(loc(n), a, b); }
  void v3(const char *n, const Vec3 &v) { glUniform3f(loc(n), v.x, v.y, v.z); }
  void m4(const char *n, const Mat4 &m) { glUniformMatrix4fv(loc(n), 1, GL_FALSE, m.data()); }
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
    std::fprintf(stderr, "Cannot read shaders %s / %s (run from the repo root)\n",
                 vsPath, fsPath);
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

// ---------------------------------------------------------------- textures
static GLuint LoadPngTexture(const char *path, bool srgb) {
  unsigned char *rgba = nullptr;
  unsigned w = 0, h = 0;
  unsigned err = lodepng_decode_file(&rgba, &w, &h, path, LCT_RGBA, 8);
  if (err != 0 || !rgba) {
    std::fprintf(stderr, "Cannot decode texture %s (%u)\n", path, err);
    std::exit(EXIT_FAILURE);
  }
  // The OBJ/UV convention in this repository has v = 0 at the bottom of the
  // image, so the rows are flipped on upload (same as the old scene 1 loader).
  std::vector<unsigned char> px((size_t)w * h * 4);
  for (unsigned y = 0; y < h; y++)
    std::memcpy(&px[(size_t)y * w * 4], &rgba[(size_t)(h - 1 - y) * w * 4],
                (size_t)w * 4);
  std::free(rgba);

  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, (GLsizei)w,
               (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
#ifdef GL_TEXTURE_MAX_ANISOTROPY_EXT
  if (GLEW_EXT_texture_filter_anisotropic) {
    GLfloat maxAniso = 1.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                    maxAniso < 8.0f ? maxAniso : 8.0f);
  }
#endif
  return tex;
}

// ------------------------------------------------------------------- meshes
struct Mesh {
  GLuint vao = 0, vbo = 0;
  int vertexCount = 0;
};

// Interleaved: position(3), normal(3), tangent(4), uv(2) -- 12 floats.
// The vertices are welded per (position, uv, normal) corner, so the mesh has to
// be drawn through the index buffer built alongside them; drawing the array in
// order would connect corners from unrelated triangles.
struct GunMesh {
  GLuint vao = 0, vbo = 0, ebo = 0;
  int vertexCount = 0;
  int indexCount = 0;
  float length = 0.55f;
};

static GunMesh LoadGunMesh(const char *path, float targetLength) {
  FILE *f = std::fopen(path, "rb");
  if (!f) {
    std::fprintf(stderr, "Cannot open OBJ %s\n", path);
    std::exit(EXIT_FAILURE);
  }
  std::vector<Vec3> positions;
  std::vector<float> uvs;   // 2 per entry
  std::vector<Vec3> normals;
  struct Tri { int p[3]; int t[3]; int n[3]; };
  std::vector<Tri> tris;

  char line[1024];
  while (std::fgets(line, sizeof(line), f)) {
    if (line[0] == 'v' && line[1] == ' ') {
      float x, y, z;
      if (std::sscanf(line, "v %f %f %f", &x, &y, &z) == 3) positions.push_back(V3(x, y, z));
    } else if (line[0] == 'v' && line[1] == 't') {
      float u, v;
      if (std::sscanf(line, "vt %f %f", &u, &v) == 2) { uvs.push_back(u); uvs.push_back(v); }
    } else if (line[0] == 'v' && line[1] == 'n') {
      float x, y, z;
      if (std::sscanf(line, "vn %f %f %f", &x, &y, &z) == 3) normals.push_back(V3(x, y, z));
    } else if (line[0] == 'f' && line[1] == ' ') {
      int vi[4] = {0, 0, 0, 0}, ti[4] = {0, 0, 0, 0}, ni[4] = {0, 0, 0, 0};
      int count = 0;
      const char *p = line + 2;
      while (*p && count < 4) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '\n' || *p == '\r') break;
        int a = 0, b = 0, c = 0;
        int got = std::sscanf(p, "%d/%d/%d", &a, &b, &c);
        if (got == 1) got = std::sscanf(p, "%d//%d", &a, &c);
        if (got == 1) { b = 0; c = 0; }
        vi[count] = a - 1;
        ti[count] = b - 1;
        ni[count] = c - 1;
        count++;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
      }
      for (int k = 1; k + 1 < count; k++) {
        Tri t;
        t.p[0] = vi[0]; t.p[1] = vi[k]; t.p[2] = vi[k + 1];
        t.t[0] = ti[0]; t.t[1] = ti[k]; t.t[2] = ti[k + 1];
        t.n[0] = ni[0]; t.n[1] = ni[k]; t.n[2] = ni[k + 1];
        tris.push_back(t);
      }
    }
  }
  std::fclose(f);

  if (positions.empty() || tris.empty()) {
    std::fprintf(stderr, "OBJ %s has no usable geometry\n", path);
    std::exit(EXIT_FAILURE);
  }

  // The model is authored Z-up and centred on its own origin; lay it on the
  // floor with Y up and its long axis along X.
  Vec3 mn = positions[0], mx = positions[0];
  for (const Vec3 &p : positions) {
    mn.x = std::fmin(mn.x, p.x); mn.y = std::fmin(mn.y, p.y); mn.z = std::fmin(mn.z, p.z);
    mx.x = std::fmax(mx.x, p.x); mx.y = std::fmax(mx.y, p.y); mx.z = std::fmax(mx.z, p.z);
  }
  const float cx = (mn.x + mx.x) * 0.5f;
  const float cy = (mn.y + mx.y) * 0.5f;
  const float scale = targetLength / std::fmax(mx.x - mn.x, 1e-5f);

  // The OBJ is Y-up (X = length, Y = height, Z = width). It is only centred and
  // scaled here -- never mirrored: the file's triangles are counter-clockwise
  // around their outward normals, and a mirror would flip that winding and make
  // back-face culling eat the outside of every gun.
  const float cz = (mn.z + mx.z) * 0.5f;
  auto xf = [&](const Vec3 &p) {
    return V3((p.x - cx) * scale, (p.y - mn.y) * scale, (p.z - cz) * scale);
  };
  // The file's triangles wind clockwise around it and its stored normals face
  // into the model, so neither side of the surface is usable as authored:
  // negating the normals and flipping the winding below turns it into an
  // ordinary counter-clockwise, outward-facing mesh.
  auto xfN = [](const Vec3 &n) { return V3Norm(V3(-n.x, -n.y, -n.z)); };

  GunMesh mesh;
  mesh.length = (mx.x - mn.x) * scale;

  std::vector<float> out;
  std::vector<unsigned int> indices;
  std::map<std::tuple<int, int, int>, int> index;
  std::vector<Vec3> tanAccum;
  std::vector<Vec3> bitAccum;
  std::vector<float> handedness;

  for (const Tri &tri : tris) {
    // Flip the winding by swapping the last two corners *before* the corners are
    // evaluated, so the weld key below always describes the vertex it stores.
    Tri c = tri;
    std::swap(c.p[1], c.p[2]);
    std::swap(c.t[1], c.t[2]);
    std::swap(c.n[1], c.n[2]);

    Vec3 p[3], n[3];
    float uv[3][2];
    for (int i = 0; i < 3; i++) {
      p[i] = xf(positions[c.p[i]]);
      n[i] = (c.n[i] >= 0 && c.n[i] < (int)normals.size()) ? xfN(normals[c.n[i]]) : V3(0, 1, 0);
      uv[i][0] = (c.t[i] >= 0) ? uvs[(size_t)c.t[i] * 2 + 0] : 0.0f;
      uv[i][1] = (c.t[i] >= 0) ? uvs[(size_t)c.t[i] * 2 + 1] : 0.0f;
    }

    Vec3 e1 = V3Sub(p[1], p[0]);
    Vec3 e2 = V3Sub(p[2], p[0]);
    float du1 = uv[1][0] - uv[0][0], dv1 = uv[1][1] - uv[0][1];
    float du2 = uv[2][0] - uv[0][0], dv2 = uv[2][1] - uv[0][1];
    float det = du1 * dv2 - du2 * dv1;
    Vec3 t = V3(1.0f, 0.0f, 0.0f), b = V3(0.0f, 0.0f, 1.0f);
    if (std::fabs(det) > 1e-12f) {
      const float r = 1.0f / det;
      t = V3Scale(V3Sub(V3Scale(e1, dv2), V3Scale(e2, dv1)), r);
      b = V3Scale(V3Sub(V3Scale(e2, du1), V3Scale(e1, du2)), r);
    }

    for (int i = 0; i < 3; i++) {
      auto key = std::make_tuple(c.p[i], c.t[i], c.n[i]);
      auto it = index.find(key);
      int idx;
      if (it == index.end()) {
        idx = (int)(out.size() / 12);
        index.emplace(key, idx);
        out.push_back(p[i].x); out.push_back(p[i].y); out.push_back(p[i].z);
        out.push_back(n[i].x); out.push_back(n[i].y); out.push_back(n[i].z);
        out.push_back(t.x); out.push_back(t.y); out.push_back(t.z);
        out.push_back(0.0f); // handedness, patched below
        out.push_back(uv[i][0]); out.push_back(uv[i][1]);
        tanAccum.push_back(V3(0, 0, 0));
        bitAccum.push_back(V3(0, 0, 0));
        handedness.push_back(1.0f);
      } else {
        idx = it->second;
      }
      tanAccum[idx] = V3Add(tanAccum[idx], t);
      bitAccum[idx] = V3Add(bitAccum[idx], b);
      indices.push_back((unsigned int)idx);
    }
  }

  // Orthonormalise the accumulated tangents and store handedness in w.
  for (size_t i = 0; i < tanAccum.size(); i++) {
    Vec3 n = V3((out[i * 12 + 3]), out[i * 12 + 4], out[i * 12 + 5]);
    Vec3 t = V3Sub(tanAccum[i], V3Scale(n, V3Dot(n, tanAccum[i])));
    t = V3Norm(t);
    if (V3Dot(t, t) < 1e-6f) {
      // Degenerate UVs: pick any axis orthogonal to the normal.
      t = V3Norm(V3Cross(n, V3(0.0f, 1.0f, 0.0f)));
      if (V3Dot(t, t) < 1e-6f) t = V3Norm(V3Cross(n, V3(1.0f, 0.0f, 0.0f)));
    }
    float w = V3Dot(V3Cross(n, t), bitAccum[i]) < 0.0f ? -1.0f : 1.0f;
    out[i * 12 + 6] = t.x;
    out[i * 12 + 7] = t.y;
    out[i * 12 + 8] = t.z;
    out[i * 12 + 9] = w;
    (void)handedness;
  }

  mesh.vertexCount = (int)(out.size() / 12);
  mesh.indexCount = (int)indices.size();
  glGenVertexArrays(1, &mesh.vao);
  glGenBuffers(1, &mesh.vbo);
  glGenBuffers(1, &mesh.ebo);
  glBindVertexArray(mesh.vao);
  glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
  glBufferData(GL_ARRAY_BUFFER, out.size() * sizeof(float), out.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int),
               indices.data(), GL_STATIC_DRAW);
  const GLsizei stride = 12 * sizeof(float);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void *)(6 * sizeof(float)));
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, (void *)(10 * sizeof(float)));
  glBindVertexArray(0);
  return mesh;
}

// position(3), normal(3), uv(2) -- 8 floats.
static Mesh UploadMesh(const std::vector<float> &v) {
  Mesh mesh;
  mesh.vertexCount = (int)(v.size() / 8);
  glGenVertexArrays(1, &mesh.vao);
  glGenBuffers(1, &mesh.vbo);
  glBindVertexArray(mesh.vao);
  glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
  glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
  const GLsizei stride = 8 * sizeof(float);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void *)(6 * sizeof(float)));
  glBindVertexArray(0);
  return mesh;
}

static Mesh BuildDisc(float radius, int rings, int segments) {
  std::vector<float> v;
  for (int r = 0; r < rings; r++) {
    float r0 = radius * (float)r / (float)rings;
    float r1 = radius * (float)(r + 1) / (float)rings;
    for (int s = 0; s < segments; s++) {
      float a0 = 2.0f * kPi * (float)s / (float)segments;
      float a1 = 2.0f * kPi * (float)(s + 1) / (float)segments;
      auto push = [&](float x, float z) {
        v.push_back(x); v.push_back(0.0f); v.push_back(z);
        v.push_back(0.0f); v.push_back(1.0f); v.push_back(0.0f);
        v.push_back(x * 0.5f); v.push_back(z * 0.5f);
      };
      float c0 = std::cos(a0), s0 = std::sin(a0);
      float c1 = std::cos(a1), s1 = std::sin(a1);
      // counter-clockwise seen from above, so the slab faces +Y
      push(r0 * c0, r0 * s0);
      push(r1 * c1, r1 * s1);
      push(r1 * c0, r1 * s0);
      push(r0 * c0, r0 * s0);
      push(r0 * c1, r0 * s1);
      push(r1 * c1, r1 * s1);
    }
  }
  return UploadMesh(v);
}

static Mesh BuildCylinderWall(float radius, float height, int segments, int tiers) {
  std::vector<float> v;
  for (int t = 0; t < tiers; t++) {
    float y0 = height * (float)t / (float)tiers;
    float y1 = height * (float)(t + 1) / (float)tiers;
    for (int s = 0; s < segments; s++) {
      float a0 = 2.0f * kPi * (float)s / (float)segments;
      float a1 = 2.0f * kPi * (float)(s + 1) / (float)segments;
      auto push = [&](float a, float y) {
        float c = std::cos(a), si = std::sin(a);
        v.push_back(c * radius); v.push_back(y); v.push_back(si * radius);
        v.push_back(-c); v.push_back(0.0f); v.push_back(-si); // inward
        v.push_back(a / (2.0f * kPi)); v.push_back(y / height);
      };
      // inward-facing winding
      push(a0, y0);
      push(a0, y1);
      push(a1, y1);
      push(a0, y0);
      push(a1, y1);
      push(a1, y0);
    }
  }
  return UploadMesh(v);
}

// Unit cone: radius 1 at y = 0 (floor), radius 0.06 at y = 1 (the bar).
static Mesh BuildCone(int segments) {
  std::vector<float> v;
  for (int s = 0; s < segments; s++) {
    float a0 = 2.0f * kPi * (float)s / (float)segments;
    float a1 = 2.0f * kPi * (float)(s + 1) / (float)segments;
    auto push = [&](float a, float y, float rad, float uvx, float uvy) {
      float c = std::cos(a), si = std::sin(a);
      v.push_back(c * rad); v.push_back(y); v.push_back(si * rad);
      v.push_back(c); v.push_back(0.0f); v.push_back(si);
      v.push_back(uvx); v.push_back(uvy);
    };
    float u0 = (float)s / (float)segments;
    float u1 = (float)(s + 1) / (float)segments;
    push(a0, 0.0f, 1.0f, u0, 1.0f);
    push(a0, 1.0f, 0.06f, u0, 0.0f);
    push(a1, 1.0f, 0.06f, u1, 0.0f);
    push(a0, 0.0f, 1.0f, u0, 1.0f);
    push(a1, 1.0f, 0.06f, u1, 0.0f);
    push(a1, 0.0f, 1.0f, u1, 1.0f);
  }
  return UploadMesh(v);
}

static Mesh BuildBox() {
  std::vector<float> v;
  auto quad = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 n) {
    Vec3 pts[4] = {a, b, c, d};
    float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    int order[6] = {0, 1, 2, 0, 2, 3};
    for (int i : order) {
      v.push_back(pts[i].x); v.push_back(pts[i].y); v.push_back(pts[i].z);
      v.push_back(n.x); v.push_back(n.y); v.push_back(n.z);
      v.push_back(uv[i][0]); v.push_back(uv[i][1]);
    }
  };
  const float h = 0.5f;
  quad(V3(-h, h, -h), V3(-h, h, h), V3(h, h, h), V3(h, h, -h), V3(0, 1, 0));
  quad(V3(-h, -h, -h), V3(h, -h, -h), V3(h, -h, h), V3(-h, -h, h), V3(0, -1, 0));
  quad(V3(-h, -h, h), V3(h, -h, h), V3(h, h, h), V3(-h, h, h), V3(0, 0, 1));
  quad(V3(-h, -h, -h), V3(-h, h, -h), V3(h, h, -h), V3(h, -h, -h), V3(0, 0, -1));
  quad(V3(h, -h, -h), V3(h, h, -h), V3(h, h, h), V3(h, -h, h), V3(1, 0, 0));
  quad(V3(-h, -h, -h), V3(-h, -h, h), V3(-h, h, h), V3(-h, h, -h), V3(-1, 0, 0));
  return UploadMesh(v);
}

// ------------------------------------------------------------------- state
static SDL_Window *gWindow = nullptr;
static SDL_GLContext gContext = nullptr;
static int gWindowWidth = WIDTH, gWindowHeight = HEIGHT;
static int gWindowWidthOverride = 0;
static float gSimStart = 0.0f; // --start-time: jump the clock for still captures

static postproc::Pipeline gPost;
static postproc::Settings gPostSettings;

static Program gGunProg, gFloorProg, gWallProg, gBeamProg, gEmissiveProg,
    gShadowProg, gHudProg;

static GunMesh gGun;
static Mesh gFloor, gWall, gCone, gBar;
static GLuint gTexBase = 0, gTexNormal = 0, gTexMetal = 0, gTexHeight = 0,
              gTexAO = 0, gTexRough = 0;
static GLuint gFontTex = 0;
static GLuint gShadowFbo = 0, gShadowTex = 0;
static GLuint gReflectFbo = 0, gReflectTex = 0, gReflectDepth = 0;
static int gReflectW = 0, gReflectH = 0;

static GLuint gHudVao = 0, gHudVbo = 0;
static int gHudVertexFloats = 0;

struct GunInstance {
  float angle;   // base angle on the carousel
  float radius;
  float yawOffset;
};

static std::vector<GunInstance> gGuns;

struct CamState {
  Vec3 eye;
  Vec3 target;
  float fov;
  Mat4 view, proj, viewProj;
};

static CamState gCam;

struct Frame {
  float t;
  Vec3 keyDir;
  Vec3 keyColor;
  Vec3 rimPos, rimColor;
  Vec3 fillPos, fillColor;
  Vec3 barPos[3], barColor[3];
  float barAngle[3], barRadius[3], barHeight[3];
  float barPosFlat[9];
  float barColorFlat[9];
  Vec3 skyAmb, groundAmb, glowAmb;
  Mat4 lightVP;
};

static Frame gFrameState;

// ----------------------------------------------------------------- run state
static double gSimTime = 0.0;
static double gStartTime = 0.0, gFpsTimer = 0.0, gSmoothFps = 0.0;
static int gFrameCount = 0, gFps = 0, gFrameAccum = 0;
static bool gQuit = false, gFusedDone = false, gStandaloneScene = false;
static const char *gScreenshotPath = nullptr;
static std::vector<float> gShotTimes;
static size_t gNextShot = 0;
static bool gResultsShown = false;
static double gResultsElapsed = 0.0, gResultsFps = 0.0, gResultsScore = 0.0;
static double gResultsShownAt = 0.0;
static const double kResultsScreenSeconds = 4.0;
double gFusedOgScore = 0.0; // read by main.cxx for the combined results screen

static void RenderHUD();
static void RenderResults();
static void WriteScreenshotPPM(const char *path);

// camera keyframes: a low macro glide, a rising orbit, a high reveal, then a
// dive back down onto the rings. The carousel keeps turning underneath.
struct CamKey {
  float t;
  Vec3 eye;
  Vec3 target;
  float fov;
};

static const CamKey kCamKeys[] = {
    {0.0f,  {5.60f, 1.90f, 3.85f},   {0.00f, 0.28f, 0.00f},   42.0f},
    {7.0f,  {3.60f, 1.05f, 2.55f},   {0.00f, 0.20f, 0.00f},   38.0f},
    {14.0f, {2.10f, 1.30f, 1.75f},   {0.00f, 0.30f, 0.20f},   34.0f},
    {21.0f, {-2.95f, 0.95f, -1.95f}, {0.00f, 0.28f, 0.00f},   34.0f},
    {31.0f, {0.40f, 5.10f, 5.90f},   {0.00f, 0.00f, 0.00f},   46.0f},
    {42.0f, {-5.40f, 2.30f, 2.55f},  {0.00f, 0.05f, 0.00f},   43.0f},
    {51.0f, {2.45f, 0.95f, 2.05f},   {0.00f, 0.34f, 0.00f},   36.0f},
    {60.0f, {3.70f, 1.75f, 3.10f},   {0.00f, 0.22f, 0.00f},   41.0f},
};

static float SmoothStep01(float u) {
  if (u < 0.0f) u = 0.0f;
  if (u > 1.0f) u = 1.0f;
  return u * u * u * (u * (u * 6.0f - 15.0f) + 10.0f);
}

static void EvalCamera(float t, Vec3 &eye, Vec3 &target, float &fov) {
  const int n = (int)(sizeof(kCamKeys) / sizeof(kCamKeys[0]));
  if (t <= kCamKeys[0].t) {
    eye = kCamKeys[0].eye;
    target = kCamKeys[0].target;
    fov = kCamKeys[0].fov;
    return;
  }
  if (t >= kCamKeys[n - 1].t) {
    eye = kCamKeys[n - 1].eye;
    target = kCamKeys[n - 1].target;
    fov = kCamKeys[n - 1].fov;
    return;
  }
  for (int i = 0; i + 1 < n; i++) {
    if (t < kCamKeys[i + 1].t) {
      float u = (t - kCamKeys[i].t) / (kCamKeys[i + 1].t - kCamKeys[i].t);
      u = SmoothStep01(u);
      eye = V3Lerp(kCamKeys[i].eye, kCamKeys[i + 1].eye, u);
      target = V3Lerp(kCamKeys[i].target, kCamKeys[i + 1].target, u);
      fov = kCamKeys[i].fov + (kCamKeys[i + 1].fov - kCamKeys[i].fov) * u;
      return;
    }
  }
}

static void BuildLayout() {
  static const int kRingCounts[5] = {10, 16, 22, 28, 34};
  static const float kRingRadii[5] = {1.20f, 2.00f, 2.80f, 3.60f, 4.40f};
  gGuns.clear();
  for (int r = 0; r < 5; r++) {
    for (int i = 0; i < kRingCounts[r]; i++) {
      float a = 2.0f * kPi * (float)i / (float)kRingCounts[r] +
                (float)r * 0.37f;
      GunInstance g;
      g.angle = a;
      g.radius = kRingRadii[r];
      g.yawOffset = 0.0f;
      gGuns.push_back(g);
    }
  }
  // keep the count honest even if the table above is edited
  while ((int)gGuns.size() > kGunCount) gGuns.pop_back();
}

static void UpdateFrame(float t) {
  gFrameState.t = t;

  float ka = 0.55f + t * 0.055f;
  gFrameState.keyDir = V3Norm(V3(std::cos(ka) * 0.52f, 0.84f, std::sin(ka) * 0.52f));
  gFrameState.keyColor = V3Scale(V3(1.0f, 0.86f, 0.70f), 6.2f);

  float ra = 2.45f + t * 0.085f;
  gFrameState.rimPos = V3(std::cos(ra) * 4.10f, 2.65f, std::sin(ra) * 4.10f);
  gFrameState.rimColor = V3Scale(V3(0.36f, 0.62f, 1.0f), 9.0f);

  float fa = 4.45f - t * 0.070f;
  gFrameState.fillPos = V3(std::cos(fa) * 4.30f, 1.75f, std::sin(fa) * 4.30f);
  gFrameState.fillColor = V3Scale(V3(1.0f, 0.30f, 0.58f), 9.0f);

  static const float kBaseAngle[3] = {0.20f, 2.35f, 4.55f};
  static const float kBaseRadius[3] = {2.45f, 3.10f, 1.95f};
  static const float kBaseHeight[3] = {3.35f, 3.80f, 3.55f};
  static const Vec3 kBaseColor[3] = {{1.00f, 0.52f, 0.22f},
                                     {0.42f, 0.70f, 1.00f},
                                     {0.88f, 0.34f, 0.86f}};
  for (int i = 0; i < 3; i++) {
    float a = kBaseAngle[i] + t * 0.060f;
    gFrameState.barAngle[i] = a;
    gFrameState.barRadius[i] = kBaseRadius[i];
    gFrameState.barHeight[i] = kBaseHeight[i];
    gFrameState.barPos[i] = V3(std::cos(a) * kBaseRadius[i], kBaseHeight[i],
                               std::sin(a) * kBaseRadius[i]);
    gFrameState.barColor[i] = kBaseColor[i];
    gFrameState.barPosFlat[i * 3 + 0] = gFrameState.barPos[i].x;
    gFrameState.barPosFlat[i * 3 + 1] = gFrameState.barPos[i].y;
    gFrameState.barPosFlat[i * 3 + 2] = gFrameState.barPos[i].z;
    gFrameState.barColorFlat[i * 3 + 0] = kBaseColor[i].x * 6.0f;
    gFrameState.barColorFlat[i * 3 + 1] = kBaseColor[i].y * 6.0f;
    gFrameState.barColorFlat[i * 3 + 2] = kBaseColor[i].z * 6.0f;
  }

  gFrameState.skyAmb = V3(0.115f, 0.135f, 0.190f);
  gFrameState.groundAmb = V3(0.105f, 0.080f, 0.062f);
  gFrameState.glowAmb = V3(0.52f, 0.26f, 0.120f);

  Mat4 proj, view;
  Vec3 eye = V3Add(gFrameState.keyDir, V3(0.01f, 0.02f, 0.0f));
  Mat4Ortho(proj, -6.0f, 6.0f, -6.0f, 6.0f, 1.0f, 26.0f);
  Mat4LookAt(view, V3Scale(eye, 9.0f), V3(0.0f, 0.0f, 0.0f), V3(0, 1, 0));
  Mat4Multiply(gFrameState.lightVP, proj, view);
}

static void UploadFrameUniforms(Program &p) {
  p.v3("uEye", gCam.eye);
  p.v3("uKeyDir", gFrameState.keyDir);
  p.v3("uKeyColor", gFrameState.keyColor);
  p.v3("uRimPos", gFrameState.rimPos);
  p.v3("uRimColor", gFrameState.rimColor);
  p.v3("uFillPos", gFrameState.fillPos);
  p.v3("uFillColor", gFrameState.fillColor);
  p.v3("uSkyAmb", gFrameState.skyAmb);
  p.v3("uGroundAmb", gFrameState.groundAmb);
  p.v3("uGlowAmb", gFrameState.glowAmb);
  p.m4("uLightVP", gFrameState.lightVP);
  p.v2("uShadowTexel", 1.0f / (float)kShadowSize, 1.0f / (float)kShadowSize);
  p.f1("uTime", gFrameState.t);
  glUniform3fv(p.loc("uBarPos"), 3, gFrameState.barPosFlat);
  glUniform3fv(p.loc("uBarColor"), 3, gFrameState.barColorFlat);
}

// --------------------------------------------------------------- render bits
static void DrawGuns(const Mat4 &viewProj, bool reflection) {
  float carousel = gFrameState.t * 0.045f;
  for (const GunInstance &g : gGuns) {
    float a = g.angle + carousel + g.yawOffset;
    Vec3 pos = V3(std::cos(a) * g.radius, 0.0015f, std::sin(a) * g.radius);
    Mat4 model;
    Mat4Model(model, pos, kPi - a, 0.0f, V3(1, 1, 1));
    glUseProgram(gGunProg.handle);
    gGunProg.m4("uModel", model);
    gGunProg.m4("uViewProj", viewProj);
    (void)reflection;
    glBindVertexArray(gGun.vao);
    glDrawElements(GL_TRIANGLES, gGun.indexCount, GL_UNSIGNED_INT, (void *)0);
  }
  glBindVertexArray(0);
}

static void DrawBars(const Mat4 &viewProj, bool emissive, bool reflection) {
  // the bar box has to be bound here: the caller is left with whatever mesh it
  // drew last, and drawing the bar's vertex count from that mesh sprayed stray
  // triangles across the frame
  glBindVertexArray(gBar.vao);
  for (int i = 0; i < 3; i++) {
    float len = (i == 0) ? 5.2f : (i == 1 ? 4.4f : 4.0f);
    Mat4 model;
    Mat4Model(model, gFrameState.barPos[i], gFrameState.barAngle[i] + kPi * 0.5f,
              0.0f, V3(len, 0.055f, 0.105f));
    glUseProgram(emissive ? gEmissiveProg.handle : gWallProg.handle);
    if (emissive) {
      gEmissiveProg.m4("uModel", model);
      gEmissiveProg.m4("uViewProj", viewProj);
      gEmissiveProg.v3("uColor", gFrameState.barColor[i]);
      gEmissiveProg.v3("uEye", gCam.eye);
      gEmissiveProg.f1("uIntensity", 7.0f);
      glDrawArrays(GL_TRIANGLES, 0, gBar.vertexCount);
    }
    (void)reflection;
  }
  glBindVertexArray(0);
}

static void DrawBeams(const Mat4 &viewProj) {
  glUseProgram(gBeamProg.handle);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE);
  glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE);
  for (int i = 0; i < 3; i++) {
    Mat4 model;
    Mat4Model(model, V3(gFrameState.barPos[i].x, 0.0f, gFrameState.barPos[i].z),
              0.0f, 0.0f,
              V3(2.20f, gFrameState.barHeight[i], 2.20f));
    gBeamProg.m4("uModel", model);
    gBeamProg.m4("uViewProj", viewProj);
    gBeamProg.v3("uColor", gFrameState.barColor[i]);
    gBeamProg.v3("uEye", gCam.eye);
    gBeamProg.f1("uIntensity", 0.035f);
    gBeamProg.f1("uTime", gFrameState.t);
    glBindVertexArray(gCone.vao);
    glDrawArrays(GL_TRIANGLES, 0, gCone.vertexCount);
  }
  glBindVertexArray(0);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
}

// ------------------------------------------------------------------ targets
static void CreateShadowTarget() {
  glGenTextures(1, &gShadowTex);
  glBindTexture(GL_TEXTURE_2D, gShadowTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kShadowSize, kShadowSize,
               0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glGenFramebuffers(1, &gShadowFbo);
  glBindFramebuffer(GL_FRAMEBUFFER, gShadowFbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                         gShadowTex, 0);
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    std::fprintf(stderr, "Scene 1: shadow framebuffer incomplete\n");
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void DestroyReflectionTarget() {
  if (gReflectFbo) glDeleteFramebuffers(1, &gReflectFbo);
  if (gReflectTex) glDeleteTextures(1, &gReflectTex);
  if (gReflectDepth) glDeleteRenderbuffers(1, &gReflectDepth);
  gReflectFbo = gReflectTex = gReflectDepth = 0;
  gReflectW = gReflectH = 0;
}

static void CreateReflectionTarget(int w, int h) {
  DestroyReflectionTarget();
  gReflectW = w > 1 ? w : 1;
  gReflectH = h > 1 ? h : 1;

  glGenTextures(1, &gReflectTex);
  glBindTexture(GL_TEXTURE_2D, gReflectTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, gReflectW, gReflectH, 0, GL_RGBA,
               GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glGenRenderbuffers(1, &gReflectDepth);
  glBindRenderbuffer(GL_RENDERBUFFER, gReflectDepth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, gReflectW, gReflectH);

  glGenFramebuffers(1, &gReflectFbo);
  glBindFramebuffer(GL_FRAMEBUFFER, gReflectFbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         gReflectTex, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                            gReflectDepth);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    std::fprintf(stderr, "Scene 1: reflection framebuffer incomplete\n");
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ------------------------------------------------------------------- setup
static void BuildFontAtlas() {
  std::vector<unsigned char> px((size_t)kFontAtlasW * kFontAtlasH * 4);
  FontAtlasFillRGBA(px.data(), px.size());
  glGenTextures(1, &gFontTex);
  glBindTexture(GL_TEXTURE_2D, gFontTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kFontAtlasW, kFontAtlasH, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, px.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static void Setup() {
  BuildLayout();
  BuildFontAtlas();

  gGun = LoadGunMesh(resolveAssetPath("assets/UZI.obj").c_str(), 0.55f);
  gFloor = BuildDisc(40.0f, 40, 192);
  gWall = BuildCylinderWall(26.0f, 18.0f, 192, 10);
  gCone = BuildCone(64);
  gBar = BuildBox();

  gTexBase = LoadPngTexture(resolveAssetPath("assets/UZI_BaseColor.png").c_str(), true);
  gTexNormal = LoadPngTexture(resolveAssetPath("assets/UZI_Normal.png").c_str(), false);
  gTexMetal = LoadPngTexture(resolveAssetPath("assets/UZI_Metallic.png").c_str(), false);
  gTexHeight = LoadPngTexture(resolveAssetPath("assets/UZI_Height.png").c_str(), false);
  gTexAO = LoadPngTexture(resolveAssetPath("assets/UZI_AO.png").c_str(), false);
  gTexRough = LoadPngTexture(resolveAssetPath("assets/UZI_Roughness.png").c_str(), false);

  glGenVertexArrays(1, &gHudVao);
  glGenBuffers(1, &gHudVbo);
  glBindVertexArray(gHudVao);
  glBindBuffer(GL_ARRAY_BUFFER, gHudVbo);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        (void *)(2 * sizeof(float)));
  glBindVertexArray(0);

  gGunProg = LinkProgram(resolveAssetPath("shaders/range/uzi_vert.glsl").c_str(),
                         resolveAssetPath("shaders/range/uzi_frag.glsl").c_str());
  gFloorProg = LinkProgram(resolveAssetPath("shaders/range/mesh_vert.glsl").c_str(),
                           resolveAssetPath("shaders/range/floor_frag.glsl").c_str());
  gWallProg = LinkProgram(resolveAssetPath("shaders/range/mesh_vert.glsl").c_str(),
                          resolveAssetPath("shaders/range/wall_frag.glsl").c_str());
  gBeamProg = LinkProgram(resolveAssetPath("shaders/range/mesh_vert.glsl").c_str(),
                          resolveAssetPath("shaders/range/beam_frag.glsl").c_str());
  gEmissiveProg = LinkProgram(resolveAssetPath("shaders/range/mesh_vert.glsl").c_str(),
                              resolveAssetPath("shaders/range/emissive_frag.glsl").c_str());
  gShadowProg = LinkProgram(resolveAssetPath("shaders/range/shadow_vert.glsl").c_str(),
                            resolveAssetPath("shaders/range/shadow_frag.glsl").c_str());
  gHudProg = LinkProgram(resolveAssetPath("shaders/ps14/hud_vert.glsl").c_str(),
                         resolveAssetPath("shaders/pool/hud_frag.glsl").c_str());

  CreateShadowTarget();
  CreateReflectionTarget(gWindowWidth / 2, gWindowHeight / 2);

  // texture units: 0-5 the UZI maps, 6 the shadow map, 7 the reflection
  glUseProgram(gGunProg.handle);
  glUniform1i(gGunProg.loc("uBaseColor"), 0);
  glUniform1i(gGunProg.loc("uNormalMap"), 1);
  glUniform1i(gGunProg.loc("uMetallicMap"), 2);
  glUniform1i(gGunProg.loc("uHeightMap"), 3);
  glUniform1i(gGunProg.loc("uAOMap"), 4);
  glUniform1i(gGunProg.loc("uRoughnessMap"), 5);
  glUniform1i(gGunProg.loc("uShadowMap"), 6);
  gGunProg.f1("uParallax", 0.0025f);
  glUniform1i(gGunProg.loc("uReflect"), 0);

  glUseProgram(gFloorProg.handle);
  glUniform1i(gFloorProg.loc("uReflectTex"), 7);
  glUniform1i(gFloorProg.loc("uShadowMap"), 6);
  glUseProgram(0);

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glClearColor(0.006f, 0.007f, 0.010f, 1.0f);

  gPostSettings.exposure = 1.12f;
  gPostSettings.bloom = 0.040f;
  gPostSettings.threshold = 1.55f;
  gPostSettings.knee = 0.55f;
  gPostSettings.aberration = 0.0080f;
  gPostSettings.vignette = 0.30f;
  gPostSettings.grain = 0.020f;
  gPostSettings.flare = 0.9f;
  if (postproc::Init(gPost, gWindowWidth, gWindowHeight, 4)) {
    std::printf("Camera: HDR RGBA16F, %dx MSAA, %d-level bloom, ACES composite\n",
                gPost.samples, postproc::Pipeline::kLevels);
  } else {
    std::printf("Camera: HDR target unavailable - drawing the in-shader response\n");
  }
  std::fflush(stdout);
}

// -------------------------------------------------------------------- frame
static void UpdateCamera(float t) {
  Vec3 eye, target;
  float fov;
  EvalCamera(t, eye, target, fov);

  // a little hand-held drift so the dolly never feels keyframed
  eye.x += 0.006f * std::sin(t * 0.83f);
  eye.y += 0.004f * std::sin(t * 1.27f + 1.1f);
  target.x += 0.004f * std::sin(t * 0.61f + 2.0f);

  gCam.eye = eye;
  gCam.target = target;
  gCam.fov = fov;

  float aspect = (float)gWindowWidth / (float)gWindowHeight;
  Mat4LookAt(gCam.view, eye, target, V3(0, 1, 0));
  Mat4Perspective(gCam.proj, fov, aspect, 0.05f, 140.0f);
  Mat4Multiply(gCam.viewProj, gCam.proj, gCam.view);
}

// The HUD and the post pipeline both leave their own textures on units 0/1,
// so every pass that samples the gun maps binds them itself.
static void BindGunTextures() {
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, gTexBase);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, gTexNormal);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, gTexMetal);
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, gTexHeight);
  glActiveTexture(GL_TEXTURE4);
  glBindTexture(GL_TEXTURE_2D, gTexAO);
  glActiveTexture(GL_TEXTURE5);
  glBindTexture(GL_TEXTURE_2D, gTexRough);
  glActiveTexture(GL_TEXTURE0);
}

static void RenderShadowMap() {
  glBindFramebuffer(GL_FRAMEBUFFER, gShadowFbo);
  glViewport(0, 0, kShadowSize, kShadowSize);
  glClear(GL_DEPTH_BUFFER_BIT);
  glUseProgram(gShadowProg.handle);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_FRONT);
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(2.0f, 4.0f);

  float carousel = gFrameState.t * 0.045f;
  for (const GunInstance &g : gGuns) {
    float a = g.angle + carousel + g.yawOffset;
    Vec3 pos = V3(std::cos(a) * g.radius, 0.0015f, std::sin(a) * g.radius);
    Mat4 model;
    Mat4Model(model, pos, kPi - a, 0.0f, V3(1, 1, 1));
    gShadowProg.m4("uModel", model);
    gShadowProg.m4("uLightVP", gFrameState.lightVP);
    glBindVertexArray(gGun.vao);
    glDrawElements(GL_TRIANGLES, gGun.indexCount, GL_UNSIGNED_INT, (void *)0);
  }
  glBindVertexArray(0);

  glDisable(GL_POLYGON_OFFSET_FILL);
  glCullFace(GL_BACK);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void RenderReflection() {
  Mat4 mirror;
  Mat4Identity(mirror);
  mirror[5] = -1.0f; // reflect about y = 0
  Mat4 reflectView, reflectVP;
  Mat4Multiply(reflectView, gCam.view, mirror);
  Mat4Multiply(reflectVP, gCam.proj, reflectView);

  glBindFramebuffer(GL_FRAMEBUFFER, gReflectFbo);
  glViewport(0, 0, gReflectW, gReflectH);
  glClearColor(0.004f, 0.005f, 0.008f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glClearColor(0.006f, 0.007f, 0.010f, 1.0f);

  BindGunTextures();

  // mirroring flips the winding
  glFrontFace(GL_CW);

  glUseProgram(gWallProg.handle);
  UploadFrameUniforms(gWallProg);
  gWallProg.f1("uReflectPass", 1.0f);
  Mat4 identity;
  Mat4Identity(identity);
  gWallProg.m4("uModel", identity);
  gWallProg.m4("uViewProj", reflectVP);
  glDisable(GL_CULL_FACE);
  glBindVertexArray(gWall.vao);
  glDrawArrays(GL_TRIANGLES, 0, gWall.vertexCount);

  glUseProgram(gGunProg.handle);
  UploadFrameUniforms(gGunProg);
  gGunProg.i1("uReflect", 1);
  DrawGuns(reflectVP, true);
  gGunProg.i1("uReflect", 0);

  DrawBars(reflectVP, true, true);

  glBindVertexArray(0);
  glEnable(GL_CULL_FACE);
  glFrontFace(GL_CCW);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

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
  gSimTime += dt;

  const float t = (float)gSimTime;
  UpdateCamera(t);
  UpdateFrame(t);

  // ---- shadow map ------------------------------------------------------
  RenderShadowMap();

  // ---- planar reflection ----------------------------------------------
  RenderReflection();

  // ---- main HDR pass ---------------------------------------------------
  const bool hdr = postproc::Begin(gPost, gWindowWidth, gWindowHeight);
  glViewport(0, 0, gWindowWidth, gWindowHeight);
  glClearColor(0.006f, 0.007f, 0.010f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glActiveTexture(GL_TEXTURE6);
  glBindTexture(GL_TEXTURE_2D, gShadowTex);
  glActiveTexture(GL_TEXTURE7);
  glBindTexture(GL_TEXTURE_2D, gReflectTex);
  BindGunTextures();

  // wall
  glUseProgram(gWallProg.handle);
  UploadFrameUniforms(gWallProg);
  gWallProg.f1("uReflectPass", 0.0f);
  {
    Mat4 identity;
    Mat4Identity(identity);
    gWallProg.m4("uModel", identity);
    gWallProg.m4("uViewProj", gCam.viewProj);
  }
  glDisable(GL_CULL_FACE);
  glBindVertexArray(gWall.vao);
  glDrawArrays(GL_TRIANGLES, 0, gWall.vertexCount);
  glEnable(GL_CULL_FACE);

  // slab
  glUseProgram(gFloorProg.handle);
  UploadFrameUniforms(gFloorProg);
  {
    Mat4 identity;
    Mat4Identity(identity);
    gFloorProg.m4("uModel", identity);
    gFloorProg.m4("uViewProj", gCam.viewProj);
  }
  Mat4 mirror;
  Mat4Identity(mirror);
  mirror[5] = -1.0f;
  Mat4 reflectView, reflectVP;
  Mat4Multiply(reflectView, gCam.view, mirror);
  Mat4Multiply(reflectVP, gCam.proj, reflectView);
  gFloorProg.m4("uReflectVP", reflectVP);
  glBindVertexArray(gFloor.vao);
  glDrawArrays(GL_TRIANGLES, 0, gFloor.vertexCount);

  // light bars (emissive)
  glUseProgram(gEmissiveProg.handle);
  UploadFrameUniforms(gEmissiveProg);
  DrawBars(gCam.viewProj, true, false);

  // the guns
  glUseProgram(gGunProg.handle);
  UploadFrameUniforms(gGunProg);
  DrawGuns(gCam.viewProj, false);

  // haze cones last: additive, no depth write
  DrawBeams(gCam.viewProj);

  glBindVertexArray(0);
  if (hdr) postproc::End(gPost, gPostSettings, t);
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

  gFrameCount++;
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
    double fps = (double)gFrameCount / elapsed;
    double score = fps * fps * 2.0;
    std::printf("Benchmark Results - Time : %.1fs, Average FPS : %.1f, Score : %.0f\n",
                elapsed, fps, score);
    std::fflush(stdout);
    gFusedOgScore = score;
    gResultsElapsed = elapsed;
    gResultsFps = fps;
    gResultsScore = score;
    gResultsShownAt = now;
    gResultsShown = true;
  }
}
// ------------------------------------------------------------------ HUD text
static float TextWidth(const char *text, float scale) {
  float w = 0.0f;
  for (const char *p = text; *p; ++p)
    w += (float)kFontAtlasCell * scale *
         (FontAtlasHasGlyph((unsigned char)*p) ? 1.0f : 0.75f);
  return w;
}

static void RenderText(float x, float y, const char *text, float scale) {
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
    const float gs = (float)kFontAtlasCell * scale;
    float x0 = pen, y0 = y, x1 = pen + gs, y1 = y + gs;
    auto push = [&](float px, float py, float u, float v) {
      buf.push_back(px); buf.push_back(py); buf.push_back(u); buf.push_back(v);
    };
    push(x0, y0, uv[0], uv[1]); push(x1, y0, uv[2], uv[1]); push(x1, y1, uv[2], uv[3]);
    push(x0, y0, uv[0], uv[1]); push(x1, y1, uv[2], uv[3]); push(x0, y1, uv[0], uv[3]);
    pen += gs;
  }
  gHudVertexFloats = (int)buf.size();
  if (!gHudVertexFloats) return;
  glBindBuffer(GL_ARRAY_BUFFER, gHudVbo);
  glBufferData(GL_ARRAY_BUFFER, buf.size() * sizeof(float), buf.data(),
               GL_DYNAMIC_DRAW);
  glUseProgram(gHudProg.handle);
  gHudProg.v2("uResolution", (float)gWindowWidth, (float)gWindowHeight);
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
  char line[128];
  std::snprintf(line, sizeof(line), "FPS : %d   SCENE 1   ELECTRO RANGE", gFps);
  RenderText(16.0f, 14.0f, line, 2.0f);
}

static void RenderResults() {
  glViewport(0, 0, gWindowWidth, gWindowHeight);
  glClearColor(0.012f, 0.012f, 0.022f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  char big[96], timeLine[128], hint[96];
  std::snprintf(big, sizeof(big), "SCORE : %.0f", gResultsScore);
  std::snprintf(timeLine, sizeof(timeLine),
                "Time : %.1fs   Average FPS : %.1f", gResultsElapsed, gResultsFps);
  std::snprintf(hint, sizeof(hint), "Scene 1 - Electro Range");

  const float cx = 0.5f * (float)gWindowWidth;
  const float cy = 0.5f * (float)gWindowHeight;
  RenderText(cx - TextWidth(big, 6.0f) * 0.5f, cy - 40.0f, big, 6.0f);
  RenderText(cx - TextWidth(timeLine, 2.0f) * 0.5f, cy + 28.0f, timeLine, 2.0f);
  RenderText(cx - TextWidth(hint, 2.0f) * 0.5f, cy + 58.0f, hint, 2.0f);
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

int GunSceneParseArgs(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--shot-times") && i + 1 < argc) {
      if (!ParseShotTimes(argv[++i])) {
        std::fprintf(stderr, "Bad --shot-times list: %s\n", argv[i]);
        return EXIT_FAILURE;
      }
    } else if (!std::strcmp(argv[i], "--width") && i + 1 < argc) {
      gWindowWidthOverride = std::atoi(argv[++i]);
    } else if (!std::strcmp(argv[i], "--start-time") && i + 1 < argc) {
      gSimStart = (float)std::atof(argv[++i]);
    }
  }
  return EXIT_SUCCESS;
}

void GunSceneSetScreenshot(const char *path) { gScreenshotPath = path; }
void GunSceneSetShotTime(float t) {
  if (gShotTimes.empty()) gShotTimes.push_back(t);
}
void GunSceneSetStandalone(bool standalone) { gStandaloneScene = standalone; }

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

// ------------------------------------------------------ scene entry point
int RunGunScene(bool *gaveUpOut) {
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
    gWindow = SDL_CreateWindow(NAME, SDL_WINDOWPOS_UNDEFINED,
                               SDL_WINDOWPOS_UNDEFINED, winW, winH,
                               SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN |
                                   SDL_WINDOW_RESIZABLE |
                                   (gWindowedMode ? 0u : SDL_WINDOW_FULLSCREEN_DESKTOP));
    if (!gWindow) {
      std::fprintf(stderr, "Window could not be created! SDL_Error: %s\n",
                   SDL_GetError());
      return EXIT_FAILURE;
    }
    gContext = SDL_GL_CreateContext(gWindow);
    if (gContext) break;
    SDL_DestroyWindow(gWindow);
    gWindow = nullptr;
    msaa = false;
  }
  if (!gContext) {
    std::printf("Electro range scene: OpenGL 3.3 core context unavailable - "
                "skipping this scene\n");
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
  std::printf("Display: %s %dx%d | capture with Win+PrtScr, or --screenshot FILE "
              "(outputs a ppm file)\n",
              gWindowedMode ? "windowed" : "borderless fullscreen", gWindowWidth,
              gWindowHeight);
  std::fflush(stdout);
  SDL_GL_SetSwapInterval(0);

  GLenum glewError = glewInit();
  if (glewError != GLEW_OK && glewError != GLEW_ERROR_NO_GLX_DISPLAY) {
    std::fprintf(stderr, "glewInit failed: %s\n", glewGetErrorString(glewError));
    return EXIT_FAILURE;
  }
  std::printf("Renderer: %s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

  Setup();

  gStartTime = NowSeconds();
  gFpsTimer = gStartTime;
  gSimTime = (double)gSimStart;
  gResultsShown = false;
  gFusedDone = false;
  gQuit = false;
  gFrameCount = 0;
  gFrameAccum = 0;
  gFps = 0;
  gSmoothFps = 0.0;

  SDL_Event event;
  while (!gQuit && !gFusedDone) {
    while (SDL_PollEvent(&event) != 0) {
      if (event.type == SDL_QUIT) {
        gQuit = true;
      } else if (event.type == SDL_KEYDOWN) {
        if (event.key.keysym.sym == SDLK_ESCAPE) gQuit = true;
      } else if (event.type == SDL_WINDOWEVENT) {
        if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
          gWindowWidth = event.window.data1;
          gWindowHeight = event.window.data2;
          if (gWindowHeight < 1) gWindowHeight = 1;
          glViewport(0, 0, gWindowWidth, gWindowHeight);
          CreateReflectionTarget(gWindowWidth / 2, gWindowHeight / 2);
        }
      }
    }
    RenderScene();
  }

  SDL_GL_DeleteContext(gContext);
  SDL_DestroyWindow(gWindow);
  SDL_Quit();
  gWindow = nullptr;
  gContext = nullptr;
  if (gQuit) return 2;
  return 0;
}
