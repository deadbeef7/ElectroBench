// ===========================================================================
// SDL2 subset implementation for the ElectroBench PS Vita port (see
// vitaport/include/SDL2/SDL.h for what this is and why it is not SDL).
//
//   window + GL context  -> vitaGL (vglInitExtended / vglSwapBuffers)
//   input                -> SceCtrl pad, edge detected
//   clock                -> sceKernelGetSystemTimeWide (microseconds, monotonic)
//
// The Vita has a single 960x544 panel, so the window is created once and every
// size query answers with the panel size: the scenes then size their viewport,
// projection and HUD from that, which is what makes the port run at native
// resolution instead of rendering 1280x720 into a downscaled surface.
// ===========================================================================
#include <SDL2/SDL.h>

#include <vitaGL.h>

#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <psp2/kernel/sysmem.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Trace hook mirroring vita_main.cxx's vitaFAT-flushed log: the shim's init
// diagnostics must also survive a hard exit, so they land in the same
// ux0:/data/ELEB00002/spinlog.txt (first user appends every later line).
void VitaLog(const char *fmt, ...);
static void PortTrace(const char *fmt, ...) {
  char buf[384];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  VitaLog("%s", buf);
}

// vitaGL bridge (see vitaport/tools/build_deps.sh): our libvitaGL is built with
// LOG_ERRORS=1 but with vgl_log redirected to this symbol, so every internal
// diagnostic (GXM/sceGxmVsh init, shader patcher, per-pool allocs) lands in the
// same persistent spinlog instead of the volatile VitaShell console.
extern "C" void PRX_PRINTF_PROXY(const char *fmt, ...) {
  char buf[384];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  VitaLog("%s", buf);
}

// SDL.h only forward declares this; the one window lives here.
struct SDL_Window {
  int width = 0;
  int height = 0;
  Uint32 flags = 0;
  char title[128] = {0};
};

namespace {

const int kScreenW = 960;
const int kScreenH = 544;

SDL_Window gWindow;

int gDepthBits = 24;
int gMsaaRequest = 0;
int gSwapInterval = 0;
bool gInitialised = false;
bool gGlReady = false;
char gError[256] = {0};

// Frame logging: every 30 swaps one line lands in the persistent ux0:/data log
// (file opened on demand here, mirroring vita_main.cxx), so after a hard exit
// the on-device log says the exact frame count the loop reached.
int gFramesSinceLog = 0;
unsigned long gFrameTotal = 0;

void PortLogFrame(void) {
  gFramesSinceLog++;
  gFrameTotal++;
  if (gFramesSinceLog < 30) return;
  gFramesSinceLog = 0;
  SceUID f = sceIoOpen("ux0:/data/ELEB00002/frames.txt",
                       SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (f < 0) return;  // log dir may not exist yet; scene log lines create it
  char line[64];
  std::snprintf(line, sizeof(line), "swapped %lu frames\n", gFrameTotal);
  sceIoWrite(f, line, std::strlen(line));
  sceIoClose(f);
}

// Pad state for edge detection. gPrevValid stays false until the first poll so
// that a button held from launch (e.g. L, which picks the starting scene) is not
// reported as a fresh press by the first frame.
unsigned int gPrevButtons = 0;
bool gPrevValid = false;
bool gQuitRequested = false;
SDL_Event gPending[4];
int gPendingHead = 0;
int gPendingCount = 0;

void PushEvent(const SDL_Event &e) {
  if (gPendingCount >= 4) return;
  gPending[(gPendingHead + gPendingCount) % 4] = e;
  gPendingCount++;
}

bool PopEvent(SDL_Event *out) {
  if (!gPendingCount) return false;
  *out = gPending[gPendingHead];
  gPendingHead = (gPendingHead + 1) % 4;
  gPendingCount--;
  return true;
}

void SetError(const char *msg) {
  std::snprintf(gError, sizeof(gError), "%s", msg);
}

}  // namespace

extern "C" {

int SDL_Init(Uint32 flags) {
  (void)flags;
  // Analog sampling so the pad gives us the full button set; raise the clocks
  // to the standard 444/222 homebrew profile (the default 333/166 would cap the
  // scenes well below 60 fps).
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
  // Standard high-performance homebrew profile (ARM 444 / bus 222 / GPU 222).
  // The scenes are fill-rate bound, so the default 333/166 would cost frames.
  scePowerSetArmClockFrequency(444);
  scePowerSetBusClockFrequency(222);
  scePowerSetGpuClockFrequency(222);
  gInitialised = true;
  return 0;
}

void SDL_Quit(void) {
  // The GL device is intentionally kept alive across scene changes: the port
  // runs scene 2 and scene 4 back to back in one process, and tearing GXM down
  // between them would cost a full re-init. gInitialised only guards pad use.
  gInitialised = false;
}

int SDL_GL_SetAttribute(int attr, int value) {
  switch (attr) {
    case SDL_GL_DEPTH_SIZE:
      gDepthBits = value;
      return 0;
    case SDL_GL_MULTISAMPLESAMPLES:
    case SDL_GL_SAMPLES:
      gMsaaRequest = value;
      return 0;
    case SDL_GL_MULTISAMPLEBUFFERS:
      if (value == 0) gMsaaRequest = 0;
      return 0;
    default:
      // Context version, profile mask, double buffering: the Vita has exactly
      // one answer for all of them, accepted here so the scene code runs on.
      return 0;
  }
}

int SDL_GL_GetAttribute(int attr, int *value) {
  if (!value) return -1;
  switch (attr) {
    case SDL_GL_DEPTH_SIZE:
      *value = gDepthBits;
      return 0;
    case SDL_GL_SAMPLES:
    case SDL_GL_MULTISAMPLESAMPLES:
      *value = gMsaaRequest >= 4 ? 4 : (gMsaaRequest >= 2 ? 2 : 0);
      return 0;
    default:
      *value = 0;
      return 0;
  }
}

SDL_GLContext SDL_GL_CreateContext(SDL_Window *window) {
  (void)window;
  if (!gGlReady) {
    SceGxmMultisampleMode msaa = SCE_GXM_MULTISAMPLE_NONE;
    if (gMsaaRequest >= 4)
      msaa = SCE_GXM_MULTISAMPLE_4X;
    else if (gMsaaRequest >= 2)
      msaa = SCE_GXM_MULTISAMPLE_2X;

    // The one attempt that matters: vglInit* returns exactly
    //   "already inited"  -> FALSE (never happens for us: guarded by gGlReady)
    //   "downscaled res"  -> res_fallback, FALSE when >panel  (we are 960x544)
    // so a FALSE here means the on-device work inside init fell apart. Trace
    // the whole ladder + the free-memory numbers into the persistent log so a
    // single run identifies the failing piece without a reboot dance.
    SceKernelFreeMemorySizeInfo mem;
    mem.size = sizeof(mem);
    sceKernelGetFreeMemorySize(&mem);
    PortTrace("vgl init attempt: msaa=%d free user=%u cdram=%u phycont=%u",
              (int)msaa, mem.size_user, mem.size_cdram, mem.size_phycont);

    // Cached-memory profile first: vglUseCachedMem before init is the config
    // the vitaGL samples/game ports are actually shipped with. Its value only
    // matters at vgl_mem_init time (inside the first vglInit*) so setting it
    // here, just before, is the only correct place.
    vglUseCachedMem(GL_TRUE);

    // Ladder of init calls, most complete first. Each entry names itself in
    // the log, so whichever one flips us to success is on record and the port
    // can ship just that one and stop burning time on init side-effects.
    struct InitStep {
      const char *name;
      SceGxmMultisampleMode msaa;
      int ram_threshold;
    };
    const InitStep ladder[] = {
        {"vglInitExtended(4MB,cached)", msaa, 4 * 1024 * 1024},
        {"vglInitExtended(cached,no-msaa)", SCE_GXM_MULTISAMPLE_NONE,
         4 * 1024 * 1024},
        {"vglInitExtended(cached,16MB-no-msaa)", SCE_GXM_MULTISAMPLE_NONE,
         16 * 1024 * 1024},
    };
    GLboolean ok = GL_FALSE;
    const char *used = nullptr;
    for (const InitStep &step : ladder) {
      // vglInit* return value is NOT a success flag: it is vitaGL's internal
      // res_fallback, TRUE only when the requested resolution had to be
      // downscaled to the panel. A successful init at the native 960x544
      // returns GL_FALSE -- the one emu run that "failed" on every ladder
      // entry actually inited fine on the first call (vgl_inited flipped to
      // TRUE: every retry got "Suppressed ... already inited"). The real
      // success test is whether the memory pools came up.
      GLboolean ret = vglInitExtended(0, kScreenW, kScreenH,
                                      step.ram_threshold, step.msaa);
      (void)ret;
      // vglMemTotal > 0 means the pool memblocks exist and were mapped; the
      // mspace free count can legitimately read 0 right after init (capacity
      // is lazily established on first allocation), so free-space probing
      // under-reports a working init in Vita3K.
      if (vglMemTotal(VGL_MEM_RAM) > 0 || vglMemTotal(VGL_MEM_VRAM) > 0) {
        ok = GL_TRUE;
        used = step.name;
        break;
      }
      PortTrace("vgl init failed at %s", step.name);
      used = step.name;
    }
    if (!ok) {
      // vitaGL's vgl_inited flag flips TRUE inside the first init even when a
      // later stage fails, so retrying against the same lib state is futile
      // ("Suppressed ... already inited"). Report the failure but do not hard
      // block scene 2 before scene 4 ever got a chance: scene 4 runs with
      // lower mem and succeeds on the second ladder pass, so the port treats
      // the two scenes as independent init attempts guarded by gGlReady.
      SetError("vglInitExtended failed");
      PortTrace("vgl init gave up (all ladder entries failed)");
      return nullptr;
    }
    PortTrace("vgl init ok via %s", used);
    gGlReady = true;
  }
  return (SDL_GLContext)(void *)&gWindow;
}

void SDL_GL_DeleteContext(SDL_GLContext context) { (void)context; }

void SDL_GL_SwapWindow(SDL_Window *window) {
  (void)window;
  vglSwapBuffers(GL_FALSE);
  PortLogFrame();
}

int SDL_GL_SetSwapInterval(int interval) {
  // The Vita compositor is always vblank-synced; the scenes' request for an
  // unsynced swap cannot be honoured, so record it and keep vsync on.
  gSwapInterval = interval;
  return 0;
}

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags) {
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  if (title) std::snprintf(gWindow.title, sizeof(gWindow.title), "%s", title);
  gWindow.flags = flags;
  gWindow.width = kScreenW;
  gWindow.height = kScreenH;
  return &gWindow;
}

void SDL_DestroyWindow(SDL_Window *window) { (void)window; }

void SDL_SetWindowTitle(SDL_Window *window, const char *title) {
  (void)window;
  (void)title;
}

void SDL_GetWindowSize(SDL_Window *window, int *w, int *h) {
  (void)window;
  if (w) *w = gWindow.width;
  if (h) *h = gWindow.height;
}

Uint32 SDL_GetWindowFlags(SDL_Window *window) {
  return window ? window->flags : gWindow.flags;
}

int SDL_PollEvent(SDL_Event *event) {
  if (!event) return 0;
  if (PopEvent(event)) return 1;

  if (!gInitialised) return 0;

  SceCtrlData pad;
  std::memset(&pad, 0, sizeof(pad));
  if (sceCtrlPeekBufferPositive(0, &pad, 1) < 0) return 0;

  if (!gPrevValid) {
    gPrevButtons = pad.buttons;
    gPrevValid = true;
    return 0;
  }
  const unsigned int pressed = pad.buttons & ~gPrevButtons;
  gPrevButtons = pad.buttons;

  if (pressed & SCE_CTRL_START) {
    SDL_Event e;
    std::memset(&e, 0, sizeof(e));
    e.type = SDL_QUIT;
    // START means "leave the app"; SELECT below only ends the current scene, so
    // the port entry point needs to tell the two apart.
    gQuitRequested = true;
    PushEvent(e);
  }
  if (pressed & (SCE_CTRL_SELECT | SCE_CTRL_CIRCLE)) {
    SDL_Event e;
    std::memset(&e, 0, sizeof(e));
    e.type = SDL_KEYDOWN;
    e.key.state = 1;
    e.key.keysym.sym = SDLK_ESCAPE;
    PushEvent(e);
  }

  if (PopEvent(event)) return 1;
  return 0;
}

const char *SDL_GetError(void) { return gError; }

// Port-specific: 1 once the user asked to quit the whole app (START).
int VitaPortQuitRequested(void) { return gQuitRequested ? 1 : 0; }

Uint64 SDL_GetPerformanceCounter(void) {
  return (Uint64)sceKernelGetSystemTimeWide();
}

Uint64 SDL_GetPerformanceFrequency(void) { return 1000000ull; }

void SDL_Delay(Uint32 ms) { sceKernelDelayThread(ms * 1000); }

void SDL_free(void *mem) { std::free(mem); }

char *SDL_GetBasePath(void) {
  // Assets are read straight out of the app0: mount, so the base path is app0:/
  // (the scenes' resolveAssetPath() prepends nothing else on Vita).
  const char *base = "app0:/";
  char *copy = (char *)std::malloc(std::strlen(base) + 1);
  if (copy) std::strcpy(copy, base);
  return copy;
}

}  // extern "C"
