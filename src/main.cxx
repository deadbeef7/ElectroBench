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

// -------------------------------------------------------------- window setup
// Every scene asks for borderless fullscreen in SDL_CreateWindow, which is the
// path the display server presents cleanly (a window created fullscreen never
// shows a torn frame, where one switched over while it is already drawing can).
// macOS does not always honour the flag on every window of the process, so
// BenchApplyDesktopFullscreen() still switches the window over afterwards for
// the ones it missed.
Uint32 BenchCreateFullscreenFlag() {
  return gWindowedMode ? (Uint32)0 : (Uint32)SDL_WINDOW_FULLSCREEN_DESKTOP;
}

void BenchApplyDesktopFullscreen(SDL_Window *win) {
  if (!win || gWindowedMode) return;
  if (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) return;
  if (SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
    std::fprintf(stderr, "ElectroBench: could not enter borderless fullscreen: %s\n",
                 SDL_GetError());
    return;
  }
  // The window manager finishes the switch asynchronously, so wait for the flag
  // before the caller reads the window size back for its viewport.
  const Uint32 deadline = SDL_GetTicks() + 1000;
  while (!(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) &&
         SDL_GetTicks() < deadline) {
    SDL_PumpEvents();
    SDL_Delay(2);
  }
}

// ------------------------------------------------------------- scene entry
int RunGunScene(bool *gaveUpOut);              // scene 1 (GL 3.3 range)
extern double gFusedOgScore;                   // scene 1's final score
extern double gFusedOgFps;
int  GunSceneParseArgs(int argc, char **argv);
void GunSceneSetScreenshot(const char *path);
void GunSceneSetShotTime(float t);
void GunSceneSetStandalone(bool standalone);

int RunOceanScene(bool *gaveUpOut);            // scene 2 (GL 3.3 ocean)
extern double gFusedTideScore;
extern double gFusedTideFps;
int  OceanSceneParseArgs(int argc, char **argv);
void OceanSceneSetScreenshot(const char *path);
void OceanSceneSetShotTime(float t);
void OceanSceneSetStandalone(bool standalone);

int RunPoleScene(bool *gaveUpOut);             // scene 3 (GL 3.3 power lines)
extern double gFusedPoleScore;
extern double gFusedPoleFps;
int  PoleSceneParseArgs(int argc, char **argv);
void PoleSceneSetScreenshot(const char *path);
void PoleSceneSetShotTime(float t);
void PoleSceneSetStandalone(bool standalone);

bool gTimingsVerbose = false; // read by every scene (see lib/framestats.hxx)

static bool gResultsOnly = false;
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

// Shrinks a text scale only as far as needed to keep the line inside maxWidth.
static float FittedScale(const char *text, float scale, float maxWidth) {
  const float w = HudTextWidth(text, 1.0f);
  if (w <= 0.0f) return scale;
  const float fit = maxWidth / w;
  return fit < scale ? fit : scale;
}

static void HudText(float x, float y, const char *text) {
  HudTextScaled(x, y, text, 1.0f);
}

static double NowSeconds() {
  return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
}

static void FillRect(float x, float y, float w, float h, float r, float g,
                     float b, float a) {
  glDisable(GL_TEXTURE_2D);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glColor4f(r, g, b, a);
  glBegin(GL_QUADS);
  glVertex2f(x, y);
  glVertex2f(x + w, y);
  glVertex2f(x + w, y + h);
  glVertex2f(x, y + h);
  glEnd();
}

struct ResultsRow {
  const char *label;
  double score;
  double fps;
  bool ran;
  float r, g, b;
};

static void RenderResults(bool savedFlash) {
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

  ResultsRow rows[3] = {
      {"Scene 1  Range", gFusedOgScore, gFusedOgFps,
       gFusedOgScore > 0.0, 1.00f, 0.62f, 0.28f},
      {"Scene 2  Ocean", gFusedTideScore, gFusedTideFps,
       gFusedTideRan, 0.36f, 0.76f, 1.00f},
      {"Scene 3  Power Lines", gFusedPoleScore, gFusedPoleFps,
       gFusedPoleRan, 0.95f, 0.78f, 0.46f},
  };

  int scenesRan = 0;
  double sum = 0.0, maxScore = 1.0;
  for (const ResultsRow &row : rows) {
    if (!row.ran) continue;
    scenesRan++;
    sum += row.score;
    if (row.score > maxScore) maxScore = row.score;
  }

  char big[96], hint[128], explain[160], explain2[160];
  if (scenesRan > 1)
    std::snprintf(big, sizeof(big), "AVERAGE SCORE : %.0f", sum / scenesRan);
  else
    std::snprintf(big, sizeof(big), "SCORE : %.0f", gFusedOgScore);
  std::snprintf(hint, sizeof(hint), "S  save results image      ESC  exit");
  std::snprintf(explain, sizeof(explain),
                "Score = average FPS x average FPS x 2: twice the frames per second, "
                "squared.");
  std::snprintf(explain2, sizeof(explain2),
                "Higher is better. Compare runs at the same resolution; 1%% low "
                "and GPU times print to the console.");

  const float cx = 0.5f * (float)gWinW;
  const float cy = 0.5f * (float)gWinH;

  // The card is laid out as one centred block: title, score, two explanation
  // lines, the three score rows, then the key hint. Keeping the block centred
  // keeps the margins balanced at any window height.
  const float blockH = 356.0f;
  const float top = cy - 0.5f * blockH;

  glColor4f(0.44f, 0.58f, 0.68f, 1.0f);
  HudTextScaled(cx - HudTextWidth("ELECTROBENCH", 3.0f) * 0.5f, top,
                "ELECTROBENCH", 3.0f);
  glColor4f(0.72f, 0.93f, 1.0f, 1.0f);
  const float bigScale = FittedScale(big, 5.0f, 0.88f * (float)gWinW);
  HudTextScaled(cx - HudTextWidth(big, bigScale) * 0.5f, top + 42.0f, big,
                bigScale);

  glColor4f(0.48f, 0.60f, 0.70f, 1.0f);
  const float explainScale = FittedScale(explain, 1.3f, 0.90f * (float)gWinW);
  const float explain2Scale = FittedScale(explain2, 1.3f, 0.90f * (float)gWinW);
  HudTextScaled(cx - HudTextWidth(explain, explainScale) * 0.5f, top + 104.0f,
                explain, explainScale);
  HudTextScaled(cx - HudTextWidth(explain2, explain2Scale) * 0.5f, top + 128.0f,
                explain2, explain2Scale);

  const float labelX = cx - 398.0f;
  const float barX = cx - 82.0f;
  const float barW = 260.0f;
  const float barH = 18.0f;
  const float valueX = barX + barW + 26.0f;
  // Text is drawn from its top edge, so lift it by half the height difference
  // between the glyph line (8 texels * 1.6) and the bar.
  const float rowTextDy = 2.5f;
  for (int i = 0; i < 3; i++) {
    const float y = top + 170.0f + (float)i * 46.0f;
    const ResultsRow &row = rows[i];
    glColor4f(0.60f, 0.72f, 0.82f, 1.0f);
    if (row.ran) HudTextScaled(labelX, y + rowTextDy, row.label, 1.6f);

    FillRect(barX, y + 2.0f, barW, barH, 0.10f, 0.13f, 0.17f, 1.0f);
    if (row.ran) {
      float fraction = (float)(row.score / maxScore);
      if (fraction > 1.0f) fraction = 1.0f;
      FillRect(barX, y + 2.0f, barW * fraction, barH, row.r, row.g, row.b, 0.92f);
      char value[64];
      std::snprintf(value, sizeof(value), "%.0f  (%.1f FPS)", row.score, row.fps);
      glColor4f(0.82f, 0.90f, 0.96f, 1.0f);
      HudTextScaled(valueX, y + rowTextDy, value, 1.6f);
    } else {
      glColor4f(0.38f, 0.42f, 0.48f, 1.0f);
      HudTextScaled(barX + 8.0f, y + 3.5f, "skipped (needs GL 3.3)", 1.4f);
    }
  }

  glColor4f(savedFlash ? 0.55f : 0.38f, savedFlash ? 0.90f : 0.48f,
             savedFlash ? 0.55f : 0.55f, 1.0f);
  HudTextScaled(cx - HudTextWidth(hint, 1.6f) * 0.5f, top + 320.0f, hint, 1.6f);
  if (savedFlash) {
    glColor4f(0.55f, 0.90f, 0.60f, 1.0f);
    HudTextScaled(cx - HudTextWidth("electrobench-results.png written", 1.4f) * 0.5f,
                  top + 346.0f, "electrobench-results.png written", 1.4f);
  }

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
                                BenchCreateFullscreenFlag());
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
  BenchApplyDesktopFullscreen(window);
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

// Writes the finished results card to electrobench-results.png. The window is
// a compatibility-profile context, so the pixels are read straight from the
// back buffer the card was just drawn into and flipped (GL rows are bottom-up).
static bool SaveResultsImage(const char *path) {
  std::vector<unsigned char> rgb((size_t)gWinW * gWinH * 3);
  std::vector<unsigned char> flipped((size_t)gWinW * gWinH * 3);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadBuffer(GL_BACK);
  glReadPixels(0, 0, gWinW, gWinH, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
  const size_t rowBytes = (size_t)gWinW * 3;
  for (int y = 0; y < gWinH; y++)
    std::memcpy(&flipped[(size_t)y * rowBytes],
                &rgb[(size_t)(gWinH - 1 - y) * rowBytes], rowBytes);
  unsigned err = lodepng_encode24_file(path, flipped.data(), (unsigned)gWinW,
                                       (unsigned)gWinH);
  if (err) {
    std::fprintf(stderr, "Could not save results image: %s\n",
                 lodepng_error_text(err));
    return false;
  }
  std::printf("Results image written: %s\n", path);
  std::fflush(stdout);
  return true;
}

// autoSavePath is for --results-only: render the card once, write it and return
// without waiting for a key press.
static void RunResultsScreen(const char *autoSavePath) {
  OpenResultsWindow();
  if (autoSavePath != nullptr) {
    RenderResults(false);
    SaveResultsImage(autoSavePath);
    return;
  }
  const double start = NowSeconds();
  SDL_Event event;
  bool quit = false;
  bool saveRequested = false;
  bool savedFlash = false;
  while (!quit) {
    while (SDL_PollEvent(&event) != 0) {
      if (event.type == SDL_QUIT) quit = true;
      if (event.type == SDL_KEYDOWN) {
        if (event.key.keysym.sym == SDLK_ESCAPE) quit = true;
        if (event.key.keysym.sym == SDLK_s) saveRequested = true;
      }
    }
    RenderResults(savedFlash);
    if (saveRequested) {
      saveRequested = false;
      savedFlash = SaveResultsImage("electrobench-results.png");
    }
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
    } else if (arg == "--timings") {
      gTimingsVerbose = true;
    } else if (arg == "--results-only") {
      gResultsOnly = true;
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

  if (gResultsOnly) {
    // Layout/screenshot check for the card: fixed sample scores so the numbers
    // exercise the full width of the rows without running the scenes.
    gFusedOgScore = 1234.0;
    gFusedOgFps = 61.2;
    gFusedTideRan = true;
    gFusedTideScore = 421.0;
    gFusedTideFps = 9.6;
    gFusedPoleRan = true;
    gFusedPoleScore = 907.0;
    gFusedPoleFps = 24.3;
    RunResultsScreen(gShotPath != nullptr ? gShotPath
                                         : "electrobench-results.png");
    SDL_Quit();
    return 0;
  }

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

  RunResultsScreen(nullptr);
  SDL_Quit();
  return 0;
}
