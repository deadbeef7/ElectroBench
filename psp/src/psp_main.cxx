// ElectroBench — PlayStation Portable port: application shell.
//
// Two scenes (2, the dusk ocean, then 4, the power-line corridor) driven
// through the Graphics Engine, 45 s each, the same order and run length as the
// Vita and Android builds:
//   * 480x272 16-bit framebuffer + 16-bit depth, both in the 2 MB EDRAM, double
//     buffered through sceGuSwapBuffers
//   * two 5650 buffers and the depth buffer live at EDRAM offsets 0 / 512 KiB
//   * the GE's blend units carry the reflection and glitter passes additively,
//     which is how the fixed function pipeline gets the shader's mix()
//
// HUD and results use the same 8x8 bitmap font as the Vita/Android ports
// (src/font_atlas.hxx), so all three builds print identical text.
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspctrl.h>
#include <psppower.h>
#include <psprtc.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "font_atlas.hxx"
#include "psp_scene2.hxx"
#include "psp_scene4.hxx"

PSP_MODULE_INFO("ElectroBench", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

namespace {

constexpr int kScreenW = 480;
constexpr int kScreenH = 272;
constexpr int kBufWidth = (int)kPspBufWidth;      // POT stride for the GE
constexpr int kFrameBytes = (int)kPspFrameBytes;
constexpr unsigned int kDrawOffset = kPspDrawVram;
constexpr unsigned int kDispOffset = kPspDispVram;
constexpr unsigned int kDepthOffset = kPspDepthVram;
constexpr int kBenchMs = 45000;                   // same 45 s run as the original
constexpr int kResultsMs = 6000;                  // then hand over to the next scene
unsigned int gList[524288] __attribute__((aligned(16))); // 2 MB
unsigned short gFontTex[kFontAtlasW * kFontAtlasH] __attribute__((aligned(16)));
bool gFontTexDirty = true;

struct HudVertex { float u, v; unsigned int color; float x, y, z; };

bool gRunning = true;

int ExitCallback(int, int, void *) {
  gRunning = false;
  sceKernelExitGame();
  return 0;
}

// The callback lives on its own thread: sceKernelSleepThreadCB() blocks the
// caller until a callback fires, so it must never run on the render thread.
int CallbackThread(SceSize, void *) {
  int id = sceKernelCreateCallback("exit", ExitCallback, nullptr);
  sceKernelRegisterExitCallback(id);
  sceKernelSleepThreadCB();
  return 0;
}

void SetupCallbacks() {
  int thid = sceKernelCreateThread("exit_cb", CallbackThread, 0x11, 0xFA0, 0, 0);
  if (thid >= 0) sceKernelStartThread(thid, 0, nullptr);
}

void BuildFontTexture() {
  static unsigned char rgba[kFontAtlasW * kFontAtlasH * 4];
  FontAtlasFillRGBA(rgba, sizeof(rgba));
  for (int i = 0; i < kFontAtlasW * kFontAtlasH; i++) {
    unsigned int a = rgba[i * 4 + 3];
    unsigned int r = rgba[i * 4 + 0];
    unsigned int g = rgba[i * 4 + 1];
    unsigned int b = rgba[i * 4 + 2];
    gFontTex[i] = (unsigned short)((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | ((a >= 128) ? 0x8000u : 0));
  }
  gFontTexDirty = true;
}

// -------------------------------------------------------------- HUD drawing
float gHudX = 0.0f, gHudY = 0.0f;
HudVertex gHudVerts[6 * 96] __attribute__((aligned(16)));
int gHudCount = 0;

void HudBegin() { gHudCount = 0; gHudX = 0.0f; gHudY = 0.0f; }

void HudGlyph(int c, float x, float y, float scale, unsigned int color) {
  float uv[4];
  FontAtlasGlyphUV(c, uv);
  if (uv[2] == 0.0f && uv[3] == 0.0f) return;
  // gHudVerts is sized for the longest label this shell prints; clamp instead
  // of trusting every future string to stay inside it (a glyph is 6 vertices).
  if (gHudCount + 6 > (int)(sizeof(gHudVerts) / sizeof(gHudVerts[0]))) return;
  const float w = 8.0f * scale, h = 8.0f * scale;
  float vx = x, vy = y;
  HudVertex *v = &gHudVerts[gHudCount];
  gHudCount += 6;
  // two triangles, CW
  v[0] = {uv[0], uv[1], color, vx,      vy,      0.0f};
  v[1] = {uv[2], uv[1], color, vx + w,  vy,      0.0f};
  v[2] = {uv[2], uv[3], color, vx + w,  vy + h,  0.0f};
  v[3] = {uv[0], uv[1], color, vx,      vy,      0.0f};
  v[4] = {uv[2], uv[3], color, vx + w,  vy + h,  0.0f};
  v[5] = {uv[0], uv[3], color, vx,      vy + h,  0.0f};
}

void HudText(const char *text, float x, float y, float scale, unsigned int color) {
  float pen = x;
  for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
    HudGlyph(*p, pen, y, scale, color);
    pen += 8.0f * scale;
  }
}

void HudDraw() {
  if (gHudCount == 0) return;

  sceGuDisable(GU_DEPTH_TEST);
  sceGuEnable(GU_BLEND);
  sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
  if (gFontTexDirty) {
    sceKernelDcacheWritebackRange(gFontTex, sizeof(gFontTex));
    gFontTexDirty = false;
  }
  sceGuEnable(GU_TEXTURE_2D);
  sceGuTexMode(GU_PSM_5551, 0, 0, GU_FALSE);
  sceGuTexImage(0, kFontAtlasW, kFontAtlasH, kFontAtlasW, (void *)gFontTex);
  sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
  sceGuTexFilter(GU_NEAREST, GU_NEAREST);
  sceGuTexWrap(GU_CLAMP, GU_CLAMP);

  sceGumMatrixMode(GU_PROJECTION);
  sceGumLoadIdentity();
  sceGumOrtho(0.0f, (float)kScreenW, (float)kScreenH, 0.0f, -1.0f, 1.0f);
  sceGumMatrixMode(GU_VIEW);
  sceGumLoadIdentity();
  sceGumUpdateMatrix();

  sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF,
                 gHudCount, nullptr, gHudVerts);
  sceGuDisable(GU_TEXTURE_2D);
  sceGuDisable(GU_BLEND);
  sceGuDepthMask(GU_FALSE);
  sceGuEnable(GU_DEPTH_TEST);
}

inline unsigned int White() { return 0xFFFFFFFF; }
inline unsigned int Warm() { return 0xFFA0C8FF; } // ABGR: r=ff g=c8 b=a0

// ---------------------------------------------------------------- GU setup
void GuInit() {
  sceGuInit();
  sceGuStart(GU_DIRECT, gList);
  sceGuDrawBuffer(GU_PSM_5650, (void *)kDrawOffset, kBufWidth);
  sceGuDispBuffer(kScreenW, kScreenH, (void *)kDispOffset, kBufWidth);
  sceGuDepthBuffer((void *)kDepthOffset, kBufWidth);
  sceGuOffset(2048 - kScreenW / 2, 2048 - kScreenH / 2);
  sceGuViewport(2048, 2048, kScreenW, kScreenH);
  sceGuScissor(0, 0, kScreenW, kScreenH);
  sceGuEnable(GU_SCISSOR_TEST);
  sceGuDepthRange(65535, 0);
  sceGuDepthFunc(GU_GEQUAL);
  sceGuEnable(GU_DEPTH_TEST);
  sceGuEnable(GU_CLIP_PLANES);
  sceGuFrontFace(GU_CW);
  sceGuDisable(GU_CULL_FACE);
  sceGuShadeModel(GU_SMOOTH);
  sceGuTexEnvColor(0xFFFFFFFF);
  sceGumMatrixMode(GU_MODEL);
  sceGumLoadIdentity();
  sceGumUpdateMatrix();
  sceGuFinish();
  sceGuSync(0, 0);
  sceDisplayWaitVblankStart();
  sceGuDisplay(1);
}

void SetSceneMatrix(const float eye[3], float yaw, float pitch, float fovDeg,
                    float nearZ, float farZ) {
  const float fov = fovDeg * 3.14159265f / 180.0f;
  const float f = 1.0f / std::tan(fov * 0.5f);
  ScePspFMatrix4 proj;
  std::memset(&proj, 0, sizeof(proj));
  proj.x.x = f / ((float)kScreenW / (float)kScreenH);
  proj.y.y = f;
  proj.z.z = farZ / (farZ - nearZ);
  proj.z.w = 1.0f;
  proj.w.z = -farZ * nearZ / (farZ - nearZ);

  // Look-at, +Z forward.
  const float cy = std::cos(yaw), sy = std::sin(yaw);
  const float cp = std::cos(pitch), sp = std::sin(pitch);
  const float fwd[3] = {sy * cp, sp, cy * cp};
  const float horiz = std::sqrt(fwd[0] * fwd[0] + fwd[2] * fwd[2]);
  const float right[3] = {horiz > 1e-5f ? fwd[2] / horiz : 1.0f, 0.0f,
                          horiz > 1e-5f ? -fwd[0] / horiz : 0.0f};
  const float up[3] = {fwd[1] * right[2] - fwd[2] * right[1],
                       fwd[2] * right[0] - fwd[0] * right[2],
                       fwd[0] * right[1] - fwd[1] * right[0]};
  ScePspFMatrix4 view;
  std::memset(&view, 0, sizeof(view));
  view.x.x = right[0]; view.x.y = up[0]; view.x.z = fwd[0];
  view.y.x = right[1]; view.y.y = up[1]; view.y.z = fwd[1];
  view.z.x = right[2]; view.z.y = up[2]; view.z.z = fwd[2];
  view.w.x = -(right[0] * eye[0] + right[1] * eye[1] + right[2] * eye[2]);
  view.w.y = -(up[0] * eye[0] + up[1] * eye[1] + up[2] * eye[2]);
  view.w.z = -(fwd[0] * eye[0] + fwd[1] * eye[1] + fwd[2] * eye[2]);
  view.w.w = 1.0f;

  ScePspFMatrix4 ident;
  std::memset(&ident, 0, sizeof(ident));
  ident.x.x = 1.0f; ident.y.y = 1.0f; ident.z.z = 1.0f; ident.w.w = 1.0f;

  sceGuSetMatrix(GU_PROJECTION, &proj);
  sceGuSetMatrix(GU_VIEW, &view);
  sceGuSetMatrix(GU_MODEL, &ident);
}

void SetSkyMatrix() {
  sceGumMatrixMode(GU_PROJECTION);
  sceGumLoadIdentity();
  sceGumOrtho(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f);
  sceGumMatrixMode(GU_VIEW);
  sceGumLoadIdentity();
  sceGumUpdateMatrix();
}

namespace {

double gRtcRes = 1000000.0;   
bool gRtcFine = false;      
bool gClockReady = false;

double gRate = 1.0;           
bool gRateMeasured = false;

double gUptime = 0.0;        
u64 gKernPrev = 0;
u64 gCalKern = 0;
u64 gCalRtc = 0;

inline u64 RtcTick() {
  u64 tick = 0;
  return sceRtcGetCurrentTick(&tick) == 0 ? tick : 0;
}
double MeasuredRate(u64 kernUs) {
  if (!gRtcFine) return -1.0;
  const double kernSpan = (double)(kernUs - gCalKern) * 1e-6;
  if (kernSpan < 0.20) return -1.0;
  const double rtcSpan = (double)(RtcTick() - gCalRtc) / gRtcRes;
  if (rtcSpan <= 0.0) return -1.0;
  const double rate = rtcSpan / kernSpan;
  return (rate > 0.2 && rate < 5.0) ? rate : -1.0;
}

}  // namespace

// Called once, before the first frame. Costs about 200 ms of boot time.
void ClockInit() {
  const u32 res = sceRtcGetTickResolution();
  gRtcRes = res > 0 ? (double)res : 1000000.0;
  const u64 rtc0 = RtcTick();
  sceKernelDelayThread(8000);
  const u64 rtc1 = RtcTick();
  gRtcFine = rtc1 > rtc0 && (double)(rtc1 - rtc0) / gRtcRes < 0.5;

  gKernPrev = sceKernelGetSystemTimeWide();
  gCalKern = gKernPrev;
  gCalRtc = rtc1;

  sceKernelDelayThread(200000);
  const u64 kernNow = sceKernelGetSystemTimeWide();
  const double rate = MeasuredRate(kernNow);
  if (rate > 0.0) {
    gRate = rate;
    gRateMeasured = true;
  }
  gCalKern = kernNow;
  gCalRtc = RtcTick();
  gKernPrev = kernNow;
  gUptime = 0.0;
  gClockReady = true;
}

double NowSeconds() {
  if (!gClockReady) ClockInit();
  const u64 kern = sceKernelGetSystemTimeWide();
  if (kern > gKernPrev) {
    gUptime += (double)(kern - gKernPrev) * 1e-6 * gRate;
    gKernPrev = kern;
  }
  const double sinceCal = (double)(kern - gCalKern) * 1e-6;
  if (sinceCal >= 4.0) {
    const double rate = MeasuredRate(kern);
    if (rate > 0.0) {
      gRate = gRateMeasured ? gRate * 0.5 + rate * 0.5 : rate;
      gRateMeasured = true;
    }
    gCalKern = kern;
    gCalRtc = RtcTick();
  }
  return gUptime;
}

void ResetDrawState() {
  sceGuDisable(GU_FOG);
  sceGuDisable(GU_BLEND);
  sceGuDisable(GU_TEXTURE_2D);
  sceGuDisable(GU_ALPHA_TEST);
  sceGuDisable(GU_CULL_FACE);
  sceGuEnable(GU_DEPTH_TEST);
  sceGuEnable(GU_CLIP_PLANES);
  sceGuDepthMask(GU_FALSE);
  sceGuDepthFunc(GU_GEQUAL);
}

const char *SceneName(int scene) {
  return scene == 2 ? "scene 2 (dusk ocean)" : "scene 4 (power lines)";
}

struct SceneStat {
  int frames;
  double seconds;
  double fps;
  double worstFrameMs;  // slowest single frame inside the timed run
  int slowFrames;       // frames that took more than 100 ms
};

struct FpsMeter {
  static constexpr int kWindow = 90;
  double stamp[kWindow];
  int head = 0;
  int count = 0;

  void Reset() { head = 0; count = 0; }

  void Sample(double now) {
    stamp[head] = now;
    head = (head + 1) % kWindow;
    if (count < kWindow) count++;
  }

  double Rate() const {
    if (count < 2) return 0.0;
    const double newest = stamp[(head - 1 + kWindow) % kWindow];
    const double oldest = stamp[(head - count + kWindow) % kWindow];
    const double span = newest - oldest;
    return span > 1e-6 ? (double)(count - 1) / span : 0.0;
  }
};

SceneStat RunScene(int scene) {
  SceneStat stat{0, 0.0, 0.0, 0.0, 0};
  const double start = NowSeconds();
  double lastFrameAt = start;
  FpsMeter meter;
  int frames = 0;
  bool results = false;
  bool sceneDone = false;
  double resultsAt = 0.0;

  SceCtrlData pad;

  while (gRunning) {
    const double now = NowSeconds();
    const double t = now - start;

    sceCtrlPeekBufferPositive(&pad, 1);
    if (pad.Buttons & PSP_CTRL_START) {
      gRunning = false;
      break;
    }
    if ((pad.Buttons & PSP_CTRL_SELECT) && !results) {
      results = true;
      resultsAt = now;
      stat.frames = frames;
      stat.seconds = t;
      stat.fps = t > 0.0 ? (double)frames / t : 0.0;
    }

    if (scene == 2) {
      Scene2UpdateCamera(t);
      Scene2Build(t);
    } else {
      Scene4Update(t);
    }

    sceGuStart(GU_DIRECT, gList);
    sceGuClearColor(0xFF1A0D10); // warm dusk black, ABGR
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    ResetDrawState();

    float eye[3], yaw, pitch;
    if (scene == 2) {
      SetSkyMatrix();
      Scene2DrawSky();
      Scene2Camera(eye, &yaw, &pitch);
      SetSceneMatrix(eye, yaw, pitch, Scene2FovY(), 1.0f, 5000.0f);
      Scene2Draw();
    } else {
      SetSkyMatrix();
      Scene4DrawSky();
      Scene4Camera(eye, &yaw, &pitch);
      SetSceneMatrix(eye, yaw, pitch, Scene4FovY(), 0.35f, 950.0f);
      Scene4Draw();
    }

    HudBegin();
    char line[64];
    if (!results) {
      const double frameMs = (now - lastFrameAt) * 1000.0;
      lastFrameAt = now;
      if (frameMs > stat.worstFrameMs) stat.worstFrameMs = frameMs;
      if (frameMs > 100.0) stat.slowFrames++;
      meter.Sample(now);
      const int rate10 = (int)(meter.Rate() * 10.0 + 0.5);
      const int t10 = (int)(t * 10.0 + 0.5);
      snprintf(line, sizeof(line), "FPS: %d.%d", rate10 / 10, rate10 % 10);
      HudText(line, 8.0f, 6.0f, 2.0f, Warm());
      HudText("ELECTROBENCH PSP", 8.0f, 26.0f, 1.0f, White());
      snprintf(line, sizeof(line), "SCENE %d %d.%dS F:%d", scene, t10 / 10,
               t10 % 10, frames);
      HudText(line, 8.0f, 40.0f, 1.0f, White());
    } else {
      const int avg10 = (int)(stat.fps * 10.0 + 0.5);
      const int sec10 = (int)(stat.seconds * 10.0 + 0.5);
      snprintf(line, sizeof(line), "SCENE %d RESULTS", scene);
      HudText(line, 60.0f, 96.0f, 2.0f, Warm());
      snprintf(line, sizeof(line), "AVG FPS: %d.%d", avg10 / 10, avg10 % 10);
      HudText(line, 60.0f, 132.0f, 2.0f, White());
      snprintf(line, sizeof(line), "FRAMES: %d IN %d.%dS", stat.frames,
               sec10 / 10, sec10 % 10);
      HudText(line, 60.0f, 152.0f, 1.0f, White());
      if (now - resultsAt > (double)kResultsMs * 0.001) sceneDone = true;
    }
    HudDraw();

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();

    frames++;
    if (!results && t * 1000.0 >= (double)kBenchMs) {
      results = true;
      resultsAt = now;
      // Average over the whole timed run, not the last one-second sample.
      stat.frames = frames;
      stat.seconds = t;
      stat.fps = t > 0.0 ? (double)frames / t : 0.0;
    }

    if (sceneDone) break;
  }

  return stat;
}

} // namespace

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  sceCtrlSetSamplingCycle(0);
  sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

  SetupCallbacks();
  ClockInit();
  GuInit();
  BuildFontTexture();
  // Both scenes are built before the first timed frame; Scene4Init's geometry
  // bake would otherwise be charged to whatever scene runs second.
  Scene2Init();
  Scene4Init();

  char report[512];
  int used = 0;
  used += snprintf(report + used, sizeof(report) - (size_t)used,
                   "ElectroBench PSP\nclock: %d/%d MHz (cpu/bus)\n",
                   scePowerGetCpuClockFrequency(),
                   scePowerGetBusClockFrequency());

  const int order[2] = {2, 4};
  for (int i = 0; i < 2; i++) {
    if (!gRunning) break;
    const int scene = order[i];
    SceneStat st = RunScene(scene);
    if (st.frames > 0) {
      const int avg10 = (int)(st.fps * 10.0 + 0.5);
      const int sec10 = (int)(st.seconds * 10.0 + 0.5);
      const int worst10 = (int)(st.worstFrameMs * 10.0 + 0.5);
      used += snprintf(report + used, sizeof(report) - (size_t)used,
                       "%s\nframes: %d\navg fps: %d.%d\nrun: %d.%d s\n"
                       "worst frame: %d.%d ms\nframes > 100 ms: %d\n\n",
                       SceneName(scene), st.frames, avg10 / 10, avg10 % 10,
                       sec10 / 10, sec10 % 10, worst10 / 10, worst10 % 10,
                       st.slowFrames);
    }
    if (!gRunning) break;
    if (used > (int)sizeof(report) - 128) break;
  }
  {
    int fd = sceIoOpen("ms0:/ElectroBench-psp.txt",
                       PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0) {
      sceIoWrite(fd, report, used);
      sceIoClose(fd);
    }
  }

  sceGuDisplay(0);
  sceGuTerm();
  sceKernelExitGame();
  return 0;
}
