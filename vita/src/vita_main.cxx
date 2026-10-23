// ===========================================================================
// ElectroBench on the PS Vita: entry point for the port that runs SCENE 2
// (dusk ocean) and SCENE 4 (power lines / "lain") and nothing else.
//
#include <vitasdk.h>

#include <psp2/io/fcntl.h>
#include <psp2/kernel/sysmem.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int RunOceanScene(bool *gaveUpOut);  // scene 2 (GL 3.3 dusk ocean)
int RunPoleScene(bool *gaveUpOut);   // scene 4 (GL 3.3 power lines)

void OceanSceneSetShotTime(float t);
void PoleSceneSetScreenshot(const char *path);
void PoleSceneSetShotTime(float t);
bool gWindowedMode = false;
extern "C" int VitaPortQuitRequested(void);

namespace {

int gLogFile = -1;
bool gLogTruncateNext = true;

void LogOpenLocked(void) {
  if (gLogFile >= 0) return;
  sceIoMkdir("ux0:/data/ELEB00002", 0777);
  unsigned int flags = SCE_O_WRONLY | SCE_O_CREAT |
                       (gLogTruncateNext ? SCE_O_TRUNC : SCE_O_APPEND);
  gLogTruncateNext = false;
  gLogFile = sceIoOpen("ux0:/data/ELEB00002/spinlog.txt", flags, 0777);
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

void ApplyLaunchSpec(const char *spec, bool *startWithPoles) {
  if (!spec || !spec[0]) return;
  if (std::strstr(spec, "start=4")) *startWithPoles = true;

  const char *shot2 = std::strstr(spec, "shot2=");
  const char *shot4 = std::strstr(spec, "shot4=");
  if (shot2) {
    OceanSceneSetScreenshot("ux0:/data/ELEB00002/vita_shot_scene2.ppm");
    OceanSceneSetShotTime((float)std::atof(shot2 + 6));  // skip "shot2="
    VitaLog("scene 2 screenshot scheduled at t=%.1f", std::atof(shot2 + 6));
  }
  if (shot4) {
    PoleSceneSetScreenshot("ux0:/data/ELEB00002/vita_shot_scene4.ppm");
    PoleSceneSetShotTime((float)std::atof(shot4 + 6));  // skip "shot4="
    VitaLog("scene 4 screenshot scheduled at t=%.1f", std::atof(shot4 + 6));
  }
}

bool StartWithPolesFromLaunch(void) {
  bool startWithPoles = false;

  char param[1024] = {0};
  if (sceAppMgrGetAppParam(param) >= 0 && param[0]) {
    VitaLog("app param: '%s'", param);
    ApplyLaunchSpec(param, &startWithPoles);
  }

  const int fd = sceIoOpen("ux0:/data/ELEB00002/autoshot.txt", SCE_O_RDONLY, 0);
  if (fd >= 0) {
    char file[256] = {0};
    const int n = sceIoRead(fd, file, sizeof(file) - 1);
    sceIoClose(fd);
    if (n > 0) {
      file[n] = '\0';
      for (int i = 0; i < n; i++)
        if (file[i] == '\n' || file[i] == '\r') file[i] = ' ';
      VitaLog("autoshot spec: '%s'", file);
      ApplyLaunchSpec(file, &startWithPoles);
    }
  }
  return startWithPoles;
}

}  // namespace

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  const bool startWithPoles = LTriggerHeld() || StartWithPolesFromLaunch();
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
