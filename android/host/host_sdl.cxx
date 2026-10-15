// ===========================================================================
// SDL2 subset for the host GLES3 check (android/Makefile's host-render target).
//
// Same contract as android/jni/android_sdl.cxx (declared in
// include/SDL2/SDL.h), but the surface is an EGL pbuffer on the host's own
// driver instead of a NativeActivity window:
//
//   surfaceless platform first (no X server needed), the default display
//   second (which is X11 when the Makefile runs us under xvfb-run)
//
// Why this exists: the APK cannot be run here - there is no device, no emulator
// and no KVM in this container - so the port's own pixels are otherwise
// unobservable. On this branch the Vita3K route was a wall (the emulator models
// the shader register bank as 20 vec4 and drops scene 2's sky and sea draws
// entirely; see the README). This harness renders the *same* scene translation
// units and the *same* shader tree through a real OpenGL ES 3 driver, which is
// the API level the Android port targets, and lets the scenes' own screenshot
// path write the frames out. It is therefore a check of the scenes and shaders
// under ES 3.0, not of the Android activity/EGL glue.
//
// Differences that matter, both deliberate:
//   * MSAA is reported as unavailable (0x) rather than requested. The scenes
//     read back the default framebuffer with glReadPixels, which a multisampled
//     default framebuffer cannot answer, and only the Android port owns the
//     blit-to-FBO path that makes that work (android_sdl.cxx WriteShot).
//   * There is no input: the harness runs the scene for a fixed number of
//     simulated seconds and lets the scene's shot path exit the process.
// ===========================================================================
#include <SDL2/SDL.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <sys/types.h>
#include <unistd.h>

struct SDL_Window {
  int width = 0;
  int height = 0;
  Uint32 flags = 0;
  char title[128] = {0};
};

namespace {

SDL_Window gWindow;
EGLDisplay gDisplay = EGL_NO_DISPLAY;
EGLSurface gSurface = EGL_NO_SURFACE;
EGLContext gContext = EGL_NO_CONTEXT;

int gDepthRequest = 24;
int gSamplesReported = 0;
bool gInitialised = false;
char gError[256] = {0};
unsigned long gFrames = 0;

// EGL_PLATFORM_SURFACELESS_MESA: defined in eglext.h only when the host headers
// are new enough, so it is spelled out here rather than relying on them.
const EGLint kPlatformSurfacelessMesa = 0x31DD;

void SetError(const char *msg) { std::snprintf(gError, sizeof(gError), "%s", msg); }

int ScreenW() {
  const char *env = std::getenv("ELECTROBENCH_HOST_SIZE");
  if (env) {
    int w = 0, h = 0;
    if (std::sscanf(env, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) return w;
  }
  return 1366;
}

int ScreenH() {
  const char *env = std::getenv("ELECTROBENCH_HOST_SIZE");
  if (env) {
    int w = 0, h = 0;
    if (std::sscanf(env, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) return h;
  }
  return 768;
}

// Tries one display connection end to end: a pbuffer of the port's size and an
// ES 3.0 context on it. Returns false without leaving a half-built device
// behind, so the caller can try the next platform.
bool TryDisplay(EGLDisplay display, const char *name) {
  if (display == EGL_NO_DISPLAY) return false;
  EGLint major = 0, minor = 0;
  if (eglInitialize(display, &major, &minor) != EGL_TRUE) return false;

  const EGLint configAttribs[] = {
      EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
      EGL_RED_SIZE,        8,
      EGL_GREEN_SIZE,      8,
      EGL_BLUE_SIZE,       8,
      EGL_ALPHA_SIZE,      8,
      EGL_DEPTH_SIZE,      gDepthRequest,
      EGL_NONE,
  };
  EGLConfig config = nullptr;
  EGLint count = 0;
  if (eglChooseConfig(display, configAttribs, &config, 1, &count) != EGL_TRUE || count == 0) {
    eglTerminate(display);
    return false;
  }

  const EGLint surfaceAttribs[] = {
      EGL_WIDTH,  gWindow.width,
      EGL_HEIGHT, gWindow.height,
      EGL_NONE,
  };
  EGLSurface surface = eglCreatePbufferSurface(display, config, surfaceAttribs);
  if (surface == EGL_NO_SURFACE) {
    eglTerminate(display);
    return false;
  }

  const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
  if (context == EGL_NO_CONTEXT) {
    eglDestroySurface(display, surface);
    eglTerminate(display);
    return false;
  }
  if (eglMakeCurrent(display, surface, surface, context) != EGL_TRUE) {
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
    return false;
  }

  gDisplay = display;
  gSurface = surface;
  gContext = context;
  std::printf("host GL: %s (EGL %d.%d) | %s | %s\n", name, (int)major, (int)minor,
              (const char *)glGetString(GL_VERSION), (const char *)glGetString(GL_RENDERER));
  std::fflush(stdout);
  return true;
}

bool InitGL() {
  gWindow.width = ScreenW();
  gWindow.height = ScreenH();

  PFNEGLGETPLATFORMDISPLAYEXTPROC getPlatformDisplay =
      (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
  if (getPlatformDisplay) {
    EGLDisplay surfaceless =
        getPlatformDisplay(kPlatformSurfacelessMesa, EGL_DEFAULT_DISPLAY, nullptr);
    if (TryDisplay(surfaceless, "surfaceless")) return true;
  }
  // The default display is X11 when DISPLAY is set (xvfb-run does that) and
  // whatever the platform prefers otherwise.
  if (TryDisplay(eglGetDisplay(EGL_DEFAULT_DISPLAY), "default/X11")) return true;

  SetError("no EGL display offered an OpenGL ES 3.0 pbuffer config");
  return false;
}

double NowSeconds() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

}  // namespace

extern "C" {

int SDL_Init(Uint32 flags) {
  (void)flags;
  gInitialised = true;
  return 0;
}

void SDL_Quit(void) { gInitialised = false; }

int SDL_GL_SetAttribute(int attr, int value) {
  if (attr == SDL_GL_DEPTH_SIZE) gDepthRequest = value;
  // Everything else (context version, profile mask, MSAA, double buffering) is
  // accepted and ignored, exactly as in the Vita and Android shims. MSAA in
  // particular is NOT honoured: see the file header.
  return 0;
}

int SDL_GL_GetAttribute(int attr, int *value) {
  if (!value) return -1;
  switch (attr) {
    case SDL_GL_DEPTH_SIZE:
      *value = gDepthRequest;
      return 0;
    case SDL_GL_SAMPLES:
    case SDL_GL_MULTISAMPLESAMPLES:
      *value = gSamplesReported;
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
  if (gContext == EGL_NO_CONTEXT && !InitGL()) return nullptr;
  return (SDL_GLContext)(void *)&gWindow;
}

void SDL_GL_DeleteContext(SDL_GLContext context) { (void)context; }

void SDL_GL_SwapWindow(SDL_Window *window) {
  (void)window;
  if (gDisplay != EGL_NO_DISPLAY && gSurface != EGL_NO_SURFACE) eglSwapBuffers(gDisplay, gSurface);
  gFrames++;
  // One line every 30 presented frames: a software rasteriser at 1366x768 runs
  // these fragment shaders slowly, and a silent log looks like a hang.
  if (gFrames % 30 == 0) {
    std::printf("host frames: %lu\n", gFrames);
    std::fflush(stdout);
  }
}

int SDL_GL_SetSwapInterval(int interval) {
  (void)interval;
  if (gDisplay != EGL_NO_DISPLAY && gSurface != EGL_NO_SURFACE) eglSwapInterval(gDisplay, 0);
  return 0;
}

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags) {
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  // The scenes only keep their own size when gWindowedMode is true; it is false
  // here, so this is the size they render at - the same contract as the Vita
  // panel and the Android surface.
  gWindow.width = ScreenW();
  gWindow.height = ScreenH();
  gWindow.flags = flags;
  if (title) std::snprintf(gWindow.title, sizeof(gWindow.title), "%s", title);
  return &gWindow;
}

void SDL_DestroyWindow(SDL_Window *window) { (void)window; }

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
  (void)event;
  // No input on the host: the scene ends by itself (benchmark + results screen)
  // or through its own shot path, which exits the process.
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
  // resolveAssetPath() tries the current directory first, and the harness is
  // built from the repository root, so this is only the fallback: the directory
  // holding the harness binary's parent is tried before <base>/<path>, and the
  // binary lives in build-android/host/.
  char link[4096] = {0};
  const ssize_t n = readlink("/proc/self/exe", link, sizeof(link) - 1);
  const std::string dir = n > 0 ? std::string(link, (size_t)n) : std::string(".");
  const size_t slash = dir.find_last_of('/');
  const std::string base = (slash == std::string::npos ? std::string(".") : dir.substr(0, slash));
  char *copy = (char *)std::malloc(base.size() + 2);
  if (copy) {
    std::memcpy(copy, base.c_str(), base.size());
    copy[base.size()] = '/';
    copy[base.size() + 1] = '\0';
  }
  return copy;
}

}  // extern "C"
