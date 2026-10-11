// ===========================================================================
// ElectroBench on the PS Vita: entry point for the port that runs SCENE 2
// (dusk ocean) and SCENE 4 (power lines / "lain") and nothing else.
//
// The two scene translation units are the desktop ones, compiled unchanged
// against vitaport/include (SDL2 + GLEW stand-ins) and linked against vitaGL.
// This file replaces main.cxx: it owns the process, decides which scene runs
// first, and keeps the vitaGL device alive across the hand-over so that
// switching scenes does not re-initialise GXM.
//
// Controls
//   hold L at launch   start with scene 4 instead of scene 2
//   SELECT / CIRCLE    end the current scene and go to the next one
//   START              quit the app
//
// Scene order is a loop of two: scene 2 then scene 4 (or scene 4 then scene 2
// when L is held), and the app exits once the last scene ends.
// ===========================================================================
#include <vitasdk.h>

#include <psp2/io/fcntl.h>
#include <psp2/kernel/sysmem.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

// --------------------------------------------------------------- scene entries
// Same signatures main.cxx declares for the desktop build.
int RunOceanScene(bool *gaveUpOut);  // scene 2 (GL 3.3 dusk ocean)
int RunPoleScene(bool *gaveUpOut);   // scene 4 (GL 3.3 power lines)

// Defined by the desktop main.cxx, which is NOT part of the Vita build. The
// scenes read it to decide whether to trust their own requested window size or
// the size the platform reports back: false means "adopt the real surface",
// which is what makes both scenes render at the Vita's native 960x544.
bool gWindowedMode = false;

// From vitaport/src/vita_sdl.cxx: set when the pad asked for a real quit
// (START) rather than just the end of the current scene (SELECT).
extern "C" int VitaPortQuitRequested(void);

// Persistent crash log: the VitaShell console cannot be read after the process
// dies, so VitaLog also appends to ux0:/data/ELEB00002/spinlog.txt. Homebrew can
// write ux0:/data without extra permissions, and the file survives a crash —
// pull it out of VitaShell after the run and the last logged line says how far
// the port got.
namespace {

int gLogFile = -1;

void LogOpenLocked(void) {
  if (gLogFile >= 0) return;
  sceIoMkdir("ux0:/data/ELEB00002", 0777);
  gLogFile = sceIoOpen("ux0:/data/ELEB00002/spinlog.txt",
                       SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
}

void LogLine(const char *line) {
  sceClibPrintf("%s\n", line);
  if (gLogFile >= 0) {
    sceIoWrite(gLogFile, line, std::strlen(line));
    sceIoWrite(gLogFile, "\n", 1);
  }
}

void LogClose(void) {
  if (gLogFile >= 0) {
    sceIoClose(gLogFile);
    gLogFile = -1;
  }
}

}  // namespace

void VitaLog(const char *fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  LogOpenLocked();
  LogLine(buf);
  // Flush to disk immediately: the process may die right after this line, and
  // the file is the only trace that survives.
  if (gLogFile >= 0) {
    sceIoClose(gLogFile);
    gLogFile = -1;
  }
}

namespace {

bool LTriggerHeld(void) {
  SceCtrlData pad;
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
  if (sceCtrlPeekBufferPositive(0, &pad, 1) < 0) return false;
  return (pad.buttons & (SCE_CTRL_LTRIGGER | SCE_CTRL_L2)) != 0;
}

}  // namespace

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  const bool startWithPoles = LTriggerHeld();
  VitaLog("ElectroBench (Vita port): scenes 2 and 4, starting with scene %d",
          startWithPoles ? 4 : 2);

  const int order[2] = {startWithPoles ? 4 : 2, startWithPoles ? 2 : 4};
  for (int i = 0; i < 2; i++) {
    bool gaveUp = false;
    const int scene = order[i];
    VitaLog("--- scene %d ---", scene);
    const int rc = (scene == 2) ? RunOceanScene(&gaveUp) : RunPoleScene(&gaveUp);
    VitaLog("scene %d returned %d%s", scene, rc, gaveUp ? " (gave up)" : "");
    if (VitaPortQuitRequested()) break;
  }

  VitaLog("process exit (all scenes done or quit requested)");
  LogClose();
  sceKernelExitProcess(0);
  return 0;
}
