// ElectroBench -- the harness.
//
// This file owns the command line, the three scene entry points and the final
// results screen. It contains no scene geometry any more: scene 1 now lives in
// src/scene1.cxx (OpenGL 3.3 core, like scenes 2-3), which is why the only
// fixed-function code left here is the results card, drawn in its own small
// OpenGL 2.1 compatibility context after the three scenes have finished.

#include "../lib/asset_path.hxx"
#include "../lib/util.hxx" // owns the lodepng definitions the scenes decode with
#include "font_atlas.hxx"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#ifndef SDL_GL_SAMPLES
#define SDL_GL_SAMPLES SDL_GL_MULTISAMPLESAMPLES
#endif

SDL_Window *window;
SDL_GLContext glContext;

const char *gShotPath = nullptr;
float gShotTime = 3.0f;
int gWinW = 1280, gWinH = 720;
bool gWindowedMode = false;

// ------------------------------------------------------------- scene entry
int RunGunScene(bool *gaveUpOut);              // scene 1 (GL 3.3 range)
extern double gFusedOgScore;                   // scene 1's final score
int  GunSceneParseArgs(int argc, char **argv);
void GunSceneSetScreenshot(const char *path);
void GunSceneSetShotTime(float t);
void GunSceneSetStandalone(bool standalone);

int RunOceanScene(bool *gaveUpOut);            // scene 2 (GL 3.3 ocean)
extern double gFusedTideScore;
int  OceanSceneParseArgs(int argc, char **argv);
void OceanSceneSetScreenshot(const char *path);
void OceanSceneSetShotTime(float t);
void OceanSceneSetStandalone(bool standalone);

int RunPoleScene(bool *gaveUpOut);             // scene 3 (GL 3.3 power lines)
extern double gFusedPoleScore;
int  PoleSceneParseArgs(int argc, char **argv);
void PoleSceneSetScreenshot(const char *path);
void PoleSceneSetShotTime(float t);
void PoleSceneSetStandalone(bool standalone);

static bool gSceneOnly = false;
static bool gPoleOnly = false;
static bool gOgOnly = false;
static bool gFusedTideRan = false;
static bool gFusedPoleRan = false;

// ------------------------------------------------------------------ results
static bool gFontAtlasTexInit = false;
static GLuint gFontAtlasTex = 0;

static void EnsureFontAtlasTexture() {
  if (gFontAtlasTexInit) return;
  std::vector<unsigned char> px((size_t)kFontAtlasW * kFontAtlasH * 4);
  FontAtlasFillRGBA(px.data(), px.size());
  glGenTextures(1, &gFontAtlasTex);
  glBindTexture(GL_TEXTURE_2D, gFontAtlasTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kFontAtlasW, kFontAtlasH, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, px.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gFontAtlasTexInit = true;
}

static const float kGlyphW = (float)kFontAtlasGlyphW;
static const float kGlyphH = (float)kFontAtlasCell;

static void HudTextScaled(float x, float y, const char *text, float scale) {
  EnsureFontAtlasTexture();
  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, gFontAtlasTex);
  glBegin(GL_QUADS);
  float pen = x;
  for (const char *p = text; *p; ++p) {
    unsigned char c = (unsigned char)*p;
    if (!FontAtlasHasGlyph(c)) {
      pen += (float)kFontAtlasCell * scale * 0.75f;
      continue;
    }
    float uv[4];
    FontAtlasGlyphUV(c, uv);
    float x0 = pen, y0 = y;
    float x1 = pen + kGlyphW * scale, y1 = y + kGlyphH * scale;
    glTexCoord2f(uv[0], uv[1]); glVertex2f(x0, y0);
    glTexCoord2f(uv[2], uv[1]); glVertex2f(x1, y0);
    glTexCoord2f(uv[2], uv[3]); glVertex2f(x1, y1);
    glTexCoord2f(uv[0], uv[3]); glVertex2f(x0, y1);
    pen += (float)kFontAtlasCell * scale;
  }
  glEnd();
}

static float HudTextWidth(const char *text, float scale) {
  float w = 0.0f;
  for (const char *p = text; *p; ++p)
    w += (float)kFontAtlasCell * scale *
         (FontAtlasHasGlyph((unsigned char)*p) ? 1.0f : 0.75f);
  return w;
}

static void HudText(float x, float y, const char *text) {
  HudTextScaled(x, y, text, 1.0f);
}

static double NowSeconds() {
  return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
}

static void RenderResults() {
  glUseProgram(0);
  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  glOrtho(0.0, (double)gWinW, (double)gWinH, 0.0, -1.0, 1.0);
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_LIGHTING);

  glActiveTexture(GL_TEXTURE0);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_TEXTURE_2D);

  glClearColor(0.012f, 0.012f, 0.022f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  char big[96], hint[96], line1[96], line2[96], line3[96];
  int scenesRan = 1 + (gFusedTideRan ? 1 : 0) + (gFusedPoleRan ? 1 : 0);
  double sum = gFusedOgScore;
  if (gFusedTideRan) sum += gFusedTideScore;
  if (gFusedPoleRan) sum += gFusedPoleScore;
  if (scenesRan > 1)
    std::snprintf(big, sizeof(big), "AVERAGE SCORE : %.0f", sum / scenesRan);
  else
    std::snprintf(big, sizeof(big), "SCORE : %.0f", gFusedOgScore);
  std::snprintf(hint, sizeof(hint), "Benchmark complete - ESC to exit");

  std::snprintf(line1, sizeof(line1), "Scene 1 (range)  : %.0f", gFusedOgScore);
  std::snprintf(line2, sizeof(line2), "Scene 2 (ocean)  : %s",
                gFusedTideRan ? "" : "skipped (needs GL 3.3)");
  std::snprintf(line3, sizeof(line3), "Scene 3 (lain)   : %s",
                gFusedPoleRan ? "" : "skipped (needs GL 3.3)");
  char score[24];
  if (gFusedTideRan) {
    std::snprintf(score, sizeof(score), "%.0f", gFusedTideScore);
    std::strncat(line2, score, sizeof(line2) - std::strlen(line2) - 1);
  }
  if (gFusedPoleRan) {
    std::snprintf(score, sizeof(score), "%.0f", gFusedPoleScore);
    std::strncat(line3, score, sizeof(line3) - std::strlen(line3) - 1);
  }

  const float cx = 0.5f * (float)gWinW;
  const float cy = 0.5f * (float)gWinH;

  glColor4f(0.72f, 0.93f, 1.0f, 1.0f);
  HudTextScaled(cx - HudTextWidth(big, 6.0f) * 0.5f, cy - 70.0f, big, 6.0f);

  glColor4f(0.60f, 0.78f, 0.88f, 1.0f);
  HudText(cx - HudTextWidth(line1, 2.0f) * 0.5f, cy + 8.0f, line1);
  HudText(cx - HudTextWidth(line2, 2.0f) * 0.5f, cy + 34.0f, line2);
  HudText(cx - HudTextWidth(line3, 2.0f) * 0.5f, cy + 60.0f, line3);
  glColor4f(0.40f, 0.48f, 0.55f, 1.0f);
  HudText(cx - HudTextWidth(hint, 2.0f) * 0.5f, cy + 132.0f, hint);

  glEnable(GL_DEPTH_TEST);
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();
  glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

// Opens the small compatibility-profile window the results card is drawn in.
static void OpenResultsWindow() {
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                      SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
  SDL_GL_SetAttribute(SDL_GL_SAMPLES, 0);

  window = SDL_CreateWindow("ElectroBench - Results", SDL_WINDOWPOS_UNDEFINED,
                            SDL_WINDOWPOS_UNDEFINED, gWinW, gWinH,
                            SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN |
                                (gWindowedMode ? 0u : SDL_WINDOW_FULLSCREEN_DESKTOP));
  if (!window) {
    std::fprintf(stderr, "Results window could not be created! SDL_Error: %s\n",
                 SDL_GetError());
    std::exit(EXIT_FAILURE);
  }
  glContext = SDL_GL_CreateContext(window);
  if (!glContext) {
    std::fprintf(stderr, "Results context could not be created! SDL Error: %s\n",
                 SDL_GetError());
    std::exit(EXIT_FAILURE);
  }
  SDL_GL_SetSwapInterval(0);
  SDL_GetWindowSize(window, &gWinW, &gWinH);
  glViewport(0, 0, gWinW, gWinH);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0.0, (double)gWinW, (double)gWinH, 0.0, -1.0, 1.0);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glewInit();
  gFontAtlasTexInit = false;
  gFontAtlasTex = 0;
}

static void RunResultsScreen() {
  OpenResultsWindow();
  const double start = NowSeconds();
  SDL_Event event;
  bool quit = false;
  while (!quit) {
    while (SDL_PollEvent(&event) != 0) {
      if (event.type == SDL_QUIT) quit = true;
      if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
        quit = true;
    }
    RenderResults();
    SDL_GL_SwapWindow(window);
    if (NowSeconds() - start >= 10.0) quit = true;
  }
}

// --------------------------------------------------------------------- main
int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--screenshot" && i + 1 < argc) {
      gShotPath = argv[++i];
      gWindowedMode = true;
    } else if (arg == "--windowed") {
      gWindowedMode = true;
    } else if (arg == "--shot-time" && i + 1 < argc) {
      gShotTime = (float)atof(argv[++i]);
    } else if (arg == "--width" && i + 1 < argc) {
      gWinW = atoi(argv[++i]);
      gWindowedMode = true;
    } else if (arg == "--height" && i + 1 < argc) {
      gWinH = atoi(argv[++i]);
    } else if (arg == "--og-only") {
      gOgOnly = true;
    } else if (arg == "--scene-only") {
      gSceneOnly = true;
    } else if (arg == "--pole-only") {
      gPoleOnly = true;
    } else if (arg == "--trace-assets") {
      assetTraceFlag() = true;
    }
  }

  if (GunSceneParseArgs(argc, argv) != EXIT_SUCCESS) return EXIT_FAILURE;
  if (OceanSceneParseArgs(argc, argv) != EXIT_SUCCESS) return EXIT_FAILURE;
  if (PoleSceneParseArgs(argc, argv) != EXIT_SUCCESS) return EXIT_FAILURE;

  if (gShotPath != nullptr) {
    GunSceneSetScreenshot(gShotPath);
    OceanSceneSetScreenshot(gShotPath);
    PoleSceneSetScreenshot(gShotPath);
    GunSceneSetShotTime(gShotTime);
    OceanSceneSetShotTime(gShotTime);
    PoleSceneSetShotTime(gShotTime);
  }

  std::printf("ElectroBench version 0.6 build R2 (Release) (2026-11-18)\n");
  {
    static const char *kKey[] = {
        "shaders/range/uzi_frag.glsl", "shaders/ps14/sea_frag.glsl",
        "shaders/pole/object_frag.glsl"};
    char *b = SDL_GetBasePath();
    std::printf("Assets: exe dir %s| cwd wins over the exe dir when it has shaders/\n",
                b ? b : "(unknown)");
    SDL_free(b);
    for (int i = 0; i < 3; i++) {
      const std::string resolved = resolveAssetPath(kKey[i]);
      std::printf("  %-32s %7ld bytes  %s\n", kKey[i], assetSizeOf(resolved),
                  resolved.c_str());
    }
  }
  if (assetTraceSelected())
    std::printf("Assets: (--trace-assets) every resolved path is printed as it loads\n");
  std::fflush(stdout);

  if (gPoleOnly) {
    PoleSceneSetStandalone(true);
    bool gaveUp = false;
    int rc = RunPoleScene(&gaveUp);
    if (rc == 1) {
      std::fprintf(stderr, "ElectroBench: no OpenGL 3.3 core context on this device - "
                           "the power-lines (scene 3) scene cannot run here\n");
      return EXIT_FAILURE;
    }
    return 0;
  }

  if (gSceneOnly) {
    OceanSceneSetStandalone(true);
    bool gaveUp = false;
    int rc = RunOceanScene(&gaveUp);
    if (rc == 1) {
      std::fprintf(stderr, "ElectroBench: no OpenGL 3.3 core context on this device - "
                           "the ocean scene cannot run here\n");
      return EXIT_FAILURE;
    }
    return 0;
  }

  // ---- scene 1: the range -------------------------------------------------
  std::printf("Scene 1/3 : Electro Range (GL 3.3)\n");
  std::fflush(stdout);
  GunSceneSetStandalone(gOgOnly);
  bool gaveUpGun = false;
  int rcGun = RunGunScene(&gaveUpGun);
  if (rcGun == 2) {
    std::printf("ElectroBench: quit during scene 1\n");
    return 0;
  }
  if (rcGun != 0) {
    std::fprintf(stderr, "ElectroBench: no OpenGL 3.3 core context on this device - "
                         "scene 1 (and the rest of the benchmark) requires it\n");
    return EXIT_FAILURE;
  }
  if (gOgOnly) return 0;

  // ---- scenes 2-3, each recreating the window with a GL 3.3 core context --
  bool gaveUp = false;
  std::printf("Scene 2/3 : Dusk Ocean (GL 3.3)\n");
  std::fflush(stdout);
  int rc = RunOceanScene(&gaveUp);
  if (rc == 2) return 0;
  if (rc == 0) gFusedTideRan = true;

  bool gaveUpPole = false;
  std::printf("Scene 3/3 : Power lines (GL 3.3)\n");
  std::fflush(stdout);
  int rcPole = RunPoleScene(&gaveUpPole);
  if (rcPole == 2) return 0;
  if (rcPole == 0) gFusedPoleRan = true;

  std::printf("Fused Results - Range : %.0f | Ocean : %.0f | "
              "Power lines : %.0f\n",
              gFusedOgScore, gFusedTideScore, gFusedPoleScore);
  std::fflush(stdout);

  RunResultsScreen();
  SDL_Quit();
  return 0;
}
