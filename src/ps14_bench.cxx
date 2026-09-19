// ElectroBench PS1.4 — a recreation of the 3DMark2001 SE "Nature" pixel
// shader 1.4 workload (sea + sky + clouds + reflections) on OpenGL 3.3 core.
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

// ------------------------------------------------------------------ constants
#define NAME "ElectroBench - PS1.4 Sea (Nature-like)"
#define WIDTH 1366
#define HEIGHT 768
#define BENCH_MILLISECONDS 45000 // 45 s like the original ElectroBench

static const int kSeaResolution = 256;   // vertices per side of the ocean grid
static const float kSeaSize = 1024.0f;   // world size of the ocean patch
static const int kEnvMapSize = 256;      // cubemap face resolution
static const int kNoiseSize = 256;       // fBm noise texture size
static const int kRippleSize = 256;      // ripple gradient texture size
static const int kFoamSize = 256;        // foam texture size

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
  Vec3 u = Vec3Cross(s, f);
  Mat4Identity(m);
  m[0] = s.x;  m[4] = s.y;  m[8] = -f.x;
  m[1] = u.x;  m[5] = u.y;  m[9] = -f.y;
  m[2] = s.z;  m[6] = u.z;  m[10] = -f.z;
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
      float r = TileableFbm(u, v, 5, 4.0f, 101);
      float g = TileableFbm(u, v, 5, 6.0f, 202);
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

// -------------------------------------------------------------- bitmap font
// 8x8 glyphs for the HUD. Each glyph is 8 bytes, MSB = leftmost pixel.
static const unsigned char kFontData[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, // space !
    {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00}, {0x36,0x7F,0x36,0x7F,0x36,0x00,0x00,0x00}, // " #
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}, // $ %
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00}, {0x06,0x06,0x0C,0x00,0x00,0x00,0x00,0x00}, // & '
    {0x0E,0x1C,0x38,0x1C,0x0E,0x00,0x00,0x00}, {0x00,0x1C,0x36,0x36,0x1C,0x00,0x00,0x00}, // ( )
    {0x00,0x36,0x1C,0x7F,0x1C,0x36,0x00,0x00}, {0x00,0x06,0x06,0x3F,0x06,0x06,0x00,0x00}, // * +
    {0x00,0x00,0x00,0x00,0x0E,0x0C,0x38,0x00}, {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00}, // , -
    {0x00,0x00,0x00,0x00,0x0C,0x0C,0x00,0x00}, {0x60,0x30,0x18,0x0C,0x06,0x00,0x00,0x00}, // . /
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00}, {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00}, // 0 1
    {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00}, {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00}, // 2 3
    {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00}, {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00}, // 4 5
    {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00}, {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00}, // 6 7
    {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00}, {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00}, // 8 9
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00}, {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x38}, // : ;
    {0x18,0x30,0x60,0x30,0x18,0x00,0x00,0x00}, {0x00,0x00,0x3F,0x00,0x3F,0x00,0x00,0x00}, // < =
    {0x06,0x03,0x01,0x03,0x06,0x00,0x00,0x00}, {0x00,0x1E,0x33,0x30,0x18,0x00,0x18,0x00}, // > ?
    {0x1E,0x33,0x3F,0x3B,0x3F,0x03,0x1E,0x00}, {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00}, // @ A
    {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00}, {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00}, // B C
    {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00}, {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00}, // D E
    {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00}, {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00}, // F G
    {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00}, {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // H I
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00}, {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00}, // J K
    {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00}, {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00}, // L M
    {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00}, {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00}, // N O
    {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00}, {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00}, // P Q
    {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00}, {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00}, // R S
    {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00}, // T U
    {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00}, {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, // V W
    {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00}, {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00}, // X Y
    {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00}, {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00}, // Z [
    {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}, {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00}, // \ ]
    {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, // ^ _
    {0x0C,0x18,0x30,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00}, // ` a
    {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00}, {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00}, // b c
    {0x38,0x30,0x30,0x3E,0x33,0x33,0x6E,0x00}, {0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00}, // d e
    {0x1C,0x36,0x06,0x0F,0x06,0x06,0x0F,0x00}, {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F}, // f g
    {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00}, {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00}, // h i
    {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E}, {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00}, // j k
    {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00}, // l m
    {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00}, {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00}, // n o
    {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F}, {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F}, // p q
    {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00}, {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00}, // r s
    {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00}, {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00}, // t u
    {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00}, {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00}, // v w
    {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00}, {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F}, // x y
    {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00}, {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00}, // z {
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00}, // | }
    {0x6E,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ~
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // DEL
};

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
  const int seg = 96, rings = 48; // fine tessellation: sun disc + full sphere
  std::vector<float> verts;
  std::vector<unsigned int> idx;
  for (int r = 0; r <= rings; r++) {
    // full sphere 0..pi: the lower hemisphere carries the horizon gradient so
    // the cubemap has no black -Y face (it used to bleed black into grazing
    // reflections, showing as dark spots on the water)
    float phi = (float)r / rings * 3.14159265f;
    for (int s = 0; s <= seg; s++) {
      float th = (float)s / seg * 3.14159265f * 2.0f;
      float cp = std::cos(phi);
      verts.push_back(cp * std::cos(th));
      verts.push_back(std::sin(phi));
      verts.push_back(cp * std::sin(th));
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

static Program gSeaProg, gSkyProg, gSkyViewProg, gHudProg;
static GLuint gRippleTex = 0, gNoiseTex = 0, gFoamTex = 0, gFontTex = 0;
static GLuint gEnvCube = 0, gEnvFbo = 0, gEnvDepth = 0;

static Vec3 gSunDir = {0.87f, 0.12f, 0.47f};   // low sun on the fly-over path: yellow disc + orange glow visible, glitter path towards the camera

static Vec3 gCamPos = {0.0f, 6.0f, 0.0f};
static float gCamYaw = 0.0f, gCamPitch = -0.05f;
static bool gAutoCam = true;

static bool gIsHoldingMouse = false;
static int gXOld = 0, gYOld = 0;
static int gCurrentScroll = 10;

static double gStartTime = 0.0;
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

static const int kHudTexW = 512, kHudTexH = 64;
static GLuint gHudVao = 0, gHudVbo = 0;
static int gHudQuadCount = 0;

// ------------------------------------------------------------- camera path
// A gentle banking fly-over: forward glide plus a slow orbit, like the
// Nature camera drifting over the ocean.
static void UpdateAutoCamera(float t) {
  float a = t * 0.05f;
  float radius = 42.0f + std::sin(t * 0.021f) * 10.0f;
  Vec3 center = {std::cos(a) * radius, 0.0f, std::sin(a) * radius * 0.7f};
  Vec3 eye;
  eye.x = std::cos(a + 0.9f) * radius;
  eye.z = std::sin(a + 0.9f) * radius * 0.7f;
  eye.y = 7.5f + std::sin(t * 0.043f) * 2.2f;
  gCamPos = eye;
  Vec3 target = Vec3Add(center, {0.0f, 1.5f, 0.0f});
  Vec3 fwd = Vec3Normalize(Vec3Sub(target, eye));
  gCamYaw = std::atan2(fwd.x, fwd.z);
  gCamPitch = std::asin(fwd.y) * 0.6f;
}

static Vec3 OrbitCamPos() {
  float cx = gCamPitch * 3.14159265f / 180.0f;
  float cy = gCamYaw;
  float d = (float)gCurrentScroll * 1.6f + 8.0f;
  Vec3 pos;
  pos.x = gCamPos.x + std::sin(cy) * std::cos(cx) * d;
  pos.y = gCamPos.y + std::sin(cx) * d + 3.0f;
  pos.z = gCamPos.z + std::cos(cy) * std::cos(cx) * d;
  if (pos.y < 1.5f) pos.y = 1.5f; // never dive under the waves
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
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
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
    glUniformMatrix4fv(gSkyProg.loc("uViewProj"), 1, GL_FALSE, vp.data());
    glUniform3f(gSkyProg.loc("uSunDir"), gSunDir.x, gSunDir.y, gSunDir.z);
    glUniform1f(gSkyProg.loc("uTime"), (float)NowSeconds());
    glUniform3f(gSkyProg.loc("uZenithColor"), 0.18f, 0.36f, 0.68f);
    glUniform3f(gSkyProg.loc("uHorizonColor"), 0.66f, 0.74f, 0.84f);
    glUniform3f(gSkyProg.loc("uSunColor"), 1.05f, 0.78f, 0.42f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gNoiseTex);
    glUniform1i(gSkyProg.loc("uNoiseTex"), 0);
    glDrawElements(GL_TRIANGLES, gDomeMesh.indexCount, GL_UNSIGNED_INT, nullptr);
  }
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
  // 96 glyphs, 16 columns x 6 rows, 8x8 each, packed in a 128x48 region.
  const int cols = 16, cell = 8;
  int rows = (96 + cols - 1) / cols;
  std::vector<unsigned char> px((size_t)kHudTexW * kHudTexH * 4, 0);
  for (int g = 0; g < 96; g++) {
    int gx = (g % cols) * cell, gy = (g / cols) * cell;
    for (int y = 0; y < 8; y++) {
      unsigned char bits = kFontData[g][y];
      for (int x = 0; x < 8; x++) {
        if (bits & (1u << x)) { // LSB = leftmost column; MSB-first drew mirrored glyphs
          int px_x = gx + x, px_y = gy + y;
          size_t o = ((size_t)px_y * kHudTexW + px_x) * 4;
          px[o] = 255; px[o + 1] = 255; px[o + 2] = 255; px[o + 3] = 255;
        }
      }
    }
  }
  (void)rows;
  glGenTextures(1, &gFontTex);
  glBindTexture(GL_TEXTURE_2D, gFontTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kHudTexW, kHudTexH, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static void RenderText(float x, float y, const char *text) {
  // Append one quad per glyph to a streaming buffer (position xy + uv).
  // Glyph quads are CCW in pixel space; the HUD vertex shader maps that to CW
  // in clip space (y-down pixel -> y-up NDC), so face culling must be off
  // while drawing text or every glyph is discarded.
  static std::vector<float> buf;
  buf.clear();
  float pen = x;
  const float scale = 2.0f; // 8px glyphs -> 16px on screen
  for (const char *p = text; *p; ++p) {
    unsigned char c = (unsigned char)*p;
    if (c < 32 || c > 126) { pen += 8.0f * scale * 0.75f; continue; }
    int g = c - 32;
    int col = g % 16, row = g / 16;
    float u0 = col * 8.0f / (float)kHudTexW;
    float v0 = row * 8.0f / (float)kHudTexH;
    float u1 = (col + 1) * 8.0f / (float)kHudTexW;
    float v1 = (row + 1) * 8.0f / (float)kHudTexH;
    float x0 = pen, y0 = y, x1 = pen + 8.0f * scale, y1 = y + 8.0f * scale;
    // two triangles, CCW in screen space (y down)
    auto push = [&](float px, float py, float u, float v) {
      buf.push_back(px); buf.push_back(py); buf.push_back(u); buf.push_back(v);
    };
    push(x0, y0, u0, v0); push(x1, y0, u1, v0); push(x1, y1, u1, v1);
    push(x0, y0, u0, v0); push(x1, y1, u1, v1); push(x0, y1, u0, v1);
    pen += 8.0f * scale;
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

  gSeaProg = LinkProgram("shaders/ps14/sea_vert.glsl", "shaders/ps14/sea_frag.glsl");
  gSkyProg = LinkProgram("shaders/ps14/sky_vert.glsl", "shaders/ps14/sky_frag.glsl");
  gSkyViewProg = LinkProgram("shaders/ps14/skyview_vert.glsl", "shaders/ps14/skyview_frag.glsl");
  gHudProg = LinkProgram("shaders/ps14/hud_vert.glsl", "shaders/ps14/hud_frag.glsl");

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glClearColor(0.55f, 0.62f, 0.70f, 1.0f);
}

// -------------------------------------------------------------- render passes
static Mat4 gProj;

static void DrawSea(const Mat4 &view, double timeSec, const Vec3 &eye) {
  Mat4 vp;
  Mat4Multiply(vp, gProj, view);
  glUseProgram(gSeaProg.handle);
  glBindVertexArray(gSeaMesh.vao);
  glUniformMatrix4fv(gSeaProg.loc("uViewProj"), 1, GL_FALSE, vp.data());
  glUniform1f(gSeaProg.loc("uTime"), (float)timeSec);
  glUniform3f(gSeaProg.loc("uEyePos"), eye.x, eye.y, eye.z);
  glUniform3f(gSeaProg.loc("uSunDir"), gSunDir.x, gSunDir.y, gSunDir.z);
  glUniform3f(gSeaProg.loc("uHorizonColor"), 0.66f, 0.74f, 0.84f);
  glUniform3f(gSeaProg.loc("uWaterColor"), 0.055f, 0.21f, 0.25f); // deep water must stay legible against the glitter path (near-black body read as "black spots")
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, gRippleTex);
  glUniform1i(gSeaProg.loc("uRippleTex"), 0);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_CUBE_MAP, gEnvCube);
  glUniform1i(gSeaProg.loc("uSkyEnvTex"), 1);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, gFoamTex);
  glUniform1i(gSeaProg.loc("uFoamTex"), 2);
  glDrawElements(GL_TRIANGLES, gSeaMesh.indexCount, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
}

static void DrawSkyView() {
  // Fullscreen sky reconstruction from the cubemap.
  float aspect = (float)gWindowWidth / (float)gWindowHeight;
  float fovRad = 45.0f * 3.14159265f / 180.0f;
  float tanHalf = std::tan(fovRad * 0.5f);

  // Camera basis from yaw/pitch.
  float cy = std::cos(gCamYaw), sy = std::sin(gCamYaw);
  float cp = std::cos(gCamPitch), sp = std::sin(gCamPitch);
  Vec3 fwd = {sy * cp, sp, cy * cp};
  Vec3 worldUp = {0, 1, 0};
  Vec3 right = Vec3Normalize(Vec3Cross(fwd, worldUp));
  Vec3 up = Vec3Cross(right, fwd);

  glDisable(GL_DEPTH_TEST);
  glBindVertexArray(gEmptyVao);
  glUseProgram(gSkyViewProg.handle);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_CUBE_MAP, gEnvCube);
  glUniform1i(gSkyViewProg.loc("uEnvMap"), 0);
  glUniform3f(gSkyViewProg.loc("uCamRight"), right.x, right.y, right.z);
  glUniform3f(gSkyViewProg.loc("uCamUp"), up.x, up.y, up.z);
  glUniform3f(gSkyViewProg.loc("uCamFwd"), fwd.x, fwd.y, fwd.z);
  glUniform1f(gSkyViewProg.loc("uTanHalfFov"), tanHalf);
  glUniform1f(gSkyViewProg.loc("uAspect"), aspect);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);
  glEnable(GL_DEPTH_TEST);
}

static void RenderHUD() {
  char line1[128], line2[128];
  std::snprintf(line1, sizeof(line1), "FPS: %d  SCORE: %.0f", gFps,
                gSmoothFps > 0.0 ? (gSmoothFps * 2.0) / (1.01 / gSmoothFps) : 0.0);
  std::snprintf(line2, sizeof(line2), "3DMARK2001SE PS1.4 - NATURE SEA - OPENGL 3.3");
  RenderText(16.0f, 16.0f, line1);
  RenderText(16.0f, (float)gWindowHeight - 34.0f, line2);
}

// Renders the scene and calculates FPS (same pattern as the original bench).
static void WriteScreenshotPPM(const char *path);

static void RenderScene() {
  double now = NowSeconds();
  float t = (float)(now - gStartTime); // camera time is benchmark-relative so --shot-times are deterministic

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
  Mat4Perspective(gProj, 45.0f, aspect, 0.5f, 2500.0f);

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

  DrawSkyView();
  DrawSea(view, now, eye);    RenderHUD();

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
    double fps = gSmoothFps > 0.0 ? gSmoothFps : (double)gFrame / elapsed;
    double score = (fps * 2.0) / (1.01 / fps);
    std::printf("Benchmark Results - Time : %.1fs, Average FPS : %.1f, Score : %.2f\n",
                elapsed, fps, score);
    std::fflush(stdout);
    SDL_Quit();
    std::exit(0);
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

// ------------------------------------------------------------------- main
int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) {
      gScreenshotPath = argv[++i];
    } else if (!std::strcmp(argv[i], "--shot-times") && i + 1 < argc) {
      if (!ParseShotTimes(argv[++i])) {
        std::fprintf(stderr, "Bad --shot-times list: %s\n", argv[i]);
        return EXIT_FAILURE;
      }
    } else if (!std::strcmp(argv[i], "--width") && i + 1 < argc) {
      gWindowWidthOverride = std::atoi(argv[++i]);
    } else if (!std::strcmp(argv[i], "--dump-env")) {
      gDumpEnv = true;
    } else {
      std::fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
      return EXIT_FAILURE;
    }
  }

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
    std::fprintf(stderr, "OpenGL 3.3 context could not be created! SDL_Error: %s\n", SDL_GetError());
    return EXIT_FAILURE;
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
  while (!gQuit) {
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
  return 0;
}
