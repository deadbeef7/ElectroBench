// ElectroBench — PlayStation Portable port: application shell.
//
// Two scenes (2, the dusk ocean, then 4, the power-line corridor) driven
// through the Graphics Engine, 45 s each, the same order and run length as the
// Vita and Android builds:
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
  // Restore depth writes: both scenes draw their opaque geometry next frame
  // and would be invisible against the cleared depth buffer without this.
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
  // Inverted-Z buffer: near maps to 65535 and the clear value is 0, so the
  // depth test must be GEQUAL (this is what the PSPSDK samples pair with these
  // two calls). With LEQUAL every fragment loses against the cleared 0 and any
  // pass that leaves the depth test on - i.e. the whole sea - is discarded.
  sceGuDepthRange(65535, 0);
  sceGuDepthFunc(GU_GEQUAL);
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

// The 3D matrices are built by hand and pushed with sceGuSetMatrix instead of
// going through sceGumPerspective/sceGumLookAt: on this toolchain those two
// produce matrices that the GE never rasterises anything with (a probe triangle
// 25 units in front of the eye simply does not appear), while the same values
// set explicitly do. The GE uses row vectors, so the projection is the GL one
// transposed: w' = z (looking down +Z) and z' = z*A + B with near -> 0.
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
  // The sky grid is clip-space (-1..1) with z=0, drawn under an orthographic
  // projection: the perspective would clip it at the near plane.
  // sceGumOrtho takes (left, right, BOTTOM, top): bottom must be -1 and top +1,
  // otherwise the grid's y=+1 row (the zenith) is drawn at the bottom of the
  // screen and the sky - sun included - appears upside down.
  sceGumMatrixMode(GU_PROJECTION);
  sceGumLoadIdentity();
  sceGumOrtho(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f);
  sceGumMatrixMode(GU_VIEW);
  sceGumLoadIdentity();
  sceGumUpdateMatrix();
}

double NowSeconds() { return (double)sceKernelGetSystemTimeWide() * 1e-6; }

// Every scene sets up the state it needs, but a pass that leaves something on
// (the sea leaves its additive passes with depth writes off, the HUD leaves
// texturing on) would leak into the next frame or the next scene. Reset the
// pieces both scenes share, once per frame, after the clear.
void ResetDrawState() {
  sceGuDisable(GU_FOG);
  sceGuDisable(GU_BLEND);
  sceGuDisable(GU_TEXTURE_2D);
  sceGuDisable(GU_ALPHA_TEST);
  sceGuDisable(GU_CULL_FACE);
  sceGuEnable(GU_DEPTH_TEST);
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
};

// One benchmark run: kBenchMs of scene, then the result panel for kResultsMs.
// Both scenes are initialised up front, so the hand-over between them does not
// pay the scene-4 geometry build (the largest single piece of CPU work in
// either scene) inside the second scene's timing.
SceneStat RunScene(int scene) {
  SceneStat stat{0, 0.0, 0.0};
  const double start = NowSeconds();
  double lastHud = start;
  double fps = 0.0;
  int frames = 0, hudFrames = 0;
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
    // SELECT skips straight to the next scene's results panel. The timed
    // figures are frozen at that moment, so the results screen's own frames do
    // not dilute the average.
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
      snprintf(line, sizeof(line), "FPS: %d", (int)(fps + 0.5));
      HudText(line, 8.0f, 6.0f, 2.0f, Warm());
      HudText("ELECTROBENCH PSP", 8.0f, 26.0f, 1.0f, White());
      snprintf(line, sizeof(line), "SCENE %d  %dS", scene, (int)t);
      HudText(line, 8.0f, 40.0f, 1.0f, White());
    } else {
      snprintf(line, sizeof(line), "SCENE %d RESULTS", scene);
      HudText(line, 60.0f, 96.0f, 2.0f, Warm());
      snprintf(line, sizeof(line), "AVG FPS: %.1f", stat.fps);
      HudText(line, 60.0f, 132.0f, 2.0f, White());
      snprintf(line, sizeof(line), "FRAMES: %d", stat.frames);
      HudText(line, 60.0f, 152.0f, 1.0f, White());
      if (now - resultsAt > (double)kResultsMs * 0.001) sceneDone = true;
    }
    HudDraw();

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();

    frames++;
    hudFrames++;
    if (now - lastHud >= 1.0) {
      fps = (double)hudFrames / (now - lastHud);
      hudFrames = 0;
      lastHud = now;
    }
    if (!results && t * 1000.0 >= (double)kBenchMs) {
      results = true;
      resultsAt = now;
      // Average over the whole timed run, not the last one-second sample.
      stat.frames = frames;
      stat.seconds = t;
      stat.fps = t > 0.0 ? (double)frames / t : 0.0;
    }

    // Leave the run only once the frame is complete. A break between
    // sceGuStart and sceGuFinish leaves the display list open, and the next
    // scene's list is then enqueued behind a stream the GE never sees the end
    // of: the console hangs on the results panel instead of starting scene 4.
    if (sceneDone) break;
  }

  // A quit (START) before the run finished leaves the stat empty; main() then
  // omits that scene from the result file.
  return stat;
}

} // namespace

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  scePowerSetClockFrequency(333, 333, 166);
  sceCtrlSetSamplingCycle(0);
  sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

  SetupCallbacks();
  GuInit();
  BuildFontTexture();
  // Both scenes are built before the first timed frame; Scene4Init's geometry
  // bake would otherwise be charged to whatever scene runs second.
  Scene2Init();
  Scene4Init();

  char report[512];
  int used = 0;
  used += snprintf(report + used, sizeof(report) - (size_t)used,
                   "ElectroBench PSP\nclock: 333/333/166 MHz\n");

  const int order[2] = {2, 4};
  for (int i = 0; i < 2; i++) {
    if (!gRunning) break;
    const int scene = order[i];
    SceneStat st = RunScene(scene);
    if (st.frames > 0)
      used += snprintf(report + used, sizeof(report) - (size_t)used,
                       "%s\nframes: %d\navg fps: %.1f\nrun: %.1f s\n\n",
                       SceneName(scene), st.frames, st.fps, st.seconds);
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
