// ===========================================================================
// SDL2 subset implementation for the ElectroBench Android port.
//
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
#include "lodepng.h"
extern struct android_app *gApp;
#ifndef EGL_OPENGL_ES3_BIT
#ifdef EGL_OPENGL_ES3_BIT_KHR
#define EGL_OPENGL_ES3_BIT EGL_OPENGL_ES3_BIT_KHR
#else
#define EGL_OPENGL_ES3_BIT 0x00000040
#endif
#endif
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
int gActualSamples = 0;

EGLDisplay gDisplay = EGL_NO_DISPLAY;
EGLConfig gConfig = nullptr;
EGLContext gContext = EGL_NO_CONTEXT;
EGLSurface gSurface = EGL_NO_SURFACE;
ANativeWindow *gNativeWindow = nullptr;

int gDepthRequest = 24;
int gMsaaRequest = 0;
bool gInitialised = false;
int gEglMajor = 0;
int gEglMinor = 0;


char gError[256] = {0};
char gInternalDir[512] = {0};
char gShareDir[512] = {0};

FILE *gLogFile = nullptr;

unsigned long gFrameTotal = 0;
bool gQuitRequested = false;

SDL_Event gPending[kEventQueue];
int gPendingHead = 0;
int gPendingCount = 0;
char gSceneLabel[32] = "scene";
double gShotTimes[kMaxShots] = {0};
int gShotCount = 0;
int gNextShot = 0;
double gSceneT0 = 0.0;
bool gSceneT0Valid = false;
bool gSceneFirstFrameLogged = false;

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
bool gValidated = false;
int gGlErrorsLogged = 0;

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
      DestroySurface();
      gNativeWindow = nullptr;
      PortLog("window gone; surface released");
      break;
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONTENT_RECT_CHANGED:
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
        PushQuit();
        break;
      case AKEYCODE_ESCAPE:
      case AKEYCODE_ENTER:
      case AKEYCODE_DPAD_CENTER:
      case AKEYCODE_SPACE:
      case AKEYCODE_BUTTON_A:
      case AKEYCODE_BUTTON_START:
        PushKey(SDLK_ESCAPE);
        break;
      default:
        break;
    }
    return 1;
  }
  if (type == AINPUT_EVENT_TYPE_MOTION) {
    if (AMotionEvent_getAction(event) == AMOTION_EVENT_ACTION_DOWN) PushKey(SDLK_ESCAPE);
    return 1;
  }
  return 0;
}

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
      PumpEvents(-1);
      if (!gQuitRequested) EnsureSurface();
      if (gSurface == EGL_NO_SURFACE) return;
    }
  }

  CaptureDue();
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
  return &gWindow;
}

void SDL_DestroyWindow(SDL_Window *window) {
  (void)window;
}

void SDL_SetWindowTitle(SDL_Window *window, const char *title) {
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
  const char *base = gInternalDir[0] ? gInternalDir : ".";
  char *copy = (char *)std::malloc(std::strlen(base) + 2);
  if (copy) {
    std::strcpy(copy, base);
    std::strcat(copy, "/");
  }
  return copy;
}

}  // extern "C"
