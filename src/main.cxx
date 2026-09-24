// Libraries to include
#include "../lib/asset_path.hxx"
#include "../lib/util.hxx"

SDL_Window *window;
SDL_GLContext glContext;

// ---------------------------------------------------------------------------
// CLI options (headless visual testing, mirrors the PS1.4 benchmark)
//   --screenshot FILE  write a PPM screenshot and exit
//   --shot-time S      seconds to run before the screenshot (default 3)
//   --orbit AZ EL      camera azimuth/elevation in degrees
//   --dolly D          camera distance from the scene target
//   --width W --height H  window size (default 1366x768)
// ---------------------------------------------------------------------------
const char *gShotPath = nullptr;
float gShotTime = 3.0f;
int gWinW = 1366, gWinH = 768;
// debug: dump the shadow map and CPU-evaluate the shadow test at floor points
bool gDumpShadow = false;
// debug: disable shadow test for ground-truth shadow-diff screenshots
bool gNoShadow = false;
bool gDollySet = false;

// Results screen: when the run ends the scene is cleared and the final score
// is drawn on the window for a few seconds (ESC skips the wait).
static bool gResultsShown = false;
static double gResultsElapsed = 0.0, gResultsFps = 0.0, gResultsScore = 0.0;
static double gResultsShownAt = 0.0;
static const double kResultsScreenSeconds = 10.0;

// ---------------------------------------------------------------------------
// Small column-major mat4 helpers
// ---------------------------------------------------------------------------
static void mat4Multiply(const float a[16], const float b[16], float out[16]) {
  float r[16];
  for (int c = 0; c < 4; c++)
    for (int row = 0; row < 4; row++) {
      float s = 0.0f;
      for (int k = 0; k < 4; k++)
        s += a[k * 4 + row] * b[c * 4 + k];
      r[c * 4 + row] = s;
    }
  for (int i = 0; i < 16; i++)
    out[i] = r[i];
}

static bool mat4Invert(const float m[16], float out[16]) {
  float inv[16];
  inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
           m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
  inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
           m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
  inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
           m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
  inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
            m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
  inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
           m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
  inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
           m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
  inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
           m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
  inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
            m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
  inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
           m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
  inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
           m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
  inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
            m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
  inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
            m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
  inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
           m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
  inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
           m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
  inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
            m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
  inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
            m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

  float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
  if (det == 0.0f)
    return false;
  float idet = 1.0f / det;
  for (int i = 0; i < 16; i++)
    out[i] = inv[i] * idet;
  return true;
}

// Anonymous namespace to initialise window
namespace {
void initialiseWindow() {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    fprintf(stderr, "SDL could not initialize! SDL_Error: %s\n",
            SDL_GetError());
    std::exit(EXIT_FAILURE);
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  window = SDL_CreateWindow(NAME, SDL_WINDOWPOS_UNDEFINED,
                            SDL_WINDOWPOS_UNDEFINED, gWinW, gWinH,
                            SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
  if (window == NULL) {
    fprintf(stderr, "Window could not be created! SDL_Error: %s\n",
            SDL_GetError());
    std::exit(EXIT_FAILURE);
  }

  glContext = SDL_GL_CreateContext(window);
  if (glContext == NULL) {
    fprintf(stderr, "OpenGL context could not be created! SDL Error: %s\n",
            SDL_GetError());
    std::exit(EXIT_FAILURE);
  }

  SDL_GL_SetSwapInterval(0);
}
} // namespace

// Global variables
GLuint gunProg, groundProg, shadowProg;
GLuint shadowFBO = 0, shadowTex = 0;
const int SHADOW_SIZE = 1024;

float pos_x, pos_y, pos_z;
float angle_x = 30.0f, angle_y = 0.0f;
int init_time = time(NULL), final_time, frame;
int fps;
int x_old = 0, y_old = 0;
std::string model_name = "assets/UZI.obj";
bool is_holding_mouse = false;
bool is_updated = false;

// Orbit camera (drag orbits, mouse wheel dollies)
float cam_azimuth = -25.0f;
float cam_elevation = 26.0f;
float cam_dist = 10.5f;
float cam_min_dist = 3.0f;
float cam_max_dist = 26.0f;
static const float cam_target[3] = {0.0f, 0.45f, 0.0f};

// Scene layout: the 110 UZIs stand on the floor in a 10x11 grid
int grid_rows = 11, grid_cols = 10;
float grid_spacing = 0.68f;
float gun_scale = 1.0f;
float sun_dir_world[3];

// ------------------------------------------------------------ real fps + score
// Frame timing uses SDL's performance counter (sub-microsecond resolution)
// and accounts EVERY frame, so the counter tracks real frame rate instead of
// sampling 1-second buckets with integer division. The smooth value drives
// the HUD/title; the final score is computed from ALL frames of the run.
static double gPerfFreq = 1.0;
static Uint64 gPerfStartTick = 0;   // first accounted frame
static Uint64 gPerfLastTick = 0;    // previous frame
static double gFpsWindowStart = -1.0;
static int gFpsWindowFrames = 0;
static double gSmoothFps = 0.0;
static long gTotalFrames = 0;

// Benchmark score: fps^2 * 2 (see README). Linear in load twice over, easy to
// reason about, and computed from the true frame-count average of the run.
static inline double BenchScore(double fps) { return fps * fps * 2.0; }

// ---------------------------------------------------------------------------
// FPS HUD (OpenGL 2.1 fixed function): 5x7 bitmap font drawn as immediate-mode
// quads after the 3D pass, with a dark backing panel for readability.
// ---------------------------------------------------------------------------
static const unsigned char kHudFont[95][5] = {
    {0x00,0x00,0x00,0x00,0x00}, {0x05,0x05,0x05,0x00,0x05}, {0x0A,0x0A,0x00,0x00,0x00},
    {0x0A,0x0A,0x1F,0x0A,0x1F}, {0x04,0x0F,0x05,0x0E,0x09}, {0x19,0x12,0x04,0x09,0x13},
    {0x0C,0x12,0x14,0x12,0x0D}, {0x04,0x04,0x02,0x00,0x00}, {0x02,0x04,0x04,0x04,0x02},
    {0x08,0x04,0x04,0x04,0x08}, {0x00,0x15,0x0E,0x00,0x00}, {0x00,0x04,0x0E,0x04,0x00},
    {0x00,0x00,0x00,0x04,0x08}, {0x00,0x00,0x0E,0x00,0x00}, {0x00,0x00,0x00,0x04,0x00},
    {0x01,0x02,0x02,0x02,0x01}, {0x0E,0x11,0x11,0x11,0x0E}, {0x04,0x0C,0x04,0x04,0x0E},
    {0x0E,0x01,0x0E,0x10,0x0F}, {0x0E,0x01,0x06,0x01,0x0E}, {0x11,0x11,0x0F,0x01,0x01},
    {0x0F,0x10,0x0E,0x01,0x0E}, {0x0E,0x10,0x0E,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08},
    {0x0E,0x11,0x0E,0x11,0x0E}, {0x0E,0x11,0x07,0x01,0x0E}, {0x00,0x04,0x00,0x04,0x00},
    {0x00,0x04,0x00,0x04,0x08}, {0x02,0x04,0x08,0x04,0x02}, {0x00,0x00,0x0E,0x00,0x0E},
    {0x08,0x04,0x02,0x04,0x08}, {0x0E,0x01,0x06,0x04,0x00}, {0x0E,0x11,0x15,0x15,0x0E},
    {0x0E,0x11,0x11,0x1F,0x11}, {0x1E,0x09,0x0E,0x09,0x1E}, {0x0E,0x11,0x10,0x11,0x0E},
    {0x1C,0x12,0x11,0x12,0x1C}, {0x0F,0x10,0x1E,0x10,0x0F}, {0x0F,0x10,0x1E,0x10,0x10},
    {0x0E,0x10,0x13,0x11,0x0F}, {0x11,0x11,0x1F,0x11,0x11}, {0x0E,0x04,0x04,0x04,0x0E},
    {0x07,0x02,0x02,0x12,0x0C}, {0x11,0x12,0x1C,0x12,0x11}, {0x10,0x10,0x10,0x10,0x0F},
    {0x11,0x1B,0x15,0x11,0x11}, {0x11,0x19,0x15,0x13,0x11}, {0x0E,0x11,0x11,0x11,0x0E},
    {0x1E,0x11,0x1E,0x10,0x10}, {0x0E,0x11,0x11,0x15,0x16}, {0x1E,0x11,0x1E,0x12,0x11},
    {0x0F,0x10,0x0E,0x01,0x1E}, {0x1F,0x04,0x04,0x04,0x04}, {0x11,0x11,0x11,0x11,0x0E},
    {0x11,0x11,0x11,0x0A,0x04}, {0x11,0x11,0x15,0x15,0x0A}, {0x11,0x0A,0x04,0x0A,0x11},
    {0x11,0x11,0x0E,0x04,0x04}, {0x1F,0x02,0x04,0x08,0x1F}, {0x0E,0x08,0x08,0x08,0x0E},
    {0x01,0x02,0x02,0x04,0x08}, {0x0E,0x02,0x02,0x02,0x0E}, {0x04,0x0E,0x15,0x04,0x04},
    {0x00,0x00,0x00,0x00,0x1F}, {0x08,0x04,0x02,0x00,0x00}, {0x00,0x0E,0x01,0x07,0x0F},
    {0x10,0x1E,0x11,0x11,0x1E}, {0x00,0x0F,0x10,0x10,0x0F}, {0x01,0x0E,0x11,0x11,0x0E},
    {0x00,0x0E,0x11,0x1E,0x10}, {0x07,0x08,0x0E,0x08,0x07}, {0x10,0x1E,0x11,0x11,0x11},
    {0x04,0x00,0x0E,0x11,0x11}, {0x08,0x02,0x02,0x02,0x0C}, {0x04,0x02,0x02,0x12,0x0C},
    {0x10,0x10,0x1E,0x11,0x1E}, {0x10,0x10,0x10,0x10,0x0E}, {0x00,0x0A,0x15,0x15,0x0A},
    {0x00,0x0E,0x11,0x11,0x0E}, {0x00,0x1E,0x11,0x1E,0x10}, {0x00,0x0E,0x11,0x11,0x0E},
    {0x00,0x0F,0x10,0x0F,0x01}, {0x08,0x0E,0x10,0x08,0x04}, {0x00,0x1D,0x12,0x04,0x09},
    {0x00,0x0E,0x0A,0x0E,0x02}, {0x0B,0x0C,0x0E,0x02,0x06}, {0x04,0x04,0x04,0x04,0x04},
    {0x04,0x04,0x0E,0x00,0x00}, {0x09,0x12,0x1F,0x12,0x09}, {0x0A,0x0A,0x0A,0x0A,0x0A},
    {0x04,0x0F,0x11,0x0F,0x04}};

static void HudGlyph(int c, float x, float y) {
  if (c < 32 || c > 126) return;
  const unsigned char *g = kHudFont[c - 32];
  glBegin(GL_QUADS);
  for (int row = 0; row < 7; row++) {
    for (int col = 0; col < 5; col++) {
      if (g[row] & (1 << (4 - col))) {
        glVertex2f(x + col, y + row);
        glVertex2f(x + col + 1, y + row);
        glVertex2f(x + col + 1, y + row + 1);
        glVertex2f(x + col, y + row + 1);
      }
    }
  }
  glEnd();
}

static void HudText(float x, float y, const char *text) {
  float pen = x;
  for (const char *p = text; *p; ++p, pen += 6.0f) HudGlyph((unsigned char)*p, pen, y);
}

static void RenderHUD() {
  if (!window) return;
  char line[96];
  snprintf(line, sizeof(line), "FPS : %d   SCORE : %.0f", fps,
           gSmoothFps > 0.0 ? BenchScore(gSmoothFps) : 0.0);

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
  glDisable(GL_BLEND);

  // The gun pass leaves textures bound and GL_TEXTURE_2D enabled on non-active
  // units; glDisable only touches the ACTIVE unit, so unit 0 would keep
  // modulating these quads by the dark gunmetal texture. Reset unit 0 first.
  glActiveTexture(GL_TEXTURE0);
  glDisable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, 0);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

  // dark backing panel: solid strip so the text reads on any background
  glColor4f(0.02f, 0.02f, 0.04f, 1.0f);
  float w = (float)strlen(line) * 6.0f + 12.0f;
  glBegin(GL_QUADS);
  glVertex2f(0.0f, 0.0f);
  glVertex2f(w, 0.0f);
  glVertex2f(w, 16.0f);
  glVertex2f(0.0f, 16.0f);
  glEnd();

  glColor4f(0.72f, 0.93f, 1.0f, 1.0f); // pale cyan, matches the PS1.4 HUD
  HudText(6.0f, 5.0f, line);

  glEnable(GL_DEPTH_TEST);
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();
  glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

// ---- fused scene support ------------------------------------------------
// The OG binary is built from BOTH translation units (see the Makefile: the
// TideBench one is compiled with -DFUSED_INTO_OG). After the 60 s gun run it
// hands the SDL session to TideBench, which probes a GL 3.3 core context and
// simply skips itself when the device cannot provide one.
int RunTideBenchFused(bool *gaveUpOut); // TideBench scene entry (GL 3.3)
extern double gFusedTideScore;          // TideBench's final score
void changeSize(int w, int h);          // resize handler (defined below)

static bool   gFusedEnabled = true; // --og-only forces the single OG scene
static bool   gFusedTideRan = false;
static double gFusedOgScore = 0.0;

// Results screen: clear the window and show the final score big and centred.
// Same dark panel + pale cyan text as the in-run HUD.
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
  glDisable(GL_BLEND);

  // unit-0 texture state must be reset here too (see RenderHUD): the gun pass
  // leaves GL_TEXTURE_2D enabled on non-active units, which would modulate
  // these quads by the gunmetal texture.
  glActiveTexture(GL_TEXTURE0);
  glDisable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, 0);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

  glClearColor(0.012f, 0.012f, 0.022f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  char big[96], timeLine[128], hint[96], scene1[96], scene2[96];
  if (gFusedEnabled && gFusedTideRan) {
    // fused run: average of the scenes that ran, per-scene scores below
    snprintf(big, sizeof(big), "AVERAGE SCORE : %.0f",
             0.5 * (gFusedOgScore + gFusedTideScore));
  } else {
    snprintf(big, sizeof(big), "SCORE : %.0f", gResultsScore);
  }
  snprintf(timeLine, sizeof(timeLine), "Time : %.1fs   Average FPS : %.1f",
           gResultsElapsed, gResultsFps);
  snprintf(hint, sizeof(hint), "Benchmark complete - ESC to exit");

  float cx = 0.5f * (float)gWinW;
  float cy = 0.5f * (float)gWinH;

  // big score, centred: 5x7 glyphs scaled 6x, gap of 6 px per glyph
  glColor4f(0.72f, 0.93f, 1.0f, 1.0f);
  {
    float s = 6.0f;
    float pen = cx - (float)strlen(big) * 6.0f * s * 0.5f;
    float y = cy - 7.0f * s * 0.5f;
    for (const char *p = big; *p; ++p, pen += 6.0f * s)
      for (int row = 0; row < 7; row++)
        for (int col = 0; col < 5; col++) {
          const unsigned char *g = kHudFont[(unsigned char)*p - 32];
          if (!(g[row] & (1 << (4 - col)))) continue;
          float x0 = pen + col * s, y0 = y + row * s;
          glBegin(GL_QUADS);
          glVertex2f(x0, y0);
          glVertex2f(x0 + s, y0);
          glVertex2f(x0 + s, y0 + s);
          glVertex2f(x0, y0 + s);
          glEnd();
        }
  }

  // time + fps line, per-scene breakdown (fused) and hint, HUD scale, centred
  glColor4f(0.55f, 0.72f, 0.82f, 1.0f);
  HudText(cx - (float)strlen(timeLine) * 6.0f * 0.5f, cy + 7.0f * 6.0f * 0.5f + 24.0f, timeLine);
  if (gFusedEnabled) {
    snprintf(scene1, sizeof(scene1), "ElectroBench (guns)  : %.0f", gFusedOgScore);
    if (gFusedTideRan)
      snprintf(scene2, sizeof(scene2), "TideBench (ocean)    : %.0f", gFusedTideScore);
    else
      snprintf(scene2, sizeof(scene2), "TideBench (ocean)    : skipped (needs GL 3.3)");
    glColor4f(0.60f, 0.78f, 0.88f, 1.0f);
    HudText(cx - (float)strlen(scene1) * 6.0f * 0.5f, cy + 7.0f * 6.0f * 0.5f + 52.0f, scene1);
    HudText(cx - (float)strlen(scene2) * 6.0f * 0.5f, cy + 7.0f * 6.0f * 0.5f + 68.0f, scene2);
    glColor4f(0.40f, 0.48f, 0.55f, 1.0f);
    HudText(cx - (float)strlen(hint) * 6.0f * 0.5f, cy + 7.0f * 6.0f * 0.5f + 96.0f, hint);
  } else {
    glColor4f(0.40f, 0.48f, 0.55f, 1.0f);
    HudText(cx - (float)strlen(hint) * 6.0f * 0.5f, cy + 7.0f * 6.0f * 0.5f + 56.0f, hint);
  }

  glEnable(GL_DEPTH_TEST);
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();
  glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

// light-space matrix (world -> [0,1] shadow map coords)
float light_matrix[16];

float sky_color[3] = {0.42f, 0.45f, 0.60f};

Model model;

// @fun textFileRead
// @args: filename : const string
// Reads the contents of a text file.
std::string textFileRead(std::string const &filename) {
  if (!filename.empty()) {
    std::fstream file{filename};
    return {(std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>()};
  }

  return {};
}

// Prints the compile info log for a shader.
void printShaderInfoLog(GLuint object) {
  constexpr std::size_t MAX_LOG_LENGTH = 10000;
  std::array<char, MAX_LOG_LENGTH> log_buffer{};

  int infologLength = 0;
  glGetShaderiv(object, GL_INFO_LOG_LENGTH, &infologLength);

  if (infologLength > 0) {
    int charsWritten = 0;
    glGetShaderInfoLog(object, MAX_LOG_LENGTH, &charsWritten,
                       log_buffer.data());
    printf("%s\n", log_buffer.data());
  }
}

// Prints the attaching info log for a shader program.
void printProgramInfoLog(GLuint object) {
  int infologLength = 0;
  int charsWritten = 0;
  char *infoLog;

  glGetProgramiv(object, GL_INFO_LOG_LENGTH, &infologLength);
  if (infologLength > 0) {
    infoLog = (char *)malloc(infologLength);
    glGetProgramInfoLog(object, infologLength, &charsWritten, infoLog);
    printf("%s\n", infoLog);
    free(infoLog);
  }
}

// Loads one GLSL 1.2 shader file and compiles it.
GLuint loadShader(GLenum type, const std::string &filename) {
  GLuint sh = glCreateShader(type);
  std::string src = textFileRead(filename);
  const char *ptr = src.data();
  glShaderSource(sh, 1, &ptr, NULL);
  glCompileShader(sh);
  printShaderInfoLog(sh);
  return sh;
}

GLuint linkProgram(const char *vs_file, const char *fs_file) {
  GLuint vs = loadShader(GL_VERTEX_SHADER, vs_file);
  GLuint fs = loadShader(GL_FRAGMENT_SHADER, fs_file);
  GLuint prog = glCreateProgram();
  glAttachShader(prog, vs);
  glAttachShader(prog, fs);
  glLinkProgram(prog);
  printProgramInfoLog(prog);
  glDeleteShader(vs);
  glDeleteShader(fs);
  return prog;
}

// Draws the floor: one big quad in world space (modelview == view only)
void drawFloor() {
  glBegin(GL_QUADS);
  glVertex3f(-25.0f, 0.0f, -25.0f);
  glVertex3f(25.0f, 0.0f, -25.0f);
  glVertex3f(25.0f, 0.0f, 25.0f);
  glVertex3f(-25.0f, 0.0f, 25.0f);
  glEnd();
}

// Draws the 110 UZIs lying flat on the floor (mag base touching the ground),
// muzzle up, in a grid
void drawGuns() {
  for (int i = 0; i < grid_rows; i++) {
    for (int j = 0; j < grid_cols; j++) {
      glPushMatrix();

      float gridX = (j - (grid_cols - 1) * 0.5f) * grid_spacing;
      float gridZ = (i - (grid_rows - 1) * 0.5f) * grid_spacing;
      float yaw = (i + j) * 15.0f;

      glTranslatef(gridX, 0.0f, gridZ);
      glRotatef(yaw, 0.0f, 1.0f, 0.0f);
      // the model already lies along X with the mag pointing down (-Y), so
      // resting raw min_y on the floor plants every gun on its mag base with
      // the body horizontal: mags touch the ground, muzzle forward.
      // 0.001 keeps the contact face out of z-fighting but is invisible.
      glTranslatef(model.pos_x * gun_scale,
                   -model.min_y * gun_scale + 0.001f,
                   -model.pos_y * gun_scale);
      glScalef(gun_scale, gun_scale, gun_scale);
      model.draw();

      glPopMatrix();
    }
  }
}

// Renders the scene depth into the shadow map from the sun's point of view.
void renderShadowMap() {
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, shadowFBO);
  glPushAttrib(GL_VIEWPORT_BIT);
  glViewport(0, 0, SHADOW_SIZE, SHADOW_SIZE);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(-4.8, 4.8, -4.8, 4.8, 0.5, 20.0);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  gluLookAt(cam_target[0] + sun_dir_world[0] * 6.0f,
            cam_target[1] + sun_dir_world[1] * 6.0f,
            cam_target[2] + sun_dir_world[2] * 6.0f, cam_target[0],
            cam_target[1], cam_target[2], 0.0, 1.0, 0.0);

  glClear(GL_DEPTH_BUFFER_BIT);
  glUseProgram(shadowProg);
  // render front faces so the shadow map stores the TOP surface of each gun.
  // With the guns lying flat on the floor their back faces are the underside,
  // at floor level, which makes contact-area shadows lose to the depth bias.
  // Front faces sit ~0.26 units above the floor -> unambiguous depth gap.
  // The polygon offset below keeps the guns from shadow-acne-ing themselves.
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(4.0f, 8.0f);
  drawGuns();
  glDisable(GL_POLYGON_OFFSET_FILL);
  glDisable(GL_CULL_FACE);
  glUseProgram(0);

  glPopAttrib();
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
}

// Builds bias * lightProj * lightView once (the sun is fixed).
void buildLightMatrix() {
  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  glOrtho(-4.8, 4.8, -4.8, 4.8, 0.5, 20.0);
  float lproj[16];
  glGetFloatv(GL_PROJECTION_MATRIX, lproj);
  glPopMatrix();

  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();
  gluLookAt(cam_target[0] + sun_dir_world[0] * 6.0f,
            cam_target[1] + sun_dir_world[1] * 6.0f,
            cam_target[2] + sun_dir_world[2] * 6.0f, cam_target[0],
            cam_target[1], cam_target[2], 0.0, 1.0, 0.0);
  float lview[16];
  glGetFloatv(GL_MODELVIEW_MATRIX, lview);
  glPopMatrix();

  const float bias[16] = {0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f,
                          0.0f, 0.0f, 0.5f, 0.0f, 0.5f, 0.5f, 0.5f, 1.0f};
  float pv[16];
  // mat4Multiply indexes m[col*4+row], i.e. it already produces standard
  // column-major data — exactly what glUniformMatrix4fv with transpose=GL_FALSE
  // expects. (A transpose here was tried and verified to be wrong: it made the
  // shader sample the wrong light-space texels, scattering shadows around.)
  mat4Multiply(lproj, lview, pv);
  mat4Multiply(bias, pv, light_matrix);
}

void uploadCommonUniforms(GLuint prog) {
  float view[16], inv_view[16];
  glGetFloatv(GL_MODELVIEW_MATRIX, view);
  if (mat4Invert(view, inv_view)) {
    glUniformMatrix4fv(glGetUniformLocation(prog, "uInvView"), 1, GL_FALSE,
                       inv_view);
  }
  glUniformMatrix4fv(glGetUniformLocation(prog, "uLightMatrix"), 1, GL_FALSE,
                     light_matrix);
  glUniform2f(glGetUniformLocation(prog, "uShadowTexel"), 1.0f / SHADOW_SIZE,
              1.0f / SHADOW_SIZE);
}

// Sets up the orbit camera view matrix; leaves GL_MODELVIEW as the view.
void applyCamera() {
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  float elev_rad = cam_elevation * 3.14159265f / 180.0f;
  float az_rad = cam_azimuth * 3.14159265f / 180.0f;
  float eye[3] = {cam_target[0] + cam_dist * cosf(elev_rad) * sinf(az_rad),
                  cam_target[1] + cam_dist * sinf(elev_rad),
                  cam_target[2] + cam_dist * cosf(elev_rad) * cosf(az_rad)};
  gluLookAt(eye[0], eye[1], eye[2], cam_target[0], cam_target[1],
            cam_target[2], 0.0, 1.0, 0.0);
}

// Renders the scene (110 UZIs on a shadowed floor!) and calculates FPS
void renderScene() {
  unsigned int timet = SDL_GetTicks();

  // ---- results screen: scene cleared, score on the window, then exit ----
  if (gResultsShown) {
    RenderResults();
    SDL_GL_SwapWindow(window);
    double nowS = (double)(SDL_GetPerformanceCounter() - gPerfStartTick) / gPerfFreq;
    if (nowS - gResultsShownAt >= kResultsScreenSeconds) {
      SDL_Quit();
      exit(0);
    }
    return;
  }

  applyCamera();

  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // Ground pass
  glUseProgram(groundProg);
  uploadCommonUniforms(groundProg);
  glUniform3fv(glGetUniformLocation(groundProg, "uSunDirWorld"), 1,
               sun_dir_world);
  glUniform3fv(glGetUniformLocation(groundProg, "uSkyColor"), 1, sky_color);
  glUniform1f(glGetUniformLocation(groundProg, "uShadowDisable"),
              gNoShadow ? 1.0f : 0.0f);
  drawFloor();

  // Gun pass
  glUseProgram(gunProg);
  uploadCommonUniforms(gunProg);
  // sun direction in view space: rotate the world-space sun by the view
  float view[16], sun_view[3] = {0.0f, 0.0f, 0.0f};
  glGetFloatv(GL_MODELVIEW_MATRIX, view);
  for (int r = 0; r < 3; r++)
    for (int k = 0; k < 3; k++)
      sun_view[r] += view[k * 4 + r] * sun_dir_world[k];
  glUniform3fv(glGetUniformLocation(gunProg, "uSunDirView"), 1, sun_view);
  drawGuns();

  glUseProgram(0);
  RenderHUD();


  // Headless screenshot capture: read BEFORE the swap so the pixels analysed
  // are exactly what this frame rendered (the PS1.4 bench does the same).
  if (gShotPath != nullptr && timet >= (unsigned int)(gShotTime * 1000.0f)) {
    std::vector<unsigned char> px((size_t)gWinW * gWinH * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, gWinW, gWinH, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    FILE *f = fopen(gShotPath, "wb");
    if (f) {
      fprintf(f, "P6\n%d %d\n255\n", gWinW, gWinH);
      for (int y = gWinH - 1; y >= 0; y--)
        fwrite(&px[(size_t)y * gWinW * 3], 1, (size_t)gWinW * 3, f);
      fclose(f);
      printf("Screenshot saved to %s\n", gShotPath);
    }
    SDL_Quit();
    exit(0);
  }

  SDL_GL_SwapWindow(window);

  frame++;
  final_time = time(NULL);

  // ---- real fps + score accounting ----
  gTotalFrames++;
  Uint64 nowTick = SDL_GetPerformanceCounter();
  double nowS = (double)(nowTick - gPerfStartTick) / gPerfFreq;
  gPerfLastTick = nowTick;

  gFpsWindowFrames++;
  if (gFpsWindowStart < 0.0) gFpsWindowStart = nowS;
  double windowLen = nowS - gFpsWindowStart;
  if (windowLen >= 0.5) {
    double inst = (double)gFpsWindowFrames / windowLen; // real frames per second
    gSmoothFps = gSmoothFps > 0.0 ? gSmoothFps * 0.8 + inst * 0.2 : inst;
    gFpsWindowFrames = 0;
    gFpsWindowStart = nowS;
    fps = (int)(gSmoothFps + 0.5);
    char title[256];
    snprintf(title, 256, "ElectroBench - FPS : %d  Score : %.0f", fps,
             gSmoothFps * gSmoothFps * 2.0);
    SDL_SetWindowTitle(window, title);
  }

  // Debug probe: read back the depth texture and evaluate the exact shader
  // shadow test on the CPU for a few key world-space floor points.
  if (gDumpShadow) {
    std::vector<float> depth((size_t)SHADOW_SIZE * SHADOW_SIZE);
    glBindTexture(GL_TEXTURE_2D, shadowTex);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data());
    printf("shadow map stats: ");
    float mn = 1.0f, mx = 0.0f;
    size_t written = 0;
    for (float v : depth) {
      if (v < mn) mn = v;
      if (v > mx) mx = v;
    }
    printf("min=%.4f max=%.4f\n", mn, mx);
    printf("light_matrix (column-major, row-major print):\n");
    for (int r = 0; r < 4; r++)
      printf("  %10.6f %10.6f %10.6f %10.6f\n", light_matrix[r * 4 + 0],
             light_matrix[r * 4 + 1], light_matrix[r * 4 + 2],
             light_matrix[r * 4 + 3]);
    // dump every intermediate stage of buildLightMatrix
    {
      glMatrixMode(GL_PROJECTION);
      glPushMatrix();
      glLoadIdentity();
      glOrtho(-4.8, 4.8, -4.8, 4.8, 0.5, 20.0);
      float lproj2[16];
      glGetFloatv(GL_PROJECTION_MATRIX, lproj2);
      glPopMatrix();
      glMatrixMode(GL_MODELVIEW);
      glPushMatrix();
      glLoadIdentity();
      gluLookAt(cam_target[0] + sun_dir_world[0] * 6.0f,
                cam_target[1] + sun_dir_world[1] * 6.0f,
                cam_target[2] + sun_dir_world[2] * 6.0f, cam_target[0],
                cam_target[1], cam_target[2], 0.0, 1.0, 0.0);
      float lview2[16];
      glGetFloatv(GL_MODELVIEW_MATRIX, lview2);
      glPopMatrix();
      float pv2[16];
      mat4Multiply(lproj2, lview2, pv2);
      printf("lproj (row-major print):\n");
      for (int r = 0; r < 4; r++)
        printf("  %10.6f %10.6f %10.6f %10.6f\n", lproj2[r * 4 + 0],
               lproj2[r * 4 + 1], lproj2[r * 4 + 2], lproj2[r * 4 + 3]);
      printf("lview (row-major print):\n");
      for (int r = 0; r < 4; r++)
        printf("  %10.6f %10.6f %10.6f %10.6f\n", lview2[r * 4 + 0],
               lview2[r * 4 + 1], lview2[r * 4 + 2], lview2[r * 4 + 3]);
      printf("pv (row-major print):\n");
      for (int r = 0; r < 4; r++)
        printf("  %10.6f %10.6f %10.6f %10.6f\n", pv2[r * 4 + 0],
               pv2[r * 4 + 1], pv2[r * 4 + 2], pv2[r * 4 + 3]);
    }
    // empirical calibration: unproject known screen pixels of the MAIN camera
    // with GLU (uses GL's own matrices, no hand-derived rays), intersect the
    // floor, then evaluate light_matrix exactly like the GLSL shader does and
    // compare against the actual depth dump.
    {
      const float az = cam_azimuth * 3.14159265f / 180.0f;
      const float el = cam_elevation * 3.14159265f / 180.0f;
      float eye[3] = {cam_target[0] + cam_dist * cosf(el) * sinf(az),
                      cam_target[1] + cam_dist * sinf(el),
                      cam_target[2] + cam_dist * cosf(el) * cosf(az)};
      applyCamera();
      double mv[16], pr[16];
      for (int k = 0; k < 16; k++) {
        float mvf[16], prf[16];
        glGetFloatv(GL_MODELVIEW_MATRIX, mvf);
        glGetFloatv(GL_PROJECTION_MATRIX, prf);
        mv[k] = mvf[k];
        pr[k] = prf[k];
      }
      const int vp[4] = {0, 0, gWinW, gWinH};
      const int probes[6][2] = {{683, 460}, {683, 560}, {450, 620},
                                {950, 620},  {300, 680}, {1050, 680}};
      for (int pi = 0; pi < 6; pi++) {
        double wx, wy, wz;
        // near-plane point through this pixel, then ray to the y=0 floor
        double nx_, ny_, nz_;
        gluUnProject(probes[pi][0], probes[pi][1], 0.0, mv, pr, vp, &nx_,
                     &ny_, &nz_);
        double dx = nx_ - eye[0], dy = ny_ - eye[1], dz = nz_ - eye[2];
        float t = (float)(-eye[1] / dy);
        float wp[3] = {(float)(eye[0] + dx * t), 0.0f, (float)(eye[2] + dz * t)};
        float p4[4] = {wp[0], 0.0f, wp[2], 1.0f};
        float cg[4] = {0, 0, 0, 0};
        for (int r = 0; r < 4; r++)
          for (int col = 0; col < 4; col++)
            cg[r] += light_matrix[col * 4 + r] * p4[col];
        // uLightMatrix is already bias*P*V, so its output IS the [0,1]
        // shadow-map coordinate; no extra half-offset (that double-bias
        // squeezed every probe into the top-right quadrant).
        float ugx = cg[0] / cg[3];
        float ugy = cg[1] / cg[3];
        float uzg = cg[2] / cg[3];
        int tx = (int)(ugx * SHADOW_SIZE), ty = (int)(ugy * SHADOW_SIZE);
        float st = -1.0f;
        if (tx >= 0 && tx < SHADOW_SIZE && ty >= 0 && ty < SHADOW_SIZE)
          st = depth[(size_t)ty * SHADOW_SIZE + tx];
        printf("px(%4d,%4d)->world(%6.2f,%6.2f) glsluv=(%.3f,%.3f) uz=%.3f "
               "stored=%.4f %s\n",
               probes[pi][0], probes[pi][1], wp[0], wp[2], ugx, ugy, uzg, st,
               (st >= 0 && uzg - 0.0022f > st) ? "SHADOW" : "lit");
      }
    }
    FILE *df = fopen("/tmp/shadowdump.raw", "wb");
    if (df) {
      fwrite(depth.data(), 4, depth.size(), df);
      fclose(df);
      printf("wrote /tmp/shadowdump.raw (%zu floats)\n", depth.size());
      (void)written;
    }
    // light-space matrix as column-major float[16] (light_matrix layout)
    for (int probe = 0; probe < 5; probe++) {
      float wx, wz;
      const char *label;
      if (probe == 0) {
        // centre of the middle gun's footprint (must be shadowed)
        wx = 0.02f; wz = -0.05f; label = "mid-gun footprint";
      } else if (probe == 1) {
        wx = 0.30f; wz = -0.05f; label = "beside mid-gun (lit?)";
      } else if (probe == 2) {
        wx = 0.0f; wz = 0.0f; label = "world origin";
      } else if (probe == 3) {
        wx = 6.0f; wz = 0.0f; label = "far floor +x";
      } else {
        wx = 0.0f; wz = 6.0f; label = "far floor +z";
      }
      float p[4] = {wx, 0.0f, wz, 1.0f};
      float c[4] = {0, 0, 0, 0};
      // column-major consumption matching the shader (m[col*4+row]); the
      // output is already in [0,1] light space, so no extra half-offset.
      for (int col = 0; col < 4; col++)
        for (int r = 0; r < 4; r++)
          c[r] += light_matrix[col * 4 + r] * p[col];
      float ux = c[0] / c[3], uy = c[1] / c[3], uz = c[2] / c[3];
      int tx = (int)(ux * SHADOW_SIZE), ty = (int)(uy * SHADOW_SIZE);
      float stored = -1.0f;
      if (tx >= 0 && tx < SHADOW_SIZE && ty >= 0 && ty < SHADOW_SIZE)
        stored = depth[(size_t)ty * SHADOW_SIZE + tx];
      float bias = 0.0022f;
      float lit = (stored >= 0.0f) ? (uz - bias <= stored ? 1.0f : 0.0f) : -1.0f;
      printf("probe %-22s uv=(%.3f,%.3f) uz=%.4f stored=%.4f -> %s\n", label,
             ux, uy, uz, stored,
             lit < 0 ? "OUT-OF-MAP" : (lit > 0 ? "LIT" : "SHADOWED"));
    }
    SDL_Quit();
    exit(0);
  }
  if (timet >= 60000) {
    // Score from ALL frames of the run (not the last 1-second window).
    double elapsed = (double)(SDL_GetPerformanceCounter() - gPerfStartTick) / gPerfFreq;
    if (elapsed <= 0.0) elapsed = 1.0;
    double avgFps = (double)gTotalFrames / elapsed;
    double score = avgFps * avgFps * 2.0;
    printf("Benchmark Results - Time : %.1fs, Average FPS : %.1f, Score : %.0f\n",
           elapsed, avgFps, score);
    fflush(stdout);
    gResultsElapsed = elapsed;
    gResultsFps = avgFps;
    gResultsScore = score;
    gFusedOgScore = score;

    if (gFusedEnabled) {
      // ---- scene 2: TideBench (GL 3.3) on the same SDL session ----
      printf("Scene 2/2 : TideBench (GL 3.3 dusk ocean)\n");
      fflush(stdout);
      SDL_Quit(); // TideBench recreates the window with a GL 3.3 core context
      bool gaveUp = false;
      int rc = RunTideBenchFused(&gaveUp);
      if (rc == 0) {
        gFusedTideRan = true;
        printf("Fused Results - ElectroBench : %.0f | TideBench : %.0f | Average : %.0f\n",
               gFusedOgScore, gFusedTideScore,
               0.5 * (gFusedOgScore + gFusedTideScore));
      } else if (rc == 2) {
        // user quit during the TideBench scene — leave without the combined screen
        SDL_Quit();
        exit(0);
      } else {
        printf("TideBench skipped: no OpenGL 3.3 core context on this device\n");
      }
      fflush(stdout);
      // TideBench tore SDL down either way; bring the window back (a fresh
      // GL 2.1 context is all the immediate-mode results text needs).
      initialiseWindow();
      glewInit();
      changeSize(gWinW, gWinH);
      gResultsShownAt =
          (double)(SDL_GetPerformanceCounter() - gPerfStartTick) / gPerfFreq;
    } else {
      gResultsShownAt = elapsed;
    }
    // Hand over to the results screen: the scene is cleared and the score is
    // drawn on the window for kResultsScreenSeconds (ESC exits immediately).
    gResultsShown = true;
  }
}

// Processes keyboard input.
void processKeys(SDL_Event &event) {
  if (event.key.keysym.sym == SDLK_ESCAPE) {
    SDL_Quit();
    exit(0);
  }
}

// Prints any OpenGL errors.
#define printOpenGLError() printGLError(__FILE__, __LINE__)
int printGLError(const char *file, int line) {
  GLenum glErr;
  int retCode = 0;

  glErr = glGetError();
  while (glErr != GL_NO_ERROR) {
    printf("glError in file %s @ line %d: %s\n", file, line,
           gluErrorString(glErr));
    retCode = 1;
    glErr = glGetError();
  }
  return retCode;
}

void handleMouseEvent(SDL_Event &event) {
  is_updated = true;

  if (event.type == SDL_MOUSEBUTTONDOWN) {
    if (event.button.button == SDL_BUTTON_LEFT) {
      x_old = event.button.x;
      y_old = event.button.y;
      is_holding_mouse = true;
    }
  } else if (event.type == SDL_MOUSEBUTTONUP) {
    if (event.button.button == SDL_BUTTON_LEFT) {
      is_holding_mouse = false;
    }
  } else if (event.type == SDL_MOUSEWHEEL) {
    // multiplicative dolly zoom, clamped so we never clip into the scene
    cam_dist *= (event.wheel.y > 0) ? 0.90f : 1.10f;
    if (cam_dist < cam_min_dist)
      cam_dist = cam_min_dist;
    if (cam_dist > cam_max_dist)
      cam_dist = cam_max_dist;
  }
}

void handleMouseMotion(SDL_Event &event) {
  if (is_holding_mouse) {
    is_updated = true;

    // drag orbits the camera around the scene
    cam_azimuth += (event.motion.x - x_old) * 0.35f;
    cam_elevation += (event.motion.y - y_old) * 0.35f;
    x_old = event.motion.x;
    y_old = event.motion.y;

    if (cam_elevation > 85.0f)
      cam_elevation = 85.0f;
    else if (cam_elevation < -3.0f)
      cam_elevation = -3.0f;
  }
}

void changeSize(int w, int h) {
  if (h == 0)
    h = 1;

  float ratio = 1.0 * w / h;
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();

  glViewport(0, 0, w, h);
  gluPerspective(50, ratio, 0.1, 120);
  glMatrixMode(GL_MODELVIEW);
}

/*
 * Initialises the application.
 * Compiles shaders, sets up the shadow map, and loads the obj file.
 */
void setup() {
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluPerspective(50, (float)gWinW / (float)gWinH, 0.1, 120);
  glMatrixMode(GL_MODELVIEW);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_LINE_SMOOTH);
  glEnable(GL_MULTISAMPLE);
  glHint(GL_MULTISAMPLE_FILTER_HINT_NV, GL_NICEST);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glEnable(GL_TEXTURE_2D);
  glEnable(GL_DEPTH_TEST);
  glClearColor(sky_color[0], sky_color[1], sky_color[2], 1.0);

  model.load(resolveAssetPath(model_name.c_str()).c_str());
  pos_x = model.pos_x;
  pos_y = model.pos_y;

  // normalise the model so the longest bbox side is 0.55 world units
  float ext_x = model.max_x - model.min_x;
  float ext_y = model.max_y - model.min_y;
  float ext_z = model.max_z - model.min_z;
  float max_dim = ext_x;
  if (ext_y > max_dim)
    max_dim = ext_y;
  if (ext_z > max_dim)
    max_dim = ext_z;
  gun_scale = 0.55f / max_dim;

  // fixed warm sun from the upper left-front. Elevation keeps each gun's
  // shadow ~0.4 units (~0.6x its height): compact, clearly attached to its
  // own contact point, and far enough from the grid edge that per-gun
  // shadows do not tile into long diagonal bands across open floor.
  float sd[3] = {-0.58f, 0.74f, 0.34f};
  float len = sqrtf(sd[0] * sd[0] + sd[1] * sd[1] + sd[2] * sd[2]);
  sun_dir_world[0] = sd[0] / len;
  sun_dir_world[1] = sd[1] / len;
  sun_dir_world[2] = sd[2] / len;

  // zoom limits relative to the scene size
  float span = (grid_cols > grid_rows ? grid_cols : grid_rows) * grid_spacing;
  cam_min_dist = span * 0.45f;
  cam_max_dist = span * 3.6f;
  if (!gDollySet)
    cam_dist = span * 1.5f;
  if (cam_dist > cam_max_dist)
    cam_dist = cam_max_dist;
  if (cam_dist < cam_min_dist)
    cam_dist = cam_min_dist;

  // shader programs
  groundProg = linkProgram(resolveAssetPath("shaders/ground_vert.glsl").c_str(),
                           resolveAssetPath("shaders/ground_frag.glsl").c_str());
  gunProg = linkProgram(resolveAssetPath("shaders/vert.glsl").c_str(),
                        resolveAssetPath("shaders/frag.glsl").c_str());
  shadowProg =
      linkProgram(resolveAssetPath("shaders/shadow_vert.glsl").c_str(),
                  resolveAssetPath("shaders/shadow_frag.glsl").c_str());

  buildLightMatrix();

  // shadow map: depth-only FBO with a depth texture
  glGenFramebuffersEXT(1, &shadowFBO);
  glGenTextures(1, &shadowTex);
  glBindTexture(GL_TEXTURE_2D, shadowTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, SHADOW_SIZE,
               SHADOW_SIZE, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, shadowFBO);
  glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT,
                            GL_TEXTURE_2D, shadowTex, 0);
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);
  GLenum status = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
  if (status != GL_FRAMEBUFFER_COMPLETE_EXT) {
    fprintf(stderr, "Shadow framebuffer incomplete: 0x%x\n", status);
    std::exit(EXIT_FAILURE);
  }
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
  glDrawBuffer(GL_BACK);
  glReadBuffer(GL_BACK);

  // bind the shadow map to texture unit 6 (units 0-5 hold the gun maps)
  glActiveTexture(GL_TEXTURE6);
  glBindTexture(GL_TEXTURE_2D, shadowTex);
  glActiveTexture(GL_TEXTURE0);
  glUseProgram(groundProg);
  glUniform1i(glGetUniformLocation(groundProg, "uShadowMap"), 6);
  glUseProgram(gunProg);
  glUniform1i(glGetUniformLocation(gunProg, "uShadowMap"), 6);
  glUseProgram(0);

  // gun textures (as before)
  glUseProgram(gunProg);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, model.m->texture1);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, model.m->texture2);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, model.m->texture3);
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, model.m->texture4);
  glActiveTexture(GL_TEXTURE4);
  glBindTexture(GL_TEXTURE_2D, model.m->texture5);
  glActiveTexture(GL_TEXTURE5);
  glBindTexture(GL_TEXTURE_2D, model.m->texture6);
  glUniform1i(glGetUniformLocation(gunProg, "uBaseColor"), 0);
  glUniform1i(glGetUniformLocation(gunProg, "uNormalMap"), 1);
  glUniform1i(glGetUniformLocation(gunProg, "uMetallicMap"), 2);
  glUniform1i(glGetUniformLocation(gunProg, "uHeightMap"), 3);
  glUniform1i(glGetUniformLocation(gunProg, "uAOMap"), 4);
  glUniform1i(glGetUniformLocation(gunProg, "uRoughnessMap"), 5);
  glUseProgram(0);

  // the guns never move in world space, so one shadow render is enough
  renderShadowMap();
}

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--screenshot" && i + 1 < argc) {
      gShotPath = argv[++i];
    } else if (arg == "--shot-time" && i + 1 < argc) {
      gShotTime = (float)atof(argv[++i]);
    } else if (arg == "--orbit" && i + 2 < argc) {
      cam_azimuth = (float)atof(argv[++i]);
      cam_elevation = (float)atof(argv[++i]);
    } else if (arg == "--dolly" && i + 1 < argc) {
      cam_dist = (float)atof(argv[++i]);
      gDollySet = true;
    } else if (arg == "--width" && i + 1 < argc) {
      gWinW = atoi(argv[++i]);
    } else if (arg == "--height" && i + 1 < argc) {
      gWinH = atoi(argv[++i]);
    } else if (arg == "--dump-shadow") {
      gDumpShadow = true;
    } else if (arg == "--no-shadow") {
      gNoShadow = true;
    } else if (arg == "--og-only") {
      gFusedEnabled = false; // run only the OG scene even on GL 3.3 devices
    }
  }

  initialiseWindow();
  glewInit();

  setup();
  changeSize(gWinW, gWinH);

  SDL_Event event;
  bool quit = false;

  init_time = time(NULL);

  // start the sub-second fps clock right before the first rendered frame
  gPerfFreq = (double)SDL_GetPerformanceFrequency();
  gPerfStartTick = SDL_GetPerformanceCounter();
  gPerfLastTick = gPerfStartTick;
  gFpsWindowStart = -1.0;
  gFpsWindowFrames = 0;
  gSmoothFps = 0.0;
  gTotalFrames = 0;

  while (!quit) {
    while (SDL_PollEvent(&event) != 0) {
      if (event.type == SDL_QUIT) {
        quit = true;
      } else if (event.type == SDL_KEYDOWN) {
        processKeys(event);
      } else if (event.type == SDL_MOUSEBUTTONDOWN ||
                 event.type == SDL_MOUSEBUTTONUP ||
                 event.type == SDL_MOUSEWHEEL) {
        handleMouseEvent(event);
      } else if (event.type == SDL_MOUSEMOTION) {
        handleMouseMotion(event);
      } else if (event.type == SDL_WINDOWEVENT) {
        if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
          gWinW = event.window.data1;
          gWinH = event.window.data2;
          changeSize(event.window.data1, event.window.data2);
        }
      }
    }

    renderScene();
  }

  SDL_GL_DeleteContext(glContext);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}
