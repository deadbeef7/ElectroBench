// ===========================================================================
// SDL2 subset implementation for the ElectroBench Android port.
//
// Same contract as vitaport/src/vita_sdl.cxx (declared in
// include/SDL2/SDL.h): a window + GL context, a polled event loop, a
// high-resolution timer and error strings. On Android the pieces are
//
//   window + GL context -> EGL over the NativeActivity's ANativeWindow
//   input               -> AInputQueue, edge detected (BACK quits, tap/A/ENTER
//                          ends the current scene, like the Vita's START/SELECT)
//   clock               -> clock_gettime(CLOCK_MONOTONIC)
//   assets              -> the app's own files directory, which android_main
//                          fills from the APK's assets/ (see android_main.cxx)
//
// Two Android-specific facts drive the design:
//
//   * The context is OpenGL ES 3.0, not the 3.3 core the scenes ask for. ES 3.0
//     is the version that has every entry point the scenes use (VAOs,
//     glReadBuffer, glBlitFramebuffer, glFramebufferTexture2D); a 3.3-core
//     request is accepted and ignored exactly as on the Vita, where the request
//     is answered by "yes, you may render".
//   * Each scene gets its own EGL config and context. Scene 4 asks for 4x MSAA
//     and falls back to none if the context fails, so the attribute snapshot
//     taken in SDL_CreateWindow is what SDL_GL_CreateContext builds; tearing
//     the context down in SDL_GL_DeleteContext is also what stops scene 2's GL
//     objects from leaking into scene 4. The EGLDisplay connection itself is
//     process-scoped: nothing here calls eglTerminate but process death. EGL
//     legally allows a terminated display to be re-initialised, but the
//     terminate-then-reinit round trip inside a scene switch is one more driver
//     path than the switch needs - SDL2 and the NDK's own GL helper keep the
//     display up and destroy only the context and the surface.
//
// This file additionally owns the port's on-device capture. A phone has no
// desktop to grab a window from and usually no debugger attached, and the
// port's history is the reason it exists: under Vita3K the emulator could not
// put a single non-black pixel on screen, so "does this thing draw" has to be
// answerable from the device itself. Frames are read back inside
// SDL_GL_SwapWindow — where the frame is complete but not yet presented — via a
// blit into a single-sample FBO (the only ES 3.0 way to read a multisampled
// default framebuffer) and written as PNGs into the app's external files dir.
// ===========================================================================
#include <SDL2/SDL.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <android/input.h>
#include <android/looper.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unistd.h>
#include <vector>

#include "android_port.hxx"

// The vendored lodepng is a header-only amalgamation (lib/lodepng.h includes
// lodepng.c at the bottom), so this one include is what compiles the encoder
// into this translation unit. lib/lodepng.c must NOT be compiled separately as
// well: both copies would define every lodepng symbol and the link would fail
// with duplicate symbols.
#include "lodepng.h"

// The glue's android_main() stores the app here (android/jni/android_main.cxx).
extern struct android_app *gApp;

// ES 3.0 bit. EGL 1.5 names it without the KHR suffix; older headers only have
// the extension spelling, so accept either.
#ifndef EGL_OPENGL_ES3_BIT
#ifdef EGL_OPENGL_ES3_BIT_KHR
#define EGL_OPENGL_ES3_BIT EGL_OPENGL_ES3_BIT_KHR
#else
#define EGL_OPENGL_ES3_BIT 0x00000040
#endif
#endif

// ---------------------------------------------------------------- window/log
// SDL.h only forward declares this; the one window lives here. Width/height are
// the real surface size (the activity's window), not a fixed panel size as on
// the Vita: gWindowedMode is false in this port, so the scenes adopt whatever
// SDL_GetWindowSize reports and render at the device's native resolution.
struct SDL_Window {
  int width = 0;
  int height = 0;
  Uint32 flags = 0;
  char title[128] = {0};
};

namespace {

const int kMaxShots = 8;
const int kEventQueue = 16;

SDL_Window gWindow;

// GL attributes: SDL_GL_SetAttribute writes them, SDL_GL_CreateContext reads
// them, and gActualSamples is what the chosen EGL config really granted (what
// SDL_GL_GetAttribute answers with, so scene 4's "Antialiasing: Nx MSAA" line
// reports the truth rather than the request).
int gActualSamples = 0;

EGLDisplay gDisplay = EGL_NO_DISPLAY;
EGLConfig gConfig = nullptr;
EGLContext gContext = EGL_NO_CONTEXT;
EGLSurface gSurface = EGL_NO_SURFACE;

// The activity owns the ANativeWindow; this is a borrowed pointer that is valid
// between APP_CMD_INIT_WINDOW and APP_CMD_TERM_WINDOW.
ANativeWindow *gNativeWindow = nullptr;

int gDepthRequest = 24;
int gMsaaRequest = 0;
bool gInitialised = false;

// EGL version of the single, process-scoped display connection, remembered for
// the log line that used to be printed by the code that initialised it.
int gEglMajor = 0;
int gEglMinor = 0;


char gError[256] = {0};
char gInternalDir[512] = {0};
char gShareDir[512] = {0};

FILE *gLogFile = nullptr;

unsigned long gFrameTotal = 0;

// Input needs no edge detection of its own: the input queue already delivers
// key-down events (AKEY_EVENT_ACTION_DOWN) and touch-down events, and the scenes
// only ever look at SDL_KEYDOWN.
bool gQuitRequested = false;

SDL_Event gPending[kEventQueue];
int gPendingHead = 0;
int gPendingCount = 0;

// Screenshot schedule for the scene that is running. The clock starts at that
// scene's first presented frame, so the times are comparable with the scenes'
// own benchmark clock (45 s of benchmark, then a 4 s results screen).
char gSceneLabel[32] = "scene";
double gShotTimes[kMaxShots] = {0};
int gShotCount = 0;
int gNextShot = 0;
double gSceneT0 = 0.0;
bool gSceneT0Valid = false;
// Logged once per scene. This is the line whose absence answers "did scene 4
// ever present a frame?" - the question a device-side startup crash turns into
// the only question that matters.
bool gSceneFirstFrameLogged = false;

// Offscreen target for the capture blit, recreated only when the size changes.
GLuint gCaptureFbo = 0;
GLuint gCaptureTex = 0;
int gCaptureW = 0;
int gCaptureH = 0;

double NowSeconds() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

void SetError(const char *msg) { std::snprintf(gError, sizeof(gError), "%s", msg); }

void PushEvent(const SDL_Event &e) {
  if (gPendingCount >= kEventQueue) return;
  gPending[(gPendingHead + gPendingCount) % kEventQueue] = e;
  gPendingCount++;
}

bool PopEvent(SDL_Event *out) {
  if (!gPendingCount) return false;
  *out = gPending[gPendingHead];
  gPendingHead = (gPendingHead + 1) % kEventQueue;
  gPendingCount--;
  return true;
}

void PushKey(int32_t sym) {
  SDL_Event e;
  std::memset(&e, 0, sizeof(e));
  e.type = SDL_KEYDOWN;
  e.key.state = 1;
  e.key.keysym.sym = sym;
  PushEvent(e);
}

void PushQuit() {
  SDL_Event e;
  std::memset(&e, 0, sizeof(e));
  e.type = SDL_QUIT;
  gQuitRequested = true;
  PushEvent(e);
}

// ------------------------------------------------------------------- EGL bits
void DestroySurface() {
  if (gDisplay != EGL_NO_DISPLAY && gSurface != EGL_NO_SURFACE) {
    eglMakeCurrent(gDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(gDisplay, gSurface);
  }
  gSurface = EGL_NO_SURFACE;
}

// Drops the scene's half of the GL device: surface and context. Called from
// SDL_GL_DeleteContext so the next scene builds a config of its own (scene 4's
// MSAA request must not be answered by scene 2's no-MSAA surface) and so the
// previous scene's GL objects die with it. The display connection stays up for
// the life of the process (see the file header).
//
// The capture target's object names belong to the context that just died: they
// are zeroed here, because EnsureCaptureTarget trusts non-zero IDs and would
// otherwise hand scene 4 a frame-buffer scene 2 created - names that do not
// exist in the new context - and capture black frames through them.
void DestroyGL() {
  DestroySurface();
  if (gDisplay != EGL_NO_DISPLAY && gContext != EGL_NO_CONTEXT) {
    eglDestroyContext(gDisplay, gContext);
  }
  gContext = EGL_NO_CONTEXT;
  gConfig = nullptr;
  gActualSamples = 0;
  gCaptureFbo = 0;
  gCaptureTex = 0;
  gCaptureW = 0;
  gCaptureH = 0;
}

bool EnsureSurface() {
  if (gSurface != EGL_NO_SURFACE) return true;
  if (!gNativeWindow || gDisplay == EGL_NO_DISPLAY || gConfig == nullptr) {
    SetError("no Android window for the GL surface");
    return false;
  }
  // An EGL surface is a producer connected to the native window's BufferQueue,
  // and Android wants the window's buffers in the chosen config's native
  // visual format: both SDL2 and the NDK's own GL helper call
  // ANativeWindow_setBuffersGeometry with the config's EGL_NATIVE_VISUAL_ID
  // before eglCreateWindowSurface. Scene 4 creates its surface on the window
  // scene 2 already used, with a (possibly) different config, so the format has
  // to be re-asserted here rather than assumed to still match.
  EGLint visualId = 0;
  if (eglGetConfigAttrib(gDisplay, gConfig, EGL_NATIVE_VISUAL_ID, &visualId) != EGL_TRUE) {
    visualId = 0;
  }
  if (visualId > 0) {
    const int format = ANativeWindow_getFormat(gNativeWindow);
    if (format != (int)visualId) {
      const int rc = ANativeWindow_setBuffersGeometry(gNativeWindow, 0, 0, (int)visualId);
      PortLog("window format %d -> config visual %d (setBuffersGeometry rc=%d)", format,
              (int)visualId, rc);
    }
  }
  gSurface = eglCreateWindowSurface(gDisplay, gConfig, gNativeWindow, nullptr);
  if (gSurface == EGL_NO_SURFACE) {
    SetError("eglCreateWindowSurface failed");
    PortLog("eglCreateWindowSurface failed: 0x%04x (visual=%d, window format=%d)",
            eglGetError(), (int)visualId, ANativeWindow_getFormat(gNativeWindow));
    return false;
  }
  if (eglMakeCurrent(gDisplay, gSurface, gSurface, gContext) != EGL_TRUE) {
    SetError("eglMakeCurrent failed");
    PortLog("eglMakeCurrent failed: 0x%04x", eglGetError());
    DestroySurface();
    return false;
  }
  PortLog("GL surface ready: %dx%d, visual=%d, format=%d", gWindow.width, gWindow.height,
          (int)visualId, ANativeWindow_getFormat(gNativeWindow));
  return true;
}

bool InitGL() {
  // The display connection is process-scoped: initialise it once, and on later
  // scene switches simply reuse it (see the file header for why).
  if (gDisplay == EGL_NO_DISPLAY) {
    gDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (gDisplay == EGL_NO_DISPLAY) {
      SetError("eglGetDisplay failed");
      return false;
    }
    EGLint major = 0, minor = 0;
    if (eglInitialize(gDisplay, &major, &minor) != EGL_TRUE) {
      SetError("eglInitialize failed");
      gDisplay = EGL_NO_DISPLAY;
      return false;
    }
    gEglMajor = (int)major;
    gEglMinor = (int)minor;
    PortLog("EGL %d.%d initialised", gEglMajor, gEglMinor);
  }

  // MSAA is requested by scene 4 only. A missing multisample config is not an
  // error here: falling back to a single-sample config and reporting
  // SDL_GL_SAMPLES honestly is what the scene's own attribute query expects,
  // and it keeps the benchmark running instead of skipping the scene.
  const int wanted = gMsaaRequest >= 4 ? 4 : (gMsaaRequest >= 2 ? 2 : 0);
  for (int attempt = 0; attempt < 2 && gConfig == nullptr; attempt++) {
    const int samples = attempt == 0 ? wanted : 0;
    PortLog("choosing ES 3.0 config: attempt %d, msaa=%d, depth=%d", attempt + 1, samples,
            gDepthRequest);
    const EGLint attribs[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_DEPTH_SIZE,      gDepthRequest,
        EGL_SAMPLE_BUFFERS,  samples > 0 ? 1 : 0,
        EGL_SAMPLES,         samples,
        EGL_NONE,
    };
    EGLint count = 0;
    EGLConfig config = nullptr;
    if (eglChooseConfig(gDisplay, attribs, &config, 1, &count) == EGL_TRUE && count > 0) {
      gConfig = config;
      eglGetConfigAttrib(gDisplay, config, EGL_SAMPLES, &gActualSamples);
    } else {
      PortLog("  no match (eglChooseConfig returned 0x%04x, count=%d)", eglGetError(),
              (int)count);
    }
  }
  if (gConfig == nullptr) {
    SetError("no ES 3.0 window config");
    PortLog("eglChooseConfig found no ES 3.0 window config");
    DestroyGL();
    return false;
  }

  const EGLint ctxAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  gContext = eglCreateContext(gDisplay, gConfig, EGL_NO_CONTEXT, ctxAttribs);
  if (gContext == EGL_NO_CONTEXT) {
    SetError("eglCreateContext(ES 3.0) failed");
    PortLog("eglCreateContext(ES 3.0) failed: 0x%04x", eglGetError());
    DestroyGL();
    return false;
  }
  EGLint visualId = 0;
  eglGetConfigAttrib(gDisplay, gConfig, EGL_NATIVE_VISUAL_ID, &visualId);
  PortLog("EGL %d.%d, ES samples=%d, depth=%d, visual=%d", gEglMajor, gEglMinor, gActualSamples,
          gDepthRequest, (int)visualId);
  return true;
}

// ------------------------------------------------------- device diagnostics
// The sea program is the one thing that has failed on a real device, and a
// driver's "no" is invisible from the screen: the sea simply contributes no
// pixels while the sky and the HUD keep working. So the port reports what the
// device actually is and what it actually thinks of every shader, in its own
// handwriting, instead of leaving that to the scenes' stdout:
//
//   * the ES version, renderer, the limits that decide whether these shaders can
//     fit (uniform vectors, varyings, texture size) and the extensions that
//     decide whether scene 2's RGBA16F environment cube is renderable at all;
//   * every program the two scenes load, compiled and linked from the files the
//     port just unpacked, with the driver's compile/link logs verbatim - which
//     also proves the unpacked shader text is complete, since a truncated file
//     fails here first;
//   * the first GL errors raised while rendering, decoded, with frame numbers.

bool gValidated = false;
int gGlErrorsLogged = 0;

// Driver compile/link logs are multi-line; one PortLog call per line is what
// keeps each line inside the log buffer instead of truncating the message that
// explains the failure. (PortLog itself lives with the other port hooks at the
// bottom of this file and is declared in android_port.hxx.)
void PortLogBlock(const char *tag, const char *text) {
  const std::string block = text ? text : "";
  size_t pos = 0;
  for (;;) {
    size_t eol = block.find('\n', pos);
    if (eol == std::string::npos) eol = block.size();
    if (eol > pos) PortLog("%s%s", tag, block.substr(pos, eol - pos).c_str());
    if (eol >= block.size()) break;
    pos = eol + 1;
  }
}

const char *GlErrorName(GLenum err) {
  switch (err) {
    case GL_NO_ERROR: return "GL_NO_ERROR";
    case GL_INVALID_ENUM: return "GL_INVALID_ENUM";
    case GL_INVALID_VALUE: return "GL_INVALID_VALUE";
    case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
    case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION";
    case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY";
    default: return "unknown GL error";
  }
}

std::string ReadFromInternal(const char *relative) {
  std::string text;
  if (!gInternalDir[0]) return text;
  char path[600];
  std::snprintf(path, sizeof(path), "%s/%s", gInternalDir, relative);
  FILE *f = std::fopen(path, "rb");
  if (!f) return text;
  char buf[4096];
  size_t n = 0;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
  std::fclose(f);
  return text;
}

void LogGlCapabilities() {
  PortLog("--- device GL capabilities");
  PortLog("  vendor   : %s", (const char *)glGetString(GL_VENDOR));
  PortLog("  renderer : %s", (const char *)glGetString(GL_RENDERER));
  PortLog("  version  : %s", (const char *)glGetString(GL_VERSION));
  PortLog("  glsl     : %s", (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION));

  const struct { GLenum pname; const char *name; } limits[] = {
      {GL_MAX_FRAGMENT_UNIFORM_VECTORS, "GL_MAX_FRAGMENT_UNIFORM_VECTORS"},
      {GL_MAX_VERTEX_UNIFORM_VECTORS, "GL_MAX_VERTEX_UNIFORM_VECTORS"},
      {GL_MAX_VARYING_VECTORS, "GL_MAX_VARYING_VECTORS"},
      {GL_MAX_VERTEX_ATTRIBS, "GL_MAX_VERTEX_ATTRIBS"},
      {GL_MAX_TEXTURE_SIZE, "GL_MAX_TEXTURE_SIZE"},
      {GL_MAX_RENDERBUFFER_SIZE, "GL_MAX_RENDERBUFFER_SIZE"},
      {GL_MAX_TEXTURE_IMAGE_UNITS, "GL_MAX_TEXTURE_IMAGE_UNITS"},
  };
  for (const auto &limit : limits) {
    GLint value = 0;
    glGetIntegerv(limit.pname, &value);
    PortLog("  %-34s %d", limit.name, (int)value);
  }

  // ES 3.0 splits the extension list: glGetString(GL_EXTENSIONS) only reports
  // the pre-ES3 ones, so the complete list needs the indexed query. Both matter
  // here: GL_EXT_color_buffer_float decides whether the RGBA16F environment cube
  // the sea reflects is renderable, and scene 4's object shader enables
  // GL_OES_standard_derivatives explicitly.
  const struct { const char *name; const char *why; } wanted[] = {
      {"GL_EXT_color_buffer_float", "scene 2's RGBA16F environment cube is renderable"},
      {"GL_OES_texture_half_float_linear", "that cube is filterable"},
      {"GL_EXT_color_buffer_half_float", "half-float render targets (older spelling)"},
      {"GL_OES_standard_derivatives", "scene 4's object shader enables it"},
      {"GL_OES_vertex_array_object", "VAOs (core in ES 3.0, often still listed)"},
  };
  GLint extensionCount = 0;
  glGetIntegerv(GL_NUM_EXTENSIONS, &extensionCount);
  PortLog("  extensions: %d reported", (int)extensionCount);
  for (const auto &entry : wanted) {
    bool present = false;
    for (GLint i = 0; i < extensionCount && !present; i++) {
      const char *name = (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)i);
      if (name && std::strcmp(name, entry.name) == 0) present = true;
    }
    // The ES2-era string is still worth checking for drivers that omit the
    // indexed list.
    const char *legacy = (const char *)glGetString(GL_EXTENSIONS);
    if (!present && legacy && std::strstr(legacy, entry.name)) present = true;
    PortLog("  %-34s %-3s (%s)", entry.name, present ? "yes" : "NO", entry.why);
  }
}

GLuint CompileAndLog(GLenum type, const std::string &source, const char *file) {
  const GLuint shader = glCreateShader(type);
  const char *text = source.c_str();
  glShaderSource(shader, 1, &text, nullptr);
  glCompileShader(shader);
  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  const char *stage = type == GL_VERTEX_SHADER ? "vertex" : "fragment";
  if (ok) {
    PortLog("    %s %s compiled (%zu bytes)", stage, file, source.size());
  } else {
    std::vector<char> log(4096, 0);
    glGetShaderInfoLog(shader, (GLsizei)log.size() - 1, nullptr, log.data());
    PortLog("    %s %s COMPILE FAILED", stage, file);
    PortLogBlock("      driver: ", log.data());
  }
  return shader;
}

void LogProgramInterface(GLuint program) {
  GLint count = 0;
  glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &count);
  for (GLint i = 0; i < count && i < 40; i++) {
    char name[128] = {0};
    GLsizei length = 0;
    GLint size = 0;
    GLenum type = 0;
    glGetActiveUniform(program, (GLuint)i, sizeof(name) - 1, &length, &size, &type, name);
    // A -1 location is the interesting case: the driver dropped the uniform, so
    // the scene's glUniform* call is a silent no-op and the draw uses defaults.
    PortLog("    uniform %-28s loc=%-3d type=0x%04x size=%d", name,
            (int)glGetUniformLocation(program, name), (unsigned)type, (int)size);
  }
  glGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &count);
  for (GLint i = 0; i < count && i < 16; i++) {
    char name[128] = {0};
    GLsizei length = 0;
    GLint size = 0;
    GLenum type = 0;
    glGetActiveAttrib(program, (GLuint)i, sizeof(name) - 1, &length, &size, &type, name);
    PortLog("    attribute %-26s loc=%-3d type=0x%04x", name,
            (int)glGetAttribLocation(program, name), (unsigned)type);
  }
}

// Every shader the two scenes load, compiled and linked here first. This is the
// answer to "the sea does not show up": if the driver refuses that program, its
// own words are in the port's log rather than only in the scenes' stdout.
void ValidateShaderPrograms() {
  static const struct { const char *label; const char *vertex; const char *fragment; } programs[] = {
      {"scene 2 sea", "shaders/ps14/sea_vert.glsl", "shaders/ps14/sea_frag.glsl"},
      {"scene 2 sky", "shaders/ps14/sky_vert.glsl", "shaders/ps14/sky_frag.glsl"},
      {"scene 2 hud", "shaders/ps14/hud_vert.glsl", "shaders/ps14/hud_frag.glsl"},
      {"scene 4 sky", "shaders/pole/sky_vert.glsl", "shaders/pole/sky_frag.glsl"},
      {"scene 4 object", "shaders/pole/object_vert.glsl", "shaders/pole/object_frag.glsl"},
      {"scene 4 hud", "shaders/ps14/hud_vert.glsl", "shaders/pole/hud_frag.glsl"},
  };
  PortLog("--- shader validation (the port compiles all six programs itself)");
  for (const auto &program : programs) {
    const std::string vertexSource = ReadFromInternal(program.vertex);
    const std::string fragmentSource = ReadFromInternal(program.fragment);
    if (vertexSource.empty() || fragmentSource.empty()) {
      PortLog("  %s: SOURCE MISSING (%s %zu bytes, %s %zu bytes)", program.label,
              program.vertex, vertexSource.size(), program.fragment, fragmentSource.size());
      continue;
    }
    const GLuint vertex = CompileAndLog(GL_VERTEX_SHADER, vertexSource, program.vertex);
    const GLuint fragment = CompileAndLog(GL_FRAGMENT_SHADER, fragmentSource, program.fragment);
    const GLuint handle = glCreateProgram();
    glAttachShader(handle, vertex);
    glAttachShader(handle, fragment);
    glLinkProgram(handle);
    GLint ok = GL_FALSE;
    glGetProgramiv(handle, GL_LINK_STATUS, &ok);
    if (ok) {
      PortLog("  %s: linked", program.label);
      LogProgramInterface(handle);
    } else {
      std::vector<char> log(4096, 0);
      glGetProgramInfoLog(handle, (GLsizei)log.size() - 1, nullptr, log.data());
      PortLog("  %s: LINK FAILED", program.label);
      PortLogBlock("      driver: ", log.data());
    }
    glDeleteProgram(handle);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
  }
  for (GLenum err = glGetError(); err != GL_NO_ERROR; err = glGetError()) {
    PortLog("  GL error raised during validation: %s (0x%04x)", GlErrorName(err), (unsigned)err);
  }
}

// ------------------------------------------------------------------ event pump
// The glue's ALooper callbacks are the only way lifecycle and input reach us, so
// every SDL_PollEvent (and every swap while the surface is gone) drives it.
void PumpEvents(int timeoutMs) {
  struct android_app *app = gApp;
  if (!app) return;
  for (;;) {
    int events = 0;
    struct android_poll_source *source = nullptr;
    const int id = ALooper_pollAll(timeoutMs, nullptr, &events, (void **)&source);
    if (id < 0) return;  // timeout or error: nothing left to drain
    if (source) source->process(app, source);
    if (app->destroyRequested) {
      PushQuit();
      return;
    }
    timeoutMs = 0;  // drain the queue, do not block again
  }
}

// -------------------------------------------------------------------- capture
bool EnsureCaptureTarget(int w, int h) {
  if (gCaptureFbo && gCaptureW == w && gCaptureH == h) return true;
  if (!gCaptureFbo) glGenFramebuffers(1, &gCaptureFbo);
  if (!gCaptureTex) glGenTextures(1, &gCaptureTex);
  glBindTexture(GL_TEXTURE_2D, gCaptureTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glBindFramebuffer(GL_FRAMEBUFFER, gCaptureFbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gCaptureTex, 0);
  const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    PortLog("capture FBO incomplete (0x%04x) at %dx%d", status, w, h);
    return false;
  }
  gCaptureW = w;
  gCaptureH = h;
  return true;
}

// Reads the frame the scene just finished (the default framebuffer's back
// buffer) and writes it as a PNG. The blit is what makes this work with 4x MSAA
// in scene 4: glReadPixels cannot sample a multisampled buffer directly, but
// glBlitFramebuffer resolves it on the way into the single-sample FBO.
void WriteShot(int w, int h, const char *path) {
  if (!EnsureCaptureTarget(w, h)) return;
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gCaptureFbo);
  glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  glBindFramebuffer(GL_FRAMEBUFFER, gCaptureFbo);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);

  std::vector<unsigned char> rgba((size_t)w * h * 4);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // GL hands back bottom-up rows; PNG is top-down. Count the lit pixels while
  // flipping, because "lit" is the number that says whether the frame is an
  // image or a black wall, and the port has shipped black screens before.
  std::vector<unsigned char> rgb((size_t)w * h * 3);
  size_t lit = 0;
  for (int y = 0; y < h; y++) {
    const unsigned char *src = &rgba[(size_t)(h - 1 - y) * w * 4];
    unsigned char *dst = &rgb[(size_t)y * w * 3];
    for (int x = 0; x < w; x++) {
      dst[x * 3 + 0] = src[x * 4 + 0];
      dst[x * 3 + 1] = src[x * 4 + 1];
      dst[x * 3 + 2] = src[x * 4 + 2];
      if (src[x * 4 + 0] + src[x * 4 + 1] + src[x * 4 + 2] > 24) lit++;
    }
  }
  const unsigned err = lodepng_encode24_file(path, rgb.data(), (unsigned)w, (unsigned)h);
  PortLog("shot %s %dx%d lit=%zu/%d lodepng=%u", path, w, h, lit, w * h, err);
}

void CaptureDue() {
  if (!gSceneT0Valid) {
    gSceneT0 = NowSeconds();
    gSceneT0Valid = true;
  }
  const double elapsed = NowSeconds() - gSceneT0;
  while (gNextShot < gShotCount && elapsed >= gShotTimes[gNextShot]) {
    char name[96];
    char path[600];
    std::snprintf(name, sizeof(name), "%s_t%05.1fs.png", gSceneLabel, gShotTimes[gNextShot]);
    std::snprintf(path, sizeof(path), "%s/%s", gShareDir[0] ? gShareDir : ".", name);
    gNextShot++;
    WriteShot(gWindow.width, gWindow.height, path);
  }
}

// -------------------------------------------------------------------- handlers
void OnAppCmd(struct android_app *app, int32_t cmd) {
  switch (cmd) {
    case APP_CMD_INIT_WINDOW:
      // The window is back (first show, or after HOME). The GL device may still
      // exist from before; only the surface has to be rebuilt, which
      // SDL_GL_SwapWindow does on the next frame.
      gNativeWindow = app->window;
      if (gNativeWindow) {
        const int w = ANativeWindow_getWidth(gNativeWindow);
        const int h = ANativeWindow_getHeight(gNativeWindow);
        if (w > 0 && h > 0) {
          gWindow.width = w;
          gWindow.height = h;
        }
        PortLog("window ready: %dx%d", gWindow.width, gWindow.height);
      }
      break;
    case APP_CMD_TERM_WINDOW:
      // The activity is going away or being paused: drop the surface, keep the
      // context (and with it every GL object the running scene still holds).
      DestroySurface();
      gNativeWindow = nullptr;
      PortLog("window gone; surface released");
      break;
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONTENT_RECT_CHANGED:
      // Handled by the size check in SDL_GL_SwapWindow, which is also where the
      // scenes' ChangeSize() gets its SDL_WINDOWEVENT_RESIZED.
      break;
    case APP_CMD_GAINED_FOCUS:
      break;
    case APP_CMD_LOST_FOCUS:
      PortLog("lost focus");
      break;
    case APP_CMD_LOW_MEMORY:
      PortLog("low memory notification");
      break;
    case APP_CMD_DESTROY:
      PushQuit();
      break;
    default:
      break;
  }
}

int32_t OnInput(struct android_app *app, AInputEvent *event) {
  (void)app;
  const int32_t type = AInputEvent_getType(event);
  if (type == AINPUT_EVENT_TYPE_KEY) {
    if (AKeyEvent_getAction(event) != AKEY_EVENT_ACTION_DOWN) return 1;
    const int32_t code = AKeyEvent_getKeyCode(event);
    switch (code) {
      case AKEYCODE_BACK:
        // BACK leaves the app, the way START does on the Vita.
        PushQuit();
        break;
      case AKEYCODE_ESCAPE:
      case AKEYCODE_ENTER:
      case AKEYCODE_DPAD_CENTER:
      case AKEYCODE_SPACE:
      case AKEYCODE_BUTTON_A:
      case AKEYCODE_BUTTON_START:
        // Ends the current scene (the desktop's Esc key), so a docked
        // controller or a keyboard can skip ahead the way SELECT does on Vita.
        PushKey(SDLK_ESCAPE);
        break;
      default:
        break;
    }
    return 1;
  }
  if (type == AINPUT_EVENT_TYPE_MOTION) {
    // A tap ends the current scene: there is no other way to leave a 45 s
    // benchmark early on a phone, and the scenes' results screen is 4 s long.
    if (AMotionEvent_getAction(event) == AMOTION_EVENT_ACTION_DOWN) PushKey(SDLK_ESCAPE);
    return 1;
  }
  return 0;
}

// The window can change size under us (rotation is locked to landscape by the
// manifest, but split-screen and external displays are not). Rebuild the surface
// and tell the scene, which is what SDL_WINDOWEVENT_RESIZED is for.
void CheckWindowSize() {
  if (!gNativeWindow) return;
  const int w = ANativeWindow_getWidth(gNativeWindow);
  const int h = ANativeWindow_getHeight(gNativeWindow);
  if (w <= 0 || h <= 0 || (w == gWindow.width && h == gWindow.height)) return;
  gWindow.width = w;
  gWindow.height = h;
  DestroySurface();
  EnsureSurface();
  SDL_Event e;
  std::memset(&e, 0, sizeof(e));
  e.type = SDL_WINDOWEVENT;
  e.window.event = SDL_WINDOWEVENT_RESIZED;
  e.window.data1 = w;
  e.window.data2 = h;
  PushEvent(e);
  PortLog("surface resized to %dx%d", w, h);
}

}  // namespace

// ------------------------------------------------------------------ port hooks
void PortLog(const char *fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  __android_log_print(ANDROID_LOG_INFO, "ElectroBench", "%s", buf);
  if (gLogFile) {
    std::fprintf(gLogFile, "%s\n", buf);
    std::fflush(gLogFile);
  }
}

void PortSetDirs(const char *internalDir, const char *shareDir) {
  std::snprintf(gInternalDir, sizeof(gInternalDir), "%s", internalDir ? internalDir : "");
  std::snprintf(gShareDir, sizeof(gShareDir), "%s", shareDir ? shareDir : "");
  if (!gShareDir[0]) {
    // No external files directory (no card, storage not mounted): everything
    // lands in the app's private directory instead, which needs adb to read.
    std::snprintf(gShareDir, sizeof(gShareDir), "%s", gInternalDir);
  }
  gLogFile = PortShareOpen("portlog.txt", "w");
}

const char *PortShareDir(void) { return gShareDir[0] ? gShareDir : "."; }

FILE *PortShareOpen(const char *name, const char *mode) {
  char path[600];
  if (gShareDir[0]) {
    std::snprintf(path, sizeof(path), "%s/%s", gShareDir, name);
    FILE *f = std::fopen(path, mode);
    if (f) return f;
  }
  if (gInternalDir[0] && std::strcmp(gInternalDir, gShareDir) != 0) {
    std::snprintf(path, sizeof(path), "%s/%s", gInternalDir, name);
    return std::fopen(path, mode);
  }
  return nullptr;
}

void PortBeginScene(const char *label, const double *shotTimes, int shotCount) {
  std::snprintf(gSceneLabel, sizeof(gSceneLabel), "%s", label ? label : "scene");
  gShotCount = 0;
  for (int i = 0; i < shotCount && i < kMaxShots; i++) gShotTimes[gShotCount++] = shotTimes[i];
  gNextShot = 0;
  gSceneT0Valid = false;
  gSceneFirstFrameLogged = false;
  PortLog("--- %s: %d shot(s) scheduled ---", gSceneLabel, gShotCount);
}

int PortQuitRequested(void) { return gQuitRequested ? 1 : 0; }

unsigned long PortFrameCount(void) { return gFrameTotal; }

// Attaches the glue's lifecycle and input callbacks. Kept here rather than in
// android_main so that all of the platform glue lives next to the shim that
// answers it.
void PortAttachApp(struct android_app *app) {
  gApp = app;
  app->onAppCmd = OnAppCmd;
  app->onInputEvent = OnInput;
  gNativeWindow = app->window;
  if (app->window) {
    const int w = ANativeWindow_getWidth(app->window);
    const int h = ANativeWindow_getHeight(app->window);
    if (w > 0 && h > 0) {
      gWindow.width = w;
      gWindow.height = h;
    }
  }
}

// ----------------------------------------------------------------- SDL subset
extern "C" {

int SDL_Init(Uint32 flags) {
  (void)flags;
  gInitialised = true;
  PortLog("SDL subset init (Android/EGL)");
  return 0;
}

void SDL_Quit(void) {
  // The GL device is deliberately kept alive until the next
  // SDL_GL_DeleteContext: the port runs scene 2 and scene 4 back to back in one
  // process. gInitialised only guards input polling.
  gInitialised = false;
}

int SDL_GL_SetAttribute(int attr, int value) {
  switch (attr) {
    case SDL_GL_DEPTH_SIZE:
      gDepthRequest = value;
      return 0;
    case SDL_GL_MULTISAMPLESAMPLES:
    case SDL_GL_SAMPLES:
      gMsaaRequest = value;
      return 0;
    case SDL_GL_MULTISAMPLEBUFFERS:
      if (value == 0) gMsaaRequest = 0;
      return 0;
    default:
      // Context version, profile mask, double buffering: Android has exactly one
      // answer for all of them and it is not the 3.3 core the scenes ask for, so
      // the request is accepted and ignored (see the file header).
      return 0;
  }
}

int SDL_GL_GetAttribute(int attr, int *value) {
  if (!value) return -1;
  switch (attr) {
    case SDL_GL_DEPTH_SIZE:
      *value = gDepthRequest;
      return 0;
    case SDL_GL_SAMPLES:
    case SDL_GL_MULTISAMPLESAMPLES:
      *value = gActualSamples;
      return 0;
    default:
      *value = 0;
      return 0;
  }
}

SDL_GLContext SDL_GL_CreateContext(SDL_Window *window) {
  if (!window) {
    SetError("no window");
    return nullptr;
  }
  if (!gNativeWindow) {
    SetError("the activity has no window yet");
    return nullptr;
  }
  PortLog("GL context: create (depth=%d, msaa request=%d, display %s)", gDepthRequest,
          gMsaaRequest, gDisplay == EGL_NO_DISPLAY ? "not initialised" : "in use");
  if (gContext == EGL_NO_CONTEXT && !InitGL()) return nullptr;
  if (!EnsureSurface()) return nullptr;
  PortLog("GL ready: %s | %s", (const char *)glGetString(GL_VERSION),
          (const char *)glGetString(GL_RENDERER));
  if (!gValidated) {
    // Once per process, on the first context: the device report and the
    // per-program verdict answer the same question every run asks ("which of
    // these shaders does this GPU accept?" and "what is this GPU?").
    gValidated = true;
    LogGlCapabilities();
    ValidateShaderPrograms();
  }
  return (SDL_GLContext)(void *)&gWindow;
}

void SDL_GL_DeleteContext(SDL_GLContext context) {
  (void)context;
  DestroyGL();
}

void SDL_GL_SwapWindow(SDL_Window *window) {
  (void)window;
  PumpEvents(0);
  if (gNativeWindow) CheckWindowSize();

  if (gSurface == EGL_NO_SURFACE) {
    EnsureSurface();
    if (gSurface == EGL_NO_SURFACE) {
      // No window right now (paused, or the activity is being torn down). Block
      // on the event loop instead of spinning: INIT_WINDOW wakes us, and a
      // destroyed activity sets gQuitRequested through PumpEvents.
      PumpEvents(-1);
      if (!gQuitRequested) EnsureSurface();
      if (gSurface == EGL_NO_SURFACE) return;
    }
  }

  CaptureDue();

  // Drain whatever the frame raised. The decoded name matters: a draw the driver
  // rejected is otherwise indistinguishable from a draw that produced nothing,
  // which is exactly how scene 2's missing sea looked from the outside.
  for (int i = 0; i < 4; i++) {
    const GLenum err = glGetError();
    if (err == GL_NO_ERROR) break;
    if (gGlErrorsLogged < 8) {
      gGlErrorsLogged++;
      PortLog("GL error %s (0x%04x) around frame %lu", GlErrorName(err), (unsigned)err,
              gFrameTotal + 1);
    }
  }

  if (eglSwapBuffers(gDisplay, gSurface) != EGL_TRUE) {
    // The surface is gone even though we thought it was current. Hand it back
    // and let the next frame rebuild it.
    PortLog("eglSwapBuffers failed: 0x%04x", eglGetError());
    DestroySurface();
    return;
  }
  gFrameTotal++;
  if (!gSceneFirstFrameLogged) {
    gSceneFirstFrameLogged = true;
    PortLog("first presented frame: %s (frame %lu)", gSceneLabel, gFrameTotal);
  }
}

int SDL_GL_SetSwapInterval(int interval) {
  // The scenes ask for 0 (unsynchronised) both times. Android's compositor is
  // vblank-driven, so the request cannot be honoured; the swap stays synced and
  // the call still succeeds, which is what keeps the benchmark running.
  (void)interval;
  if (gDisplay != EGL_NO_DISPLAY && gSurface != EGL_NO_SURFACE) {
    eglSwapInterval(gDisplay, 1);
  }
  return 0;
}

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags) {
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  if (title) std::snprintf(gWindow.title, sizeof(gWindow.title), "%s", title);
  gWindow.flags = flags;
  if (gApp && gApp->window) gNativeWindow = gApp->window;
  if (gNativeWindow) {
    const int sw = ANativeWindow_getWidth(gNativeWindow);
    const int sh = ANativeWindow_getHeight(gNativeWindow);
    gWindow.width = sw > 0 ? sw : w;
    gWindow.height = sh > 0 ? sh : h;
  } else {
    gWindow.width = w;
    gWindow.height = h;
  }
  // No attribute snapshot here on purpose: the scenes set SDL_GL_* before this
  // call and SDL_GL_CreateContext reads the same globals, which is also what
  // makes scene 4's "retry without MSAA" pass work (it clears the request with
  // SDL_GL_SetAttribute(SDL_GL_SAMPLES, 0) before creating the window again).
  return &gWindow;
}

void SDL_DestroyWindow(SDL_Window *window) {
  (void)window;
  // The activity owns the ANativeWindow: nothing to release, and the GL device
  // stays up until SDL_GL_DeleteContext.
}

void SDL_SetWindowTitle(SDL_Window *window, const char *title) {
  // The FPS the scenes put in the title has nowhere to go on Android; the same
  // numbers are in stdout.txt and in the shot log lines.
  if (window && title) std::snprintf(window->title, sizeof(window->title), "%s", title);
}

void SDL_GetWindowSize(SDL_Window *window, int *w, int *h) {
  (void)window;
  if (w) *w = gWindow.width;
  if (h) *h = gWindow.height;
}

Uint32 SDL_GetWindowFlags(SDL_Window *window) { return window ? window->flags : gWindow.flags; }

int SDL_PollEvent(SDL_Event *event) {
  if (!event) return 0;
  if (PopEvent(event)) return 1;
  if (!gInitialised) return 0;
  PumpEvents(0);
  if (PopEvent(event)) return 1;
  return 0;
}

const char *SDL_GetError(void) { return gError; }

Uint64 SDL_GetPerformanceCounter(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (Uint64)ts.tv_sec * 1000000000ull + (Uint64)ts.tv_nsec;
}

Uint64 SDL_GetPerformanceFrequency(void) { return 1000000000ull; }

void SDL_Delay(Uint32 ms) { usleep(ms * 1000); }

void SDL_free(void *mem) { std::free(mem); }

char *SDL_GetBasePath(void) {
  // The scenes' resolveAssetPath() tries CWD-relative first (meaningless here,
  // the CWD is "/"), then <base>/../<path>, then <base>/<path>. Answering with
  // the app's files directory is what makes the last variant work: android_main
  // unpacks the APK's assets/ tree into exactly that directory, keeping the
  // "shaders/..." layout the scenes ask for.
  const char *base = gInternalDir[0] ? gInternalDir : ".";
  char *copy = (char *)std::malloc(std::strlen(base) + 2);
  if (copy) {
    std::strcpy(copy, base);
    std::strcat(copy, "/");
  }
  return copy;
}

}  // extern "C"
