// ===========================================================================
// ElectroBench -- shared HDR camera pipeline for the GL 3.3 scenes.
//
// The three GL 3.3 scenes (2 dusk ocean, 3 pool room, 4 power lines) used to
// write their own tone-mapped, gamma-encoded LDR colour straight into the
// default framebuffer. That has three photographic consequences a scene
// shader cannot fix on its own:
//
//   * every pass picked its own response curve (scene 2 used color/(color+1),
//     scene 4 a Reinhard-ish col/(col*0.35+0.72), the sky an ACES blend), so
//     the same radiance had three different brightnesses depending on which
//     pass drew it;
//   * there is no bloom, so the sun, the glitter path, wet-road speculars and
//     the pool's ceiling panel have hard edges instead of the glare a real
//     lens and a real eye produce around a bright source;
//   * the scene wrote 8-bit LDR, so anything above 1.0 was clipped before it
//     could be graded.
//
// This module renders the scene into a half-float, 4x MSAA target, resolves
// it, extracts a physically-shaped bloom pyramid from the real HDR values
// (soft-knee threshold, 13-tap downsample, tent upsample), and composites once
// with the ACES filmic curve -- the same response for every pass -- plus the
// rest of a camera: exposure, lateral chromatic aberration, lens ghosts,
// cos^4 vignette, film grain and dither.
//
// Usage, per frame:
//     bool hdr = postproc::Begin(gPost, w, h);
//     ... draw the scene (shaders output linear radiance when uHdrOut = 1) ...
//     postproc::End(gPost, settings);      // -> default framebuffer, LDR
//     ... draw the HUD (stays LDR, on top of the finished frame) ...
//
// Begin() binds the HDR target when it is available and the default
// framebuffer otherwise, so a driver that refuses the render target degrades
// to the old direct-to-window path instead of showing a black screen.
// ===========================================================================
#pragma once

#include <GL/glew.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace postproc {

// ------------------------------------------------------------------ shaders
static const char *kFullscreenVert = R"GLSL(
#version 330 core
out vec2 vUv;
void main() {
    // One oversized triangle: covers the viewport with no vertex buffer and no
    // attribute state, which core profile is happy to draw with an empty VAO.
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

// 13-tap downsample (Jimenez, "Next Generation Post Processing in Call of
// Duty: Advanced Warfare"). The 13 taps, not a naive 2x2 box, are what keeps
// the bloom from crawling and flickering while the camera moves: a 2x2 box
// aliases the source, this kernel is a proper low-pass.
static const char *kDownFrag = R"GLSL(
#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uSrc;
uniform vec2 uTexel;
uniform int   uPrefilter;
uniform float uThreshold;
uniform float uKnee;

vec3 tap(vec2 uv) { return texture(uSrc, uv).rgb; }

void main() {
    vec2 t = uTexel;
    vec3 a = tap(vUv + t * vec2(-1.0, -1.0));
    vec3 b = tap(vUv + t * vec2( 0.0, -1.0));
    vec3 c = tap(vUv + t * vec2( 1.0, -1.0));
    vec3 d = tap(vUv + t * vec2(-1.0,  0.0));
    vec3 e = tap(vUv);
    vec3 f = tap(vUv + t * vec2( 1.0,  0.0));
    vec3 g = tap(vUv + t * vec2(-1.0,  1.0));
    vec3 h = tap(vUv + t * vec2( 0.0,  1.0));
    vec3 i = tap(vUv + t * vec2( 1.0,  1.0));
    vec3 j = tap(vUv + t * vec2(-0.5, -0.5));
    vec3 k = tap(vUv + t * vec2( 0.5, -0.5));
    vec3 l = tap(vUv + t * vec2(-0.5,  0.5));
    vec3 m = tap(vUv + t * vec2( 0.5,  0.5));

    vec3 col = e * 0.125;
    col += (a + c + g + i) * 0.03125;
    col += (b + d + f + h) * 0.0625;
    col += (j + k + l + m) * 0.125;

    if (uPrefilter == 1) {
        // Soft-knee bright pass: below the threshold nothing survives, around
        // it the response is quadratic so a slightly dimmer highlight does not
        // pop into the bloom as a hard disc, above it the excess is kept 1:1
        // so the sun still blooms with the right energy.
        float br = max(col.r, max(col.g, col.b));
        float knee = max(uKnee, 1e-4);
        float soft = clamp(br - uThreshold + knee, 0.0, 2.0 * knee);
        soft = soft * soft / (4.0 * knee);
        float contrib = max(soft, br - uThreshold) / max(br, 1e-4);
        col *= contrib;
    }
    fragColor = vec4(col, 1.0);
}
)GLSL";

// 9-tap tent upsample, accumulated with additive blending into the next
// (larger) pyramid level: the wide soft falloff of real glare.
static const char *kUpFrag = R"GLSL(
#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uSrc;
uniform vec2 uTexel;

void main() {
    vec2 t = uTexel;
    vec3 col = texture(uSrc, vUv).rgb * 4.0;
    col += (texture(uSrc, vUv + t * vec2(-1.0, 0.0)).rgb
          + texture(uSrc, vUv + t * vec2( 1.0, 0.0)).rgb
          + texture(uSrc, vUv + t * vec2( 0.0,-1.0)).rgb
          + texture(uSrc, vUv + t * vec2( 0.0, 1.0)).rgb) * 2.0;
    col += (texture(uSrc, vUv + t * vec2(-1.0,-1.0)).rgb
          + texture(uSrc, vUv + t * vec2( 1.0,-1.0)).rgb
          + texture(uSrc, vUv + t * vec2(-1.0, 1.0)).rgb
          + texture(uSrc, vUv + t * vec2( 1.0, 1.0)).rgb);
    fragColor = vec4(col / 16.0, 1.0);
}
)GLSL";

static const char *kFinalFrag = R"GLSL(
#version 330 core
in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uHdr;
uniform sampler2D uBloom;
uniform float uExposure;
uniform float uBloomStrength;
uniform float uTime;
uniform float uAberration;
uniform float uVignette;
uniform float uGrain;
uniform float uFlare;

// ACES filmic RRT+ODT fit (Stephen Hill): linear sRGB in, display-referred
// linear out. Every pass shares this curve, which is the point -- the old
// per-shader curves each put 0.18 at a different screen value.
const mat3 kAcesIn = mat3(
    0.59719, 0.07600, 0.02840,
    0.35458, 0.90834, 0.13383,
    0.04823, 0.01566, 0.83777);
const mat3 kAcesOut = mat3(
     1.60475, -0.10208, -0.00327,
    -0.53108,  1.10813, -0.07276,
    -0.07367, -0.00605,  1.07602);

vec3 RrtOdt(vec3 v) {
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

vec3 AcesFitted(vec3 c) {
    c = kAcesIn * max(c, 0.0);
    c = RrtOdt(c);
    c = kAcesOut * c;
    return clamp(c, 0.0, 1.0);
}

vec3 EncodeSrgb(vec3 c) {
    // Piecewise sRGB transfer, not a 2.2 power: the toe matters for the dark
    // end of a dusk scene and it keeps the highlights from skewing blue.
    vec3 lo = c * 12.92;
    vec3 hi = 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(hi, lo, step(c, vec3(0.0031308)));
}

float Hash(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}

void main() {
    vec2 uv = vUv;

    // Lateral chromatic aberration: a real lens focuses red and blue at
    // slightly different distances, so the error grows with the square of the
    // radius and is zero at the centre. Sampling the HDR buffer per channel
    // (before the tone curve) keeps it a refraction, not a colour shift.
    //
    // The offset is |dir| * r2 * uAberration of the frame width, so at the
    // corner it is 0.35 * uAberration. A real fast lens lands between 0.1% and
    // 0.3% of the frame, i.e. uAberration 0.003-0.009; the 0.55 this used to
    // be separated the channels by 19% of the frame and put three visibly
    // different images on screen instead of fringing the edges.
    vec2 dir = uv - 0.5;
    float r2 = dot(dir, dir);
    vec2 off = dir * r2 * uAberration;
    vec3 hdr;
    hdr.r = texture(uHdr, uv + off).r;
    hdr.g = texture(uHdr, uv).g;
    hdr.b = texture(uHdr, uv - off).b;

    // Bloom (the scene's own HDR values, thresholded and blurred) and a thin
    // lens-ghost train mirrored through the frame centre, the way a bright
    // sun reflecting between glass surfaces paints faint inverted images.
    vec3 bloom = texture(uBloom, uv).rgb;
    vec3 ghosts = vec3(0.0);
    if (uFlare > 0.0) {
        vec2 mirror = vec2(0.5) - uv;
        ghosts += texture(uBloom, uv + mirror * 0.62).rgb * vec3(0.55, 0.75, 1.0);
        ghosts += texture(uBloom, uv + mirror * 1.08).rgb * vec3(1.00, 0.62, 0.35);
        ghosts += texture(uBloom, uv + mirror * 1.52).rgb * vec3(0.45, 1.00, 0.70);
        ghosts *= uFlare * 0.045;
    }

    vec3 col = hdr + (bloom * uBloomStrength + ghosts);

    // Exposure is a camera property: one multiplier for the whole frame, so
    // the relative brightness of sun, sky sea and shadow stays physical.
    col *= uExposure;

    col = AcesFitted(col);

    // Split tone: the dusk grade (cool shadows, warm highlights) belongs in
    // the one stage every pass goes through. Left in the individual shaders it
    // was applied to radiance with three different curves underneath it; here
    // it is a colour decision on the finished, film-mapped image, which is
    // what a grade is.
    float tone = dot(col, vec3(0.2126, 0.7152, 0.0722));
    vec3 cool = col * vec3(0.945, 0.985, 1.055);
    vec3 warm = col * vec3(1.045, 1.008, 0.955);
    col = mix(cool, col, smoothstep(0.04, 0.30, tone));
    col = mix(col, warm, smoothstep(0.55, 1.00, tone) * 0.55);

    // cos^4-style vignette: natural light falloff towards the corners.
    float vig = 1.0 - uVignette * pow(clamp(length(dir) * 1.35, 0.0, 1.0), 2.6);
    col *= vig;

    // Grain lives after the tone curve, like film: it is densest in the
    // midtones and vanishes in the deep shadows and the specular highlights.
    float lum = dot(col, vec3(0.2126, 0.7152, 0.0722));
    float grainMask = 4.0 * lum * (1.0 - lum);
    float n = Hash(gl_FragCoord.xy + fract(uTime) * 37.0)
            + Hash(gl_FragCoord.xy * 1.7 + 11.0) - 1.0;
    col += n * uGrain * grainMask;

    col = EncodeSrgb(clamp(col, 0.0, 1.0));

    // One LSB of ordered dither: an 8-bit framebuffer showing a smooth dusk
    // gradient bands without it.
    col += (Hash(gl_FragCoord.xy * 0.71) - 0.5) / 255.0;

    fragColor = vec4(col, 1.0);
}
)GLSL";

// ----------------------------------------------------------------- pipeline
struct Settings {
  float exposure = 1.0f;
  float bloom = 0.085f;
  float aberration = 0.0085f;  // frame-width fraction at the corner; see final pass
  float vignette = 0.22f;
  float grain = 0.020f;
  float flare = 1.0f;
  float threshold = 1.0f;
  float knee = 0.55f;
};

struct Pipeline {
  bool ready = false;
  bool msaaPath = false;
  int width = 0;
  int height = 0;
  int samples = 0;

  GLuint msFbo = 0;
  GLuint msColorRb = 0;
  GLuint msDepthRb = 0;
  GLuint resolveFbo = 0;
  GLuint resolveTex = 0;

  static const int kLevels = 6;
  GLuint bloomTex[kLevels] = {0};
  GLuint bloomFbo[kLevels] = {0};
  int bloomW[kLevels] = {0};
  int bloomH[kLevels] = {0};

  GLuint progDown = 0;
  GLuint progUp = 0;
  GLuint progFinal = 0;
  GLuint emptyVao = 0;
};

inline GLuint CompileStage(GLenum type, const char *src, const char *label) {
  GLuint sh = glCreateShader(type);
  glShaderSource(sh, 1, &src, nullptr);
  glCompileShader(sh);
  GLint ok = 0;
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

inline GLuint BuildProgram(const char *frag, const char *label) {
  GLuint vs = CompileStage(GL_VERTEX_SHADER, kFullscreenVert, "fullscreen vert");
  GLuint fs = CompileStage(GL_FRAGMENT_SHADER, frag, label);
  if (!vs || !fs) return 0;
  GLuint prog = glCreateProgram();
  glAttachShader(prog, vs);
  glAttachShader(prog, fs);
  glBindAttribLocation(prog, 0, "aUnused"); // keeps core-profile validation quiet
  glLinkProgram(prog);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = 0;
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
  if (p.msColorRb) glDeleteRenderbuffers(1, &p.msColorRb);
  if (p.msDepthRb) glDeleteRenderbuffers(1, &p.msDepthRb);
  if (p.msFbo) glDeleteFramebuffers(1, &p.msFbo);
  if (p.resolveFbo) glDeleteFramebuffers(1, &p.resolveFbo);
  if (p.resolveTex) glDeleteTextures(1, &p.resolveTex);
  p.msColorRb = p.msDepthRb = 0;
  p.msFbo = p.resolveFbo = p.resolveTex = 0;
  for (int i = 0; i < Pipeline::kLevels; i++) {
    if (p.bloomTex[i]) glDeleteTextures(1, &p.bloomTex[i]);
    if (p.bloomFbo[i]) glDeleteFramebuffers(1, &p.bloomFbo[i]);
    p.bloomTex[i] = p.bloomFbo[i] = 0;
    p.bloomW[i] = p.bloomH[i] = 0;
  }
}

// Builds the HDR target and the bloom pyramid for the given size. Returns
// false (and leaves the pipeline disabled) when the driver cannot provide a
// complete half-float target; the caller then renders straight to the window.
inline bool AllocateTargets(Pipeline &p, int w, int h, int requestedSamples) {
  DestroyTargets(p);
  p.width = w > 0 ? w : 1;
  p.height = h > 0 ? h : 1;
  p.samples = 0;

  glGenTextures(1, &p.resolveTex);
  glBindTexture(GL_TEXTURE_2D, p.resolveTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, p.width, p.height, 0, GL_RGBA,
               GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  // The resolve target only carries colour; the scene's depth buffer belongs
  // to whichever target the scene actually draws into.
  glGenFramebuffers(1, &p.resolveFbo);
  glBindFramebuffer(GL_FRAMEBUFFER, p.resolveFbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         p.resolveTex, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    return false;

  glGenFramebuffers(1, &p.msFbo);
  glBindFramebuffer(GL_FRAMEBUFFER, p.msFbo);

  p.msaaPath = false;
  if (requestedSamples > 1) {
    GLint maxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    int samples = requestedSamples < maxSamples ? requestedSamples : maxSamples;
    for (; samples > 1 && !p.msaaPath; samples >>= 1) {
      glGenRenderbuffers(1, &p.msColorRb);
      glBindRenderbuffer(GL_RENDERBUFFER, p.msColorRb);
      glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA16F,
                                       p.width, p.height);
      glGenRenderbuffers(1, &p.msDepthRb);
      glBindRenderbuffer(GL_RENDERBUFFER, p.msDepthRb);
      glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples,
                                       GL_DEPTH_COMPONENT24, p.width, p.height);
      glBindFramebuffer(GL_FRAMEBUFFER, p.msFbo);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_RENDERBUFFER, p.msColorRb);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                GL_RENDERBUFFER, p.msDepthRb);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        p.msaaPath = true;
        p.samples = samples;
      } else {
        glDeleteRenderbuffers(1, &p.msColorRb);
        glDeleteRenderbuffers(1, &p.msDepthRb);
        p.msColorRb = p.msDepthRb = 0;
      }
    }
  }
  if (!p.msaaPath) {
    // Single-sample depth for the no-MSAA fallback (the buffer above, when
    // multisampled, was already deleted with the failed attempt).
    if (!p.msDepthRb) {
      glGenRenderbuffers(1, &p.msDepthRb);
      glBindRenderbuffer(GL_RENDERBUFFER, p.msDepthRb);
      glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, p.width,
                            p.height);
    }
    // No multisampling: render straight into the resolve texture.
    glBindFramebuffer(GL_FRAMEBUFFER, p.msFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           p.resolveTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, p.msDepthRb);
    p.samples = 1;
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      return false;
  }

  // Bloom pyramid: half resolution upwards, six levels (down to a handful of
  // pixels at 480p, plenty of width for the glare tail at 4K).
  int bw = p.width / 2 > 0 ? p.width / 2 : 1;
  int bh = p.height / 2 > 0 ? p.height / 2 : 1;
  for (int i = 0; i < Pipeline::kLevels; i++) {
    p.bloomW[i] = bw > 0 ? bw : 1;
    p.bloomH[i] = bh > 0 ? bh : 1;
    glGenTextures(1, &p.bloomTex[i]);
    glBindTexture(GL_TEXTURE_2D, p.bloomTex[i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, p.bloomW[i], p.bloomH[i], 0,
                 GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &p.bloomFbo[i]);
    glBindFramebuffer(GL_FRAMEBUFFER, p.bloomFbo[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           p.bloomTex[i], 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      return false;
    bw >>= 1;
    bh >>= 1;
    if (bw < 2) bw = 2;
    if (bh < 2) bh = 2;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return true;
}

inline bool Init(Pipeline &p, int w, int h, int samples = 4) {
  Destroy(p);
  if (!GLEW_VERSION_3_0) return false;
  p.progDown = BuildProgram(kDownFrag, "bloom downsample");
  p.progUp = BuildProgram(kUpFrag, "bloom upsample");
  p.progFinal = BuildProgram(kFinalFrag, "composite");
  if (!p.progDown || !p.progUp || !p.progFinal) {
    Destroy(p);
    return false;
  }
  if (!p.emptyVao) glGenVertexArrays(1, &p.emptyVao);
  if (!AllocateTargets(p, w, h, samples)) {
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
  if (p.emptyVao) glDeleteVertexArrays(1, &p.emptyVao);
  p.progDown = p.progUp = p.progFinal = 0;
  p.emptyVao = 0;
  p.ready = false;
  p.width = p.height = 0;
}

// Binds the target the frame should be drawn into. Always binds something, so
// the caller can render normally and only has to ask End() to finish.
inline bool Begin(Pipeline &p, int w, int h) {
  if (!p.ready) return false;
  if (w != p.width || h != p.height) {
    if (!AllocateTargets(p, w, h, p.samples > 1 ? p.samples : 4)) {
      p.ready = false;
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      return false;
    }
  }
  glBindFramebuffer(GL_FRAMEBUFFER, p.msFbo);
  return true;
}

// Resolves, blooms and composites into the default framebuffer, then restores
// the state the HUD and the next frame expect.
inline void End(Pipeline &p, const Settings &s, float timeSeconds) {
  if (!p.ready) return;

  // ---- resolve MSAA into the sampleable HDR texture ----
  if (p.msaaPath) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, p.msFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, p.resolveFbo);
    glBlitFramebuffer(0, 0, p.width, p.height, 0, 0, p.width, p.height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glDisable(GL_SCISSOR_TEST);
  glDepthMask(GL_FALSE);

  // ---- bloom: thresholded downsample chain ----
  glUseProgram(p.progDown);
  glActiveTexture(GL_TEXTURE0);
  glUniform1i(glGetUniformLocation(p.progDown, "uSrc"), 0);
  glUniform1f(glGetUniformLocation(p.progDown, "uThreshold"), s.threshold);
  glUniform1f(glGetUniformLocation(p.progDown, "uKnee"), s.knee);
  glBindVertexArray(p.emptyVao);
  for (int i = 0; i < Pipeline::kLevels; i++) {
    glBindFramebuffer(GL_FRAMEBUFFER, p.bloomFbo[i]);
    glViewport(0, 0, p.bloomW[i], p.bloomH[i]);
    glBindTexture(GL_TEXTURE_2D, i == 0 ? p.resolveTex : p.bloomTex[i - 1]);
    const int srcW = i == 0 ? p.width : p.bloomW[i - 1];
    const int srcH = i == 0 ? p.height : p.bloomH[i - 1];
    glUniform2f(glGetUniformLocation(p.progDown, "uTexel"), 1.0f / (float)srcW,
                1.0f / (float)srcH);
    glUniform1i(glGetUniformLocation(p.progDown, "uPrefilter"), i == 0 ? 1 : 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }

  // ---- bloom: tent upsample, accumulated additively ----
  glUseProgram(p.progUp);
  glUniform1i(glGetUniformLocation(p.progUp, "uSrc"), 0);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE);
  for (int i = Pipeline::kLevels - 1; i > 0; i--) {
    glBindFramebuffer(GL_FRAMEBUFFER, p.bloomFbo[i - 1]);
    glViewport(0, 0, p.bloomW[i - 1], p.bloomH[i - 1]);
    glBindTexture(GL_TEXTURE_2D, p.bloomTex[i]);
    glUniform2f(glGetUniformLocation(p.progUp, "uTexel"),
                1.0f / (float)p.bloomW[i], 1.0f / (float)p.bloomH[i]);
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  glDisable(GL_BLEND);

  // ---- composite ----
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, p.width, p.height);
  glUseProgram(p.progFinal);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, p.resolveTex);
  glUniform1i(glGetUniformLocation(p.progFinal, "uHdr"), 0);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, p.bloomTex[0]);
  glUniform1i(glGetUniformLocation(p.progFinal, "uBloom"), 1);
  glActiveTexture(GL_TEXTURE0);
  glUniform1f(glGetUniformLocation(p.progFinal, "uExposure"), s.exposure);
  glUniform1f(glGetUniformLocation(p.progFinal, "uBloomStrength"), s.bloom);
  glUniform1f(glGetUniformLocation(p.progFinal, "uTime"), timeSeconds);
  glUniform1f(glGetUniformLocation(p.progFinal, "uAberration"), s.aberration);
  glUniform1f(glGetUniformLocation(p.progFinal, "uVignette"), s.vignette);
  glUniform1f(glGetUniformLocation(p.progFinal, "uGrain"), s.grain);
  glUniform1f(glGetUniformLocation(p.progFinal, "uFlare"), s.flare);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);

  glDepthMask(GL_TRUE);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glEnable(GL_SCISSOR_TEST);
}

} // namespace postproc
