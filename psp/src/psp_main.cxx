// ElectroBench — PlayStation Portable port: application shell.
//
// One scene (2, the dusk ocean) driven through the Graphics Engine:
//   * 480x272 16-bit framebuffer + 16-bit depth, both in the 2 MB EDRAM, double
//     buffered through sceGuSwapBuffers
//   * two 5650 buffers and the depth buffer live at EDRAM offsets 0 / 512 KiB
//   * the CPU runs at the hardware maximum (333 MHz CPU, 166 MHz bus), and the
//     per-vertex shading in psp_scene2.cxx is all VFPU float code
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
constexpr int kResultsMs = 12000;
constexpr int kSkyRefreshFrames = 10;

// Display list. GU_DIRECT lets this grow to whatever the frame needs (three
// sea passes of ~5.3k vertices, the sky grid and the HUD); 2 MB is far above
// the ~500 KB a frame actually emits, so the list can never truncate.
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
    // The atlas is sampled straight out of RAM (see psp_scene2.hxx): writing
    // the pixels back is all the GE needs, the block-transfer copy into VRAM
    // is what used to be rejected and stop the whole display list.
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
  // Upload to the GE: sceGum* only edits the local matrix stack, and this
  // path draws with sceGuDrawArray, which does not push it.
  sceGumUpdateMatrix();

  sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF,
                 gHudCount, nullptr, gHudVerts);
  sceGuDisable(GU_TEXTURE_2D);
  sceGuDisable(GU_BLEND);
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
  sceGuDepthFunc(GU_LEQUAL);
  sceGuEnable(GU_DEPTH_TEST);
  sceGuFrontFace(GU_CW);
  sceGuDisable(GU_CULL_FACE);
  sceGuShadeModel(GU_SMOOTH);
  sceGuTexEnvColor(0xFFFFFFFF);
  // The GE always transforms by world * view * projection, and GUM's MODEL
  // stack starts UNINITIALISED: leaving it alone makes every draw collapse to
  // a point (the clear still shows, which hides the bug). Push identity into
  // the GE once here and never touch the model matrix again.
  sceGumMatrixMode(GU_MODEL);
  sceGumLoadIdentity();
  sceGumUpdateMatrix();
  sceGuFinish();
  sceGuSync(0, 0);
  sceDisplayWaitVblankStart();
  sceGuDisplay(1);
}

void SetSceneMatrix() {
  float eye[3];
  float yaw, pitch;
  Scene2Camera(eye, &yaw, &pitch);
  // Scene2FovY() is the vertical field of view in DEGREES (it is fed to glm on
  // the GL ports); sceGumPerspective takes radians. Handing it 55 directly
  // builds a nonsense projection and every sea vertex is clipped away.
  const float fov = Scene2FovY() * 3.14159265f / 180.0f;

  sceGumMatrixMode(GU_PROJECTION);
  sceGumLoadIdentity();
  sceGumPerspective(fov, (float)kScreenW / (float)kScreenH, 1.0f, 5000.0f);

  // The scene's camera orbits and always faces the sun; sceGumLookAt builds
  // the view in the GE's own convention (row vectors, -Z forward), so there is
  // no hand-built matrix to get wrong.
  const float cy = std::cos(yaw), sy = std::sin(yaw);
  const float cp = std::cos(pitch), sp = std::sin(pitch);
  ScePspFVector3 e = {eye[0], eye[1], eye[2]};
  ScePspFVector3 c = {eye[0] + sy * cp * 50.0f, eye[1] + sp * 50.0f, eye[2] + cy * cp * 50.0f};
  ScePspFVector3 up = {0.0f, 1.0f, 0.0f};
  sceGumMatrixMode(GU_VIEW);
  sceGumLoadIdentity();
  sceGumLookAt(&e, &c, &up);
  // sceGum* only edits the local stack; the GE keeps the matrices until this
  // upload. Without it every sceGuDrawArray transforms with a stale matrix
  // and the whole scene is dropped (the clear still shows, which is what made
  // this look like "nothing renders at all").
  sceGumUpdateMatrix();
}

void SetSkyMatrix() {
  // The sky grid is clip-space (-1..1) with z=0, drawn under an orthographic
  // projection: the perspective would clip it at the near plane.
  sceGumMatrixMode(GU_PROJECTION);
  sceGumLoadIdentity();
  sceGumOrtho(-1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f);
  sceGumMatrixMode(GU_VIEW);
  sceGumLoadIdentity();
  sceGumUpdateMatrix();
}

void WriteResults(const char *text) {
  int fd = sceIoOpen("ms0:/ElectroBench-psp.txt", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if (fd < 0) return;
  sceIoWrite(fd, text, (int)strlen(text));
  sceIoClose(fd);
}

double NowSeconds() { return (double)sceKernelGetSystemTimeWide() * 1e-6; }

} // namespace

int main(int argc, char *argv[]) {
  scePowerSetClockFrequency(333, 333, 166);
  sceCtrlSetSamplingCycle(0);
  sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

  SetupCallbacks();
  GuInit();
  BuildFontTexture();
  Scene2Init();

  const double start = NowSeconds();
  double lastHud = start;
  int frames = 0, hudFrames = 0;
  double fps = 0.0;
  bool results = false;
  double resultsAt = 0.0;
  int frame = 0;

  SceCtrlData pad;

  while (gRunning) {
    const double now = NowSeconds();
    const double t = now - start;

    sceCtrlPeekBufferPositive(&pad, 1);
    if (pad.Buttons & PSP_CTRL_START) break;

    const int refresh = (frame % kSkyRefreshFrames) == 0;
    Scene2UpdateCamera(t);
    Scene2Build(t, refresh);

    sceGuStart(GU_DIRECT, gList);
    sceGuClearColor(0xFF1A0D10); // warm dusk black, ABGR
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    SetSkyMatrix();
    Scene2DrawSky();
    SetSceneMatrix();
    Scene2Draw();

    HudBegin();
    char line[64];
    if (!results) {
      snprintf(line, sizeof(line), "FPS: %d", (int)(fps + 0.5));
      HudText(line, 8.0f, 6.0f, 2.0f, Warm());
      HudText("ELECTROBENCH PSP", 8.0f, 26.0f, 1.0f, White());
      snprintf(line, sizeof(line), "SCENE 2  %dS", (int)t);
      HudText(line, 8.0f, 40.0f, 1.0f, White());
    } else {
      double secs = (now - resultsAt);
      snprintf(line, sizeof(line), "SCENE 2 RESULTS");
      HudText(line, 60.0f, 96.0f, 2.0f, Warm());
      snprintf(line, sizeof(line), "AVG FPS: %.1f", fps);
      HudText(line, 60.0f, 132.0f, 2.0f, White());
      snprintf(line, sizeof(line), "FRAMES: %d", frames);
      HudText(line, 60.0f, 152.0f, 1.0f, White());
      if (secs > (double)kResultsMs * 0.001) break;
    }
    HudDraw();

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();

    frames++;
    hudFrames++;
    frame++;
    if (now - lastHud >= 1.0) {
      fps = (double)hudFrames / (now - lastHud);
      hudFrames = 0;
      lastHud = now;
    }
    if (!results && t * 1000.0 >= (double)kBenchMs) {
      results = true;
      resultsAt = now;
      char out[256];
      snprintf(out, sizeof(out),
                    "ElectroBench PSP\nscene 2 (dusk ocean)\nframes: %d\navg fps: %.1f\n"
                    "run: %.1f s\nclock: 333/333/166 MHz\n",
                    frames, fps, t);
      WriteResults(out);
      frames = 0;
    }
  }

  sceGuDisplay(0);
  sceGuTerm();
  sceKernelExitGame();
  return 0;
}
