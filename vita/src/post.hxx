// =============================================================================
// ElectroBench -- shared HDR camera for the PS Vita and Android ports.
//
// This is the port of src/post.hxx (the desktop GL 3.3 pipeline) to the GL
// surface vitaGL and GLES 3.0 have in common. The scenes it serves (1 dusk
// ocean, 2 power lines) used to write their own tone-mapped, gamma-encoded
// colour straight into the default framebuffer -- six shaders, each with its
// own response curve (pow(c, 1/2.2), Reinhard-ish col/(col*0.35+0.72), an ACES
// blend in the sky). The same radiance therefore had three different
// brightnesses depending on which pass drew it, nothing above 1.0 survived to
// be graded, and the sun and the sea's glitter path had hard clipped edges.
//
// This module renders the scene into a half-float target, extracts a bloom
// pyramid from the real HDR values (soft-knee threshold, 13-tap downsample,
// tent upsample) and composites once, with the ACES film curve -- one response
// for every pass -- plus the rest of a camera: exposure, lateral chromatic
// aberration, lens ghosts, cos^4 vignette, film grain and dither.
//
#pragma once

#include <GL/glew.h>

#include <cstdio>

namespace postproc {

constexpr int kLevels = 5;

// ------------------------------------------------------------------ shaders
static const char *kFullscreenVert =
    "#version 100\n"
    "attribute vec2 aPos;\n"
    "varying vec2 vUv;\n"
    "void main() {\n"
    "    vUv = aPos * 0.5 + 0.5;\n"
    "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "}\n";

// 13-tap downsample (Jimenez, "Next Generation Post Processing in Call of
// Duty: Advanced Warfare").
static const char *kDownFrag =
    "#version 100\n"
    "precision highp float;\n"
    "varying vec2 vUv;\n"
    "uniform sampler2D uSrc;\n"
    "uniform vec2 uTexel;\n"
    "uniform float uPrefilter;\n"
    "uniform float uThreshold;\n"
    "uniform float uKnee;\n"
    "vec3 tap(vec2 uv) { return texture2D(uSrc, uv).rgb; }\n"
    "void main() {\n"
    "    vec2 t = uTexel;\n"
    "    vec3 a = tap(vUv + t * vec2(-1.0, -1.0));\n"
    "    vec3 b = tap(vUv + t * vec2( 0.0, -1.0));\n"
    "    vec3 c = tap(vUv + t * vec2( 1.0, -1.0));\n"
    "    vec3 d = tap(vUv + t * vec2(-1.0,  0.0));\n"
    "    vec3 e = tap(vUv);\n"
    "    vec3 f = tap(vUv + t * vec2( 1.0,  0.0));\n"
    "    vec3 g = tap(vUv + t * vec2(-1.0,  1.0));\n"
    "    vec3 h = tap(vUv + t * vec2( 0.0,  1.0));\n"
    "    vec3 i = tap(vUv + t * vec2( 1.0,  1.0));\n"
    "    vec3 j = tap(vUv + t * vec2(-0.5, -0.5));\n"
    "    vec3 k = tap(vUv + t * vec2( 0.5, -0.5));\n"
    "    vec3 l = tap(vUv + t * vec2(-0.5,  0.5));\n"
    "    vec3 m = tap(vUv + t * vec2( 0.5,  0.5));\n"
    "    vec3 col = e * 0.125;\n"
    "    col += (a + c + g + i) * 0.03125;\n"
    "    col += (b + d + f + h) * 0.0625;\n"
    "    col += (j + k + l + m) * 0.125;\n"
    "    if (uPrefilter > 0.5) {\n"
    "        // Soft-knee bright pass: below the threshold nothing survives,\n"
    "        // around it the response is quadratic so a slightly dimmer\n"
    "        // highlight does not pop into the bloom as a hard disc, above it\n"
    "        // the excess is kept 1:1 so the sun still blooms with the right\n"
    "        // energy.\n"
    "        float br = max(col.r, max(col.g, col.b));\n"
    "        float knee = max(uKnee, 1e-4);\n"
    "        float soft = clamp(br - uThreshold + knee, 0.0, 2.0 * knee);\n"
    "        soft = soft * soft / (4.0 * knee);\n"
    "        float contrib = max(soft, br - uThreshold) / max(br, 1e-4);\n"
    "        col *= contrib;\n"
    "    }\n"
    "    gl_FragColor = vec4(col, 1.0);\n"
    "}\n";

// 9-tap tent upsample
static const char *kUpFrag =
    "#version 100\n"
    "precision highp float;\n"
    "varying vec2 vUv;\n"
    "uniform sampler2D uSrc;\n"
    "uniform vec2 uTexel;\n"
    "void main() {\n"
    "    vec2 t = uTexel;\n"
    "    vec3 col = texture2D(uSrc, vUv).rgb * 4.0;\n"
    "    col += (texture2D(uSrc, vUv + t * vec2(-1.0, 0.0)).rgb\n"
    "          + texture2D(uSrc, vUv + t * vec2( 1.0, 0.0)).rgb\n"
    "          + texture2D(uSrc, vUv + t * vec2( 0.0,-1.0)).rgb\n"
    "          + texture2D(uSrc, vUv + t * vec2( 0.0, 1.0)).rgb) * 2.0;\n"
    "    col += (texture2D(uSrc, vUv + t * vec2(-1.0,-1.0)).rgb\n"
    "          + texture2D(uSrc, vUv + t * vec2( 1.0,-1.0)).rgb\n"
    "          + texture2D(uSrc, vUv + t * vec2(-1.0, 1.0)).rgb\n"
    "          + texture2D(uSrc, vUv + t * vec2( 1.0, 1.0)).rgb);\n"
    "    gl_FragColor = vec4(col / 16.0, 1.0);\n"
    "}\n";

static const char *kFinalFrag =
    "#version 100\n"
    "precision highp float;\n"
    "varying vec2 vUv;\n"
    "uniform sampler2D uHdr;\n"
    "uniform sampler2D uBloom;\n"
    "uniform float uExposure;\n"
    "uniform float uBloomStrength;\n"
    "uniform float uTime;\n"
    "uniform float uAberration;\n"
    "uniform float uVignette;\n"
    "uniform float uGrain;\n"
    "uniform float uFlare;\n"
    // ACES filmic RRT+ODT fit (Stephen Hill)
    "vec3 RrtOdt(vec3 v) {\n"
    "    vec3 a = v * (v + 0.0245786) - 0.000090537;\n"
    "    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;\n"
    "    return a / b;\n"
    "}\n"
    "vec3 AcesFitted(vec3 c) {\n"
    "    vec3 v = vec3(dot(c, vec3(0.59719, 0.35458, 0.04823)),\n"
    "                  dot(c, vec3(0.07600, 0.90834, 0.01566)),\n"
    "                  dot(c, vec3(0.02840, 0.13383, 0.83777)));\n"
    "    v = RrtOdt(v);\n"
    "    return clamp(vec3(dot(v, vec3( 1.60475, -0.53108, -0.07367)),\n"
    "                      dot(v, vec3(-0.10208,  1.10813, -0.00605)),\n"
    "                      dot(v, vec3(-0.00327, -0.07276,  1.07602))),\n"
    "                 0.0, 1.0);\n"
    "}\n"
    "vec3 EncodeSrgb(vec3 c) {\n"
    "    // Piecewise sRGB transfer, not a 2.2 power: the toe matters for the\n"
    "    // dark end of a dusk scene and it keeps the highlights from skewing\n"
    "    // blue.\n"
    "    vec3 lo = c * 12.92;\n"
    "    vec3 hi = 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;\n"
    "    return mix(hi, lo, step(c, vec3(0.0031308)));\n"
    "}\n"
    "float Hash(vec2 p) {\n"
    "    vec3 q = fract(vec3(p.xyx) * 0.1031);\n"
    "    q += dot(q, q.yzx + 33.33);\n"
    "    return fract((q.x + q.y) * q.z);\n"
    "}\n"
    "void main() {\n"
    "    vec2 uv = vUv;\n"
    // Lateral chromatic aberration: a real lens focuses red and blue at
    // slightly different distances, so the error grows with the square of the
    // radius and is zero at the centre. Sampling the HDR buffer per channel
    // (before the tone curve) keeps it a refraction, not a colour shift. The
    // offset is |dir| * r2 * uAberration of the frame width, so at the corner
    // it is 0.35 * uAberration: 0.009 is 2.2 px at 720p, enough to soften a
    // hard wire edge without ever resolving into three images.
    "    vec2 dir = uv - 0.5;\n"
    "    float r2 = dot(dir, dir);\n"
    "    vec2 off = dir * r2 * uAberration;\n"
    "    vec3 hdr;\n"
    "    hdr.r = texture2D(uHdr, uv + off).r;\n"
    "    hdr.g = texture2D(uHdr, uv).g;\n"
    "    hdr.b = texture2D(uHdr, uv - off).b;\n"
    // Bloom (the scene's own HDR values, thresholded and blurred) and a thin
    // lens-ghost train mirrored through the frame centre, the way a bright
    // sun reflecting between glass surfaces paints faint inverted images.
    "    vec3 bloom = texture2D(uBloom, uv).rgb;\n"
    "    vec3 ghosts = vec3(0.0);\n"
    "    if (uFlare > 0.0) {\n"
    "        vec2 mirror = vec2(0.5) - uv;\n"
    "        ghosts += texture2D(uBloom, uv + mirror * 0.62).rgb * vec3(0.55, 0.75, 1.0);\n"
    "        ghosts += texture2D(uBloom, uv + mirror * 1.08).rgb * vec3(1.00, 0.62, 0.35);\n"
    "        ghosts += texture2D(uBloom, uv + mirror * 1.52).rgb * vec3(0.45, 1.00, 0.70);\n"
    "        ghosts *= uFlare * 0.045;\n"
    "    }\n"
    "    vec3 col = hdr + (bloom * uBloomStrength + ghosts);\n"
    // Exposure is a camera property: one multiplier for the whole frame, so
    // the relative brightness of sun, sky, sea and shadow stays physical.
    "    col *= uExposure;\n"
    "    col = AcesFitted(col);\n"
    // Split tone: the dusk grade (cool shadows, warm highlights) belongs in
    // the one stage every pass goes through.
    "    float tone = dot(col, vec3(0.2126, 0.7152, 0.0722));\n"
    "    vec3 cool = col * vec3(0.945, 0.985, 1.055);\n"
    "    vec3 warm = col * vec3(1.045, 1.008, 0.955);\n"
    "    col = mix(cool, col, smoothstep(0.04, 0.30, tone));\n"
    "    col = mix(col, warm, smoothstep(0.55, 1.00, tone) * 0.55);\n"
    // cos^4-style vignette: natural light falloff towards the corners.
    "    float vig = 1.0 - uVignette * pow(clamp(length(dir) * 1.35, 0.0, 1.0), 2.6);\n"
    "    col *= vig;\n"
    // Grain lives after the tone curve, like film: densest in the midtones
    // and gone in the deep shadows and the specular highlights.
    "    float lum = dot(col, vec3(0.2126, 0.7152, 0.0722));\n"
    "    float grainMask = 4.0 * lum * (1.0 - lum);\n"
    "    float n = Hash(gl_FragCoord.xy + fract(uTime) * 37.0)\n"
    "            + Hash(gl_FragCoord.xy * 1.7 + 11.0) - 1.0;\n"
    "    col += n * uGrain * grainMask;\n"
    "    col = EncodeSrgb(clamp(col, 0.0, 1.0));\n"
    // One LSB of ordered dither: an 8-bit panel showing a smooth dusk
    // gradient bands without it.
    "    col += (Hash(gl_FragCoord.xy * 0.71) - 0.5) / 255.0;\n"
    "    gl_FragColor = vec4(col, 1.0);\n"
    "}\n";

// ----------------------------------------------------------------- pipeline
struct Settings {
  float exposure = 1.0f;
  float bloom = 0.085f;
  float aberration = 0.009f;   // frame-width fraction at the corner
  float vignette = 0.22f;
  float grain = 0.020f;
  float flare = 1.0f;
  float threshold = 1.0f;
  float knee = 0.55f;
};

struct Pipeline {
  bool ready = false;
  int width = 0;
  int height = 0;
  int samples = 1;  // kept for API parity with the desktop pipeline

  GLuint fbo = 0;
  GLuint colorTex = 0;
  GLuint depthRb = 0;

  GLuint bloomTex[kLevels] = {0};
  GLuint bloomFbo[kLevels] = {0};
  int bloomW[kLevels] = {0};
  int bloomH[kLevels] = {0};

  GLuint progDown = 0;
  GLuint progUp = 0;
  GLuint progFinal = 0;

  GLuint vao = 0;
  GLuint vbo = 0;
  GLint downSrc = -1, downTexel = -1, downPrefilter = -1;
  GLint downThreshold = -1, downKnee = -1;
  GLint upSrc = -1, upTexel = -1;
  GLint finHdr = -1, finBloom = -1, finExposure = -1, finBloomStrength = -1;
  GLint finTime = -1, finAberration = -1, finVignette = -1, finGrain = -1;
  GLint finFlare = -1;
};

static inline GLuint CompileStage(GLenum type, const char *src, const char *label) {
  GLuint sh = glCreateShader(type);
  glShaderSource(sh, 1, &src, nullptr);
  glCompileShader(sh);
  GLint ok = GL_FALSE;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[2048];
    GLsizei n = 0;
    glGetShaderInfoLog(sh, (GLsizei)sizeof(log) - 1, &n, log);
    log[n < (GLsizei)sizeof(log) ? n : (GLsizei)sizeof(log) - 1] = 0;
    std::fprintf(stderr, "post: %s shader failed:\n%s\n", label, log);
    glDeleteShader(sh);
    return 0;
  }
  return sh;
}

static inline GLuint BuildProgram(const char *frag, const char *label) {
  GLuint vs = CompileStage(GL_VERTEX_SHADER, kFullscreenVert, "fullscreen vert");
  GLuint fs = CompileStage(GL_FRAGMENT_SHADER, frag, label);
  if (!vs || !fs) return 0;
  GLuint prog = glCreateProgram();
  glAttachShader(prog, vs);
  glAttachShader(prog, fs);
  glBindAttribLocation(prog, 0, "aPos");
  glLinkProgram(prog);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = GL_FALSE;
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[2048];
    GLsizei n = 0;
    glGetProgramInfoLog(prog, (GLsizei)sizeof(log) - 1, &n, log);
    log[n < (GLsizei)sizeof(log) ? n : (GLsizei)sizeof(log) - 1] = 0;
    std::fprintf(stderr, "post: %s program failed:\n%s\n", label, log);
    glDeleteProgram(prog);
    return 0;
  }
  return prog;
}

inline void Destroy(Pipeline &p);

inline void DestroyTargets(Pipeline &p) {
  if (p.depthRb) glDeleteRenderbuffers(1, &p.depthRb);
  if (p.fbo) glDeleteFramebuffers(1, &p.fbo);
  if (p.colorTex) glDeleteTextures(1, &p.colorTex);
  p.depthRb = p.fbo = p.colorTex = 0;
  for (int i = 0; i < kLevels; i++) {
    if (p.bloomTex[i]) glDeleteTextures(1, &p.bloomTex[i]);
    if (p.bloomFbo[i]) glDeleteFramebuffers(1, &p.bloomFbo[i]);
    p.bloomTex[i] = p.bloomFbo[i] = 0;
    p.bloomW[i] = p.bloomH[i] = 0;
  }
}

static inline void AllocHalfFloat(int w, int h) {
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT,
               nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}
inline bool AllocateTargets(Pipeline &p, int w, int h) {
  DestroyTargets(p);
  p.width = w > 0 ? w : 1;
  p.height = h > 0 ? h : 1;

  glGenTextures(1, &p.colorTex);
  glBindTexture(GL_TEXTURE_2D, p.colorTex);
  AllocHalfFloat(p.width, p.height);
  glGenRenderbuffers(1, &p.depthRb);
  glBindRenderbuffer(GL_RENDERBUFFER, p.depthRb);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, p.width,
                        p.height);

  glGenFramebuffers(1, &p.fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, p.fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         p.colorTex, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                            GL_RENDERBUFFER, p.depthRb);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    std::fprintf(stderr, "post: HDR render target is incomplete\n");
    return false;
  }
  int bw = p.width / 2 > 0 ? p.width / 2 : 1;
  int bh = p.height / 2 > 0 ? p.height / 2 : 1;
  for (int i = 0; i < kLevels; i++) {
    p.bloomW[i] = bw > 0 ? bw : 1;
    p.bloomH[i] = bh > 0 ? bh : 1;
    glGenTextures(1, &p.bloomTex[i]);
    glBindTexture(GL_TEXTURE_2D, p.bloomTex[i]);
    AllocHalfFloat(p.bloomW[i], p.bloomH[i]);
    glGenFramebuffers(1, &p.bloomFbo[i]);
    glBindFramebuffer(GL_FRAMEBUFFER, p.bloomFbo[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           p.bloomTex[i], 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      std::fprintf(stderr, "post: bloom level %d is incomplete\n", i);
      return false;
    }
    bw >>= 1;
    bh >>= 1;
    if (bw < 2) bw = 2;
    if (bh < 2) bh = 2;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return true;
}

inline bool Init(Pipeline &p, int w, int h) {
  if (p.ready) Destroy(p);

  p.progDown = BuildProgram(kDownFrag, "bloom downsample");
  p.progUp = BuildProgram(kUpFrag, "bloom upsample");
  p.progFinal = BuildProgram(kFinalFrag, "composite");
  if (!p.progDown || !p.progUp || !p.progFinal) {
    Destroy(p);
    return false;
  }

  p.downSrc = glGetUniformLocation(p.progDown, "uSrc");
  p.downTexel = glGetUniformLocation(p.progDown, "uTexel");
  p.downPrefilter = glGetUniformLocation(p.progDown, "uPrefilter");
  p.downThreshold = glGetUniformLocation(p.progDown, "uThreshold");
  p.downKnee = glGetUniformLocation(p.progDown, "uKnee");
  p.upSrc = glGetUniformLocation(p.progUp, "uSrc");
  p.upTexel = glGetUniformLocation(p.progUp, "uTexel");
  p.finHdr = glGetUniformLocation(p.progFinal, "uHdr");
  p.finBloom = glGetUniformLocation(p.progFinal, "uBloom");
  p.finExposure = glGetUniformLocation(p.progFinal, "uExposure");
  p.finBloomStrength = glGetUniformLocation(p.progFinal, "uBloomStrength");
  p.finTime = glGetUniformLocation(p.progFinal, "uTime");
  p.finAberration = glGetUniformLocation(p.progFinal, "uAberration");
  p.finVignette = glGetUniformLocation(p.progFinal, "uVignette");
  p.finGrain = glGetUniformLocation(p.progFinal, "uGrain");
  p.finFlare = glGetUniformLocation(p.progFinal, "uFlare");
  static const float kTri[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
  glGenVertexArrays(1, &p.vao);
  glGenBuffers(1, &p.vbo);
  glBindVertexArray(p.vao);
  glBindBuffer(GL_ARRAY_BUFFER, p.vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(kTri), kTri, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);
  glBindVertexArray(0);

  if (!AllocateTargets(p, w, h)) {
    DestroyTargets(p);
    Destroy(p);
    return false;
  }
  p.ready = true;
  return true;
}

inline void Destroy(Pipeline &p) {
  DestroyTargets(p);
  if (p.progDown) glDeleteProgram(p.progDown);
  if (p.progUp) glDeleteProgram(p.progUp);
  if (p.progFinal) glDeleteProgram(p.progFinal);
  if (p.vbo) glDeleteBuffers(1, &p.vbo);
  if (p.vao) glDeleteVertexArrays(1, &p.vao);
  p.progDown = p.progUp = p.progFinal = 0;
  p.vao = p.vbo = 0;
  p.ready = false;
  p.width = p.height = 0;
}
inline bool Begin(Pipeline &p, int w, int h) {
  if (!p.ready) return false;
  if (w != p.width || h != p.height) {
    if (!AllocateTargets(p, w, h)) {
      p.ready = false;
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      return false;
    }
  }
  glBindFramebuffer(GL_FRAMEBUFFER, p.fbo);
  return true;
}
inline void End(Pipeline &p, const Settings &s, float timeSeconds) {
  if (!p.ready) return;

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glDepthMask(GL_FALSE);
  glBindVertexArray(p.vao);

  // ---- bloom: thresholded downsample chain ----
  glUseProgram(p.progDown);
  glActiveTexture(GL_TEXTURE0);
  glUniform1i(p.downSrc, 0);
  glUniform1f(p.downThreshold, s.threshold);
  glUniform1f(p.downKnee, s.knee);
  for (int i = 0; i < kLevels; i++) {
    glBindFramebuffer(GL_FRAMEBUFFER, p.bloomFbo[i]);
    glViewport(0, 0, p.bloomW[i], p.bloomH[i]);
    glBindTexture(GL_TEXTURE_2D, i == 0 ? p.colorTex : p.bloomTex[i - 1]);
    const int srcW = i == 0 ? p.width : p.bloomW[i - 1];
    const int srcH = i == 0 ? p.height : p.bloomH[i - 1];
    glUniform2f(p.downTexel, 1.0f / (float)srcW, 1.0f / (float)srcH);
    glUniform1f(p.downPrefilter, i == 0 ? 1.0f : 0.0f);
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }

  // ---- bloom: tent upsample, accumulated additively ----
  glUseProgram(p.progUp);
  glUniform1i(p.upSrc, 0);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE);
  for (int i = kLevels - 1; i > 0; i--) {
    glBindFramebuffer(GL_FRAMEBUFFER, p.bloomFbo[i - 1]);
    glViewport(0, 0, p.bloomW[i - 1], p.bloomH[i - 1]);
    glBindTexture(GL_TEXTURE_2D, p.bloomTex[i]);
    glUniform2f(p.upTexel, 1.0f / (float)p.bloomW[i],
                1.0f / (float)p.bloomH[i]);
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  glDisable(GL_BLEND);

  // ---- composite ----
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, p.width, p.height);
  glUseProgram(p.progFinal);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, p.colorTex);
  glUniform1i(p.finHdr, 0);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, p.bloomTex[0]);
  glUniform1i(p.finBloom, 1);
  glActiveTexture(GL_TEXTURE0);
  glUniform1f(p.finExposure, s.exposure);
  glUniform1f(p.finBloomStrength, s.bloom);
  glUniform1f(p.finTime, timeSeconds);
  glUniform1f(p.finAberration, s.aberration);
  glUniform1f(p.finVignette, s.vignette);
  glUniform1f(p.finGrain, s.grain);
  glUniform1f(p.finFlare, s.flare);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);

  glDepthMask(GL_TRUE);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
}

}  // namespace postproc
