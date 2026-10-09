// Libraries to include
#include "../lib/asset_path.hxx"
#include "../lib/util.hxx"
#include "font_atlas.hxx" // shared 8x8 HUD font atlas (same one the ocean scene uses)

SDL_Window *window;
SDL_GLContext glContext;

const char *gShotPath = nullptr;
float gShotTime = 3.0f;
int gWinW = 1280, gWinH = 720;
bool gWindowedMode = false;
// debug: dump the shadow map and CPU-evaluate the shadow test at floor points
bool gDumpShadow = false;
// debug: disable shadow test for ground-truth shadow-diff screenshots
bool gNoShadow = false;
bool gDollySet = false;

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

  window = SDL_CreateWindow("ElectroBench - Scene 1", SDL_WINDOWPOS_UNDEFINED,
                            SDL_WINDOWPOS_UNDEFINED, gWinW, gWinH,
                            SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN
                            | (gWindowedMode ? 0u : SDL_WINDOW_FULLSCREEN_DESKTOP));
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


  if (!gWindowedMode) {
    int dw = gWinW, dh = gWinH;
    SDL_GetWindowSize(window, &dw, &dh);
    if (dw > 0 && dh > 0) {
      gWinW = dw;
      gWinH = dh;
    }
  }  
  printf("Display: %s %dx%d | capture with Win+PrtScr, or --screenshot FILE\n",
         gWindowedMode ? "windowed" : "borderless fullscreen", gWinW, gWinH);
  fflush(stdout);

  SDL_GL_SetSwapInterval(0);
}
} // namespace

// Global variables
GLuint gunProg, groundProg, shadowProg;
GLuint shadowFBO = 0, shadowTex = 0;
const int SHADOW_SIZE = 1024;

static GLuint gScreenshotFbo = 0;
static GLuint gScreenshotColorTex = 0;
static int gScreenshotWidth = 0;
static int gScreenshotHeight = 0;


float pos_x, pos_y, pos_z;
float angle_x = 30.0f, angle_y = 0.0f;
int init_time = time(NULL), final_time, frame;
int fps;
std::string model_name = "assets/UZI.obj";

float cam_azimuth = -25.0f;
float cam_elevation = 26.0f;
float cam_dist = 10.5f;
float cam_min_dist = 3.0f;
float cam_max_dist = 26.0f;
float cam_target[3] = {0.0f, 0.45f, 0.0f};

static bool gFlyover = true;
static bool gFlyoverUserSet = false;


int grid_rows = 11, grid_cols = 10;
float grid_spacing = 0.68f;
float gun_scale = 1.0f;
float sun_dir_world[3];

static double gPerfFreq = 1.0;
static Uint64 gPerfStartTick = 0;  
static Uint64 gPerfLastTick = 0;  
static double gFpsWindowStart = -1.0;
static int gFpsWindowFrames = 0;
static double gSmoothFps = 0.0;
static long gTotalFrames = 0;

static inline double BenchScore(double fps) { return fps * fps * 2.0; }

static bool gFontAtlasTexInit = false;
static GLuint gFontAtlasTex = 0;

static void EnsureFontAtlasTexture() {
  if (gFontAtlasTexInit) return;
  std::vector<unsigned char> px((size_t)kFontAtlasW * kFontAtlasH * 4);
  FontAtlasFillRGBA(px.data(), px.size());
  glGenTextures(1, &gFontAtlasTex);
  glBindTexture(GL_TEXTURE_2D, gFontAtlasTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kFontAtlasW, kFontAtlasH, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gFontAtlasTexInit = true;
}

static const float kGlyphW = (float)kFontAtlasGlyphW;
static const float kGlyphH = (float)kFontAtlasCell;
static const float kGlyphAdvance = (float)kFontAtlasCell; 
static const float kGlyphScale = 8.0f;   
static const float kHudPanelH = 18.0f;  

static float HudTextScaled(float x, float y, const char *text, float scale) {
  EnsureFontAtlasTexture();
  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, gFontAtlasTex);
  glBegin(GL_QUADS);
  float pen = x;
  for (const char *p = text; *p; ++p) {
    unsigned char c = (unsigned char)*p;
    if (!FontAtlasHasGlyph(c)) { 
      pen += kGlyphAdvance * scale * 0.75f;
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
    pen += kGlyphAdvance * scale;
  }
  glEnd();
  return pen - x;
}

static float HudTextWidth(const char *text, float scale) {
  float w = 0.0f;
  for (const char *p = text; *p; ++p)
    w += kGlyphAdvance * scale *
         (FontAtlasHasGlyph((unsigned char)*p) ? 1.0f : 0.75f);
  return w;
}

static void HudText(float x, float y, const char *text) {
  HudTextScaled(x, y, text, 1.0f);
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

  glActiveTexture(GL_TEXTURE0);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glDisable(GL_TEXTURE_2D);
  glColor4f(0.02f, 0.02f, 0.04f, 1.0f);
  float w = HudTextWidth(line, 1.0f) + 12.0f;
  glBegin(GL_QUADS);
  glVertex2f(0.0f, 0.0f);
  glVertex2f(w, 0.0f);
  glVertex2f(w, kHudPanelH);
  glVertex2f(0.0f, kHudPanelH);
  glEnd();

  glColor4f(0.72f, 0.93f, 1.0f, 1.0f); // pale cyan, matches the ocean HUD
  HudText(6.0f, 0.5f * (kHudPanelH - kGlyphH), line);

  glEnable(GL_DEPTH_TEST);
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();
  glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}


int RunOceanScene(bool *gaveUpOut);       // scene 2 entry (GL 3.3 ocean)
extern double gFusedTideScore;            // scene 2's final score
int  OceanSceneParseArgs(int argc, char **argv); // scene 2's CLI flags
void OceanSceneSetScreenshot(const char *path);  
void OceanSceneSetShotTime(float t);            
void OceanSceneSetStandalone(bool standalone);   
int RunPoolScene(bool *gaveUpOut);        // scene 3 entry (GL 3.3 pool room)
extern double gFusedPoolScore;            // scene 3's final score
int  PoolSceneParseArgs(int argc, char **argv);  // scene 3's CLI flags
void PoolSceneSetScreenshot(const char *path);   // share --screenshot
void PoolSceneSetShotTime(float t);             // --shot-time (singular)
void PoolSceneSetStandalone(bool standalone);    // --pool-only
// SCENE 4: the power-lines scene (orange sky, poles + wire tangle).
int RunPoleScene(bool *gaveUpOut);        // scene 4 entry (GL 3.3 power lines)
extern double gFusedPoleScore;            // scene 4's final score
int  PoleSceneParseArgs(int argc, char **argv);  // scene 4's CLI flags
void PoleSceneSetScreenshot(const char *path);   // share --screenshot
void PoleSceneSetShotTime(float t);             // --shot-time (singular)
void PoleSceneSetStandalone(bool standalone);    // --pole-only
void changeSize(int w, int h);            // resize handler (defined below)

static bool   gFusedEnabled = true; 
static bool   gSceneOnly = false;    
static bool   gPoolOnly = false;    
static bool   gPoleOnly = false;     
static bool   gFusedTideRan = false;
static bool   gFusedPoolRan = false;
static bool   gFusedPoleRan = false;
static double gFusedOgScore = 0.0;

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


  glActiveTexture(GL_TEXTURE0);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_TEXTURE_2D);

  glClearColor(0.012f, 0.012f, 0.022f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  char big[96], timeLine[128], hint[96], scene1[96], scene2[96], scene3[96], scene4[96];
  int scenesRan = 1 + (gFusedTideRan ? 1 : 0) + (gFusedPoolRan ? 1 : 0) +
                  (gFusedPoleRan ? 1 : 0);
  if (gFusedEnabled && scenesRan > 1) {

    double sum = gFusedOgScore;
    if (gFusedTideRan) sum += gFusedTideScore;
    if (gFusedPoolRan) sum += gFusedPoolScore;
    if (gFusedPoleRan) sum += gFusedPoleScore;
    snprintf(big, sizeof(big), "AVERAGE SCORE : %.0f", sum / scenesRan);
  } else {
    snprintf(big, sizeof(big), "SCORE : %.0f", gResultsScore);
  }
  snprintf(timeLine, sizeof(timeLine), "Time : %.1fs   Average FPS : %.1f",
           gResultsElapsed, gResultsFps);
  snprintf(hint, sizeof(hint), "Benchmark complete - ESC to exit");

  float cx = 0.5f * (float)gWinW;
  float cy = 0.5f * (float)gWinH;


  glColor4f(0.72f, 0.93f, 1.0f, 1.0f);
  HudTextScaled(cx - HudTextWidth(big, kGlyphScale) * 0.5f,
                cy - kGlyphH * kGlyphScale * 0.5f, big, kGlyphScale);

  glColor4f(0.55f, 0.72f, 0.82f, 1.0f);
  HudText(cx - HudTextWidth(timeLine, 1.0f) * 0.5f,
          cy + kGlyphH * kGlyphScale * 0.5f + 24.0f, timeLine);
  if (gFusedEnabled) {
    snprintf(scene1, sizeof(scene1), "Scene 1 (guns)  : %.0f", gFusedOgScore);
    snprintf(scene2, sizeof(scene2), "Scene 2 (ocean) : %s",
             gFusedTideRan ? "" : "skipped (needs GL 3.3)");
    if (gFusedTideRan) {
      char scoreTxt[24];
      snprintf(scoreTxt, sizeof(scoreTxt), "%.0f", gFusedTideScore);
      strncat(scene2, scoreTxt, sizeof(scene2) - strlen(scene2) - 1);
    }
    snprintf(scene3, sizeof(scene3), "Scene 3 (pool)  : %s",
             gFusedPoolRan ? "" : "skipped (needs GL 3.3)");
    if (gFusedPoolRan) {
      char scoreTxt[24];
      snprintf(scoreTxt, sizeof(scoreTxt), "%.0f", gFusedPoolScore);
      strncat(scene3, scoreTxt, sizeof(scene3) - strlen(scene3) - 1);
    }
    snprintf(scene4, sizeof(scene4), "Scene 4 (lain) : %s",
             gFusedPoleRan ? "" : "skipped (needs GL 3.3)");
    if (gFusedPoleRan) {
      char scoreTxt[24];
      snprintf(scoreTxt, sizeof(scoreTxt), "%.0f", gFusedPoleScore);
      strncat(scene4, scoreTxt, sizeof(scene4) - strlen(scene4) - 1);
    }
    glColor4f(0.60f, 0.78f, 0.88f, 1.0f);
    HudText(cx - HudTextWidth(scene1, 1.0f) * 0.5f,
            cy + kGlyphH * kGlyphScale * 0.5f + 52.0f, scene1);
    HudText(cx - HudTextWidth(scene2, 1.0f) * 0.5f,
            cy + kGlyphH * kGlyphScale * 0.5f + 68.0f, scene2);
    HudText(cx - HudTextWidth(scene3, 1.0f) * 0.5f,
            cy + kGlyphH * kGlyphScale * 0.5f + 84.0f, scene3);
    HudText(cx - HudTextWidth(scene4, 1.0f) * 0.5f,
            cy + kGlyphH * kGlyphScale * 0.5f + 100.0f, scene4);
    glColor4f(0.40f, 0.48f, 0.55f, 1.0f);
    HudText(cx - HudTextWidth(hint, 1.0f) * 0.5f,
            cy + kGlyphH * kGlyphScale * 0.5f + 128.0f, hint);
  } else {
    glColor4f(0.40f, 0.48f, 0.55f, 1.0f);
    HudText(cx - HudTextWidth(hint, 1.0f) * 0.5f,
            cy + kGlyphH * kGlyphScale * 0.5f + 56.0f, hint);
  }

  glEnable(GL_DEPTH_TEST);
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();
  glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

float light_matrix[16];
float sky_color[3] = {0.86f, 0.80f, 0.70f};

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

void drawFloor() {
  glBegin(GL_QUADS);
  glVertex3f(-110.0f, 0.0f, -110.0f);
  glVertex3f(110.0f, 0.0f, -110.0f);
  glVertex3f(110.0f, 0.0f, 110.0f);
  glVertex3f(-110.0f, 0.0f, 110.0f);
  glEnd();
}

static GLuint gSkyDomeTex = 0;
static const float kSkyDomeR = 105.0f;

static inline float clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

static void BuildSkyDomeTexture() {
  const int W = 128, H = 64;
  std::vector<unsigned char> px((size_t)W * H * 4);
  for (int j = 0; j < H; j++) {
    float v = ((float)j + 0.5f) / (float)H;
    float el = 90.0f - 180.0f * v;             
    float elr = el * 3.14159265f / 180.0f;
    float cy = sinf(elr), cr = cosf(elr);
    for (int i = 0; i < W; i++) {
      float u = ((float)i + 0.5f) / (float)W;
      float az = u * 6.28318f;
      float dx = cr * sinf(az), dz = cr * cosf(az), dy = cy;
      float up = dy;
      float r, g, b;
      if (dy >= 0.0f) {
        float t = powf(clampf(dy, 0.0f, 1.0f), 0.62f);
        r = 0.86f + (0.30f - 0.86f) * t;
        g = 0.80f + (0.40f - 0.80f) * t;
        b = 0.70f + (0.60f - 0.70f) * t;
      } else {
        float t = clampf(-dy * 1.6f, 0.0f, 1.0f);
        r = 0.86f - 0.62f * t;
        g = 0.80f - 0.60f * t;
        b = 0.70f - 0.54f * t;
      }
      float mu = dx * sun_dir_world[0] + dy * sun_dir_world[1] +
                 dz * sun_dir_world[2];
      if (mu > 0.0f) {
        float ang2 = 2.0f * (1.0f - mu);
        float glow = expf(-ang2 * 26.0f);
        float disc = expf(-ang2 * 5200.0f);
        r += 0.85f * glow + 0.55f * disc;
        g += 0.70f * glow + 0.50f * disc;
        b += 0.48f * glow + 0.42f * disc;
      }
      size_t o = ((size_t)j * W + i) * 4;
      px[o + 0] = (unsigned char)(clampf(r, 0.0f, 1.0f) * 255.0f);
      px[o + 1] = (unsigned char)(clampf(g, 0.0f, 1.0f) * 255.0f);
      px[o + 2] = (unsigned char)(clampf(b, 0.0f, 1.0f) * 255.0f);
      px[o + 3] = 255;
    }
  }
  if (!gSkyDomeTex) glGenTextures(1, &gSkyDomeTex);
  glBindTexture(GL_TEXTURE_2D, gSkyDomeTex);  
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               px.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void drawSkyDome() {
  float m[16];
  glGetFloatv(GL_MODELVIEW_MATRIX, m);
  const float ex = m[12], ey = m[13], ez = m[14];
  const int SEG = 32, RING = 16;
  (void)SEG;
  (void)RING;
  glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
  for (int r = 0; r < RING; r++) {
    for (int i = 0; i <= SEG; i++) {
      float u = (float)i / (float)SEG;
      float az = u * 6.28318f;
      for (int k = 0; k < 2; k++) {
        float v = (float)(r + k) / (float)RING;
        float el = 90.0f - 180.0f * v;
        float elr = el * 3.14159265f / 180.0f;
        glTexCoord2f(u, v);
        glVertex3f(ex + cosf(elr) * kSkyDomeR * sinf(az),
                   ey + sinf(elr) * kSkyDomeR,
                   ez + cosf(elr) * kSkyDomeR * cosf(az));
      }
    }
  }
}

void drawGuns() {
  for (int i = 0; i < grid_rows; i++) {
    for (int j = 0; j < grid_cols; j++) {
      glPushMatrix();

      float gridX = (j - (grid_cols - 1) * 0.5f) * grid_spacing;
      float gridZ = (i - (grid_rows - 1) * 0.5f) * grid_spacing;
      float yaw = (i + j) * 15.0f;

      glTranslatef(gridX, 0.0f, gridZ);
      glRotatef(yaw, 0.0f, 1.0f, 0.0f);

      glTranslatef(model.pos_x * gun_scale,
                   -model.min_y * gun_scale + 0.001f,
                   -model.pos_y * gun_scale);
      glScalef(gun_scale, gun_scale, gun_scale);
      model.draw();

      glPopMatrix();
    }
  }
}


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

static double RunSeconds() {
  if (gPerfFreq <= 0.0) return 0.0;
  return (double)(SDL_GetPerformanceCounter() - gPerfStartTick) / gPerfFreq;
}

static inline float Smooth01(float u) {
  if (u < 0.0f) u = 0.0f;
  if (u > 1.0f) u = 1.0f;
  return u * u * (3.0f - 2.0f * u);
}

void UpdateFlyoverCamera(double t) {
  if (!gFlyover) return;
  const float span =
      (float)(grid_cols > grid_rows ? grid_cols : grid_rows) * grid_spacing;
  float az, el, d, tx, ty, tz;
  if (t < 7.0) {
   
    float e = Smooth01((float)t / 7.0f);
    az = -78.0f + 53.0f * e;
    el = 24.0f - 9.0f * e;
    d = span * 3.0f - span * 1.4f * e;
    tx = 0.0f;
    ty = 0.45f;
    tz = 0.0f;
  } else if (t < 34.0) {
    float u = (float)(t - 7.0) / 27.0f;
    az = -22.0f + 5.0f * sinf(u * 6.28318f);
    el = 9.0f + 2.0f * sinf(u * 12.56636f);
    d = span * 1.15f;
    tz = span * 1.25f - u * span * 2.5f;
    tx = 0.34f * sinf(u * 9.4f); 
    ty = 0.32f;
  } else {
    float e = Smooth01((float)(t - 34.0) / 13.0f);
    az = -22.0f + 21.0f * e;
    el = 11.0f + 9.0f * e;
    d = span * 1.15f + (span * 1.5f - span * 1.15f) * e;
    tx = 0.0f;
    ty = 0.45f - 0.13f * e;
    tz = -span * 1.25f * (1.0f - e);
  }
  cam_target[0] = tx;
  cam_target[1] = ty;
  cam_target[2] = tz;
  cam_azimuth = az;
  cam_elevation = el;
  cam_dist = d;
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
  double sceneSeconds = (double)(SDL_GetPerformanceCounter() - gPerfStartTick) / gPerfFreq;

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

  UpdateFlyoverCamera(RunSeconds());
  applyCamera();

  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  if (gSkyDomeTex) {
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, gSkyDomeTex);
    glDisable(GL_LIGHTING);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_QUAD_STRIP);
    drawSkyDome();
    glEnd();
  }
  glUseProgram(groundProg);
  uploadCommonUniforms(groundProg);
  glUniform3fv(glGetUniformLocation(groundProg, "uSunDirWorld"), 1,
               sun_dir_world);
  glUniform3fv(glGetUniformLocation(groundProg, "uSkyColor"), 1, sky_color);
  {
    float mv[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, mv);
    glUniform3f(glGetUniformLocation(groundProg, "uEyePos"), mv[12], mv[13],
                mv[14]);
  }
  glUniform1f(glGetUniformLocation(groundProg, "uShadowDisable"),
              gNoShadow ? 1.0f : 0.0f);
  drawFloor();

  glUseProgram(gunProg);
  uploadCommonUniforms(gunProg);
  float view[16], sun_view[3] = {0.0f, 0.0f, 0.0f};
  glGetFloatv(GL_MODELVIEW_MATRIX, view);
  for (int r = 0; r < 3; r++)
    for (int k = 0; k < 3; k++)
      sun_view[r] += view[k * 4 + r] * sun_dir_world[k];
  glUniform3fv(glGetUniformLocation(gunProg, "uSunDirView"), 1, sun_view);
  drawGuns();

  glUseProgram(0);
  RenderHUD();
  if (gShotPath != nullptr && sceneSeconds >= (double)gShotTime) {
    if (gScreenshotFbo == 0 && gScreenshotWidth == 0) {
      gScreenshotWidth = gWinW;
      gScreenshotHeight = gWinH;
    }
    if (gScreenshotFbo) {
      glBindFramebuffer(GL_READ_FRAMEBUFFER, gScreenshotFbo);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
      glBlitFramebuffer(0, 0, gScreenshotWidth, gScreenshotHeight, 0, 0,
                        gScreenshotWidth, gScreenshotHeight,
                        GL_COLOR_BUFFER_BIT, GL_NEAREST);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    }
    std::vector<unsigned char> px((size_t)gScreenshotWidth * gScreenshotHeight * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, gScreenshotWidth, gScreenshotHeight, GL_RGB,
                 GL_UNSIGNED_BYTE, px.data());
    FILE *f = fopen(gShotPath, "wb");
    if (f) {
      fprintf(f, "P6\n%d %d\n255\n", gScreenshotWidth, gScreenshotHeight);
      for (int y = gScreenshotHeight - 1; y >= 0; y--)
        fwrite(&px[(size_t)y * gScreenshotWidth * 3], 1,
               (size_t)gScreenshotWidth * 3, f);
      fclose(f);
      printf("Screenshot saved to %s\n", gShotPath);
    }
    if (gScreenshotFbo) {
      glDeleteFramebuffers(1, &gScreenshotFbo);
      gScreenshotFbo = 0;
    }
    if (gScreenshotColorTex) {
      glDeleteTextures(1, &gScreenshotColorTex);
      gScreenshotColorTex = 0;
    }
    gScreenshotWidth = 0;
    gScreenshotHeight = 0;
    SDL_Quit();
    exit(0);
  }

  SDL_GL_SwapWindow(window);

  frame++;
  final_time = time(NULL);

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
  // very debug-y
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
  if (sceneSeconds >= 60.0) {
    double elapsed = sceneSeconds;
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
      // ---- scene 2: the GL 3.3 dusk-ocean scene, same SDL session ----
      printf("Scene 2/4 : Dusk Ocean (GL 3.3)\n");
      fflush(stdout);
      SDL_Quit(); // the ocean scene recreates the window with a GL 3.3 core context
      bool gaveUp = false;
      int rc = RunOceanScene(&gaveUp);
      if (rc == 0) {
        gFusedTideRan = true;
        printf("Fused Results - ElectroBench : %.0f | Dusk Ocean : %.0f\n",
               gFusedOgScore, gFusedTideScore);
      } else if (rc == 2) {
        // user quit during the ocean scene — leave without the combined screen
        SDL_Quit();
        exit(0);
      } else {
        printf("Dusk ocean scene skipped: no OpenGL 3.3 core context on this device\n");
      }
      fflush(stdout);

      // ---- scene 3: the GL 3.3 pool-room scene (checker sky + water + teapot) ----
      printf("Scene 3/4 : Pool Room (GL 3.3)\n");
      fflush(stdout);
      bool gaveUpPool = false;
      int rcPool = RunPoolScene(&gaveUpPool);
      if (rcPool == 0) {
        gFusedPoolRan = true;
      } else if (rcPool == 2) {
        SDL_Quit();
        exit(0);
      } else {
        printf("Pool room scene skipped: no OpenGL 3.3 core context on this device\n");
      }
      fflush(stdout);

      // ---- scene 4: the GL 3.3 power-lines scene (orange sky + wires) ----
      printf("Scene 4/4 : Power lines (GL 3.3)\n");
      fflush(stdout);
      bool gaveUpPole = false;
      int rcPole = RunPoleScene(&gaveUpPole);
      if (rcPole == 0) {
        gFusedPoleRan = true;
      } else if (rcPole == 2) {
        SDL_Quit();
        exit(0);
      } else {
        printf("Power-lines scene skipped: no OpenGL 3.3 core context on this device\n");
      }
      fflush(stdout);

      // The GL 3.3 scenes tore SDL down either way; bring the window back (a fresh
      // GL 2.1 context is all the immediate-mode results text needs). The font
      // atlas texture lived in the dead context; force a re-upload.
      gFontAtlasTexInit = false;
      gFontAtlasTex = 0;
      initialiseWindow();
      glewInit();
      changeSize(gWinW, gWinH);
      gResultsShownAt =
          (double)(SDL_GetPerformanceCounter() - gPerfStartTick) / gPerfFreq;
    } else {
      gResultsShownAt = elapsed;
    }
    // Hand over to the results screen: the scene is cleared and the score is drawn
    gResultsShown = true;
  }
}

// Processes keyboard input. ESC is the only control the app keeps: the
// flyover camera is automatic from launch and no key or mouse can steer it.
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
  float ext_x = model.max_x - model.min_x;
  float ext_y = model.max_y - model.min_y;
  float ext_z = model.max_z - model.min_z;
  float max_dim = ext_x;
  if (ext_y > max_dim)
    max_dim = ext_y;
  if (ext_z > max_dim)
    max_dim = ext_z;
  gun_scale = 0.55f / max_dim;
  float sd[3] = {-0.58f, 0.74f, 0.34f};
  float len = sqrtf(sd[0] * sd[0] + sd[1] * sd[1] + sd[2] * sd[2]);
  sun_dir_world[0] = sd[0] / len;
  sun_dir_world[1] = sd[1] / len;
  sun_dir_world[2] = sd[2] / len;
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
  glActiveTexture(GL_TEXTURE7);
  BuildSkyDomeTexture();
  glUseProgram(groundProg);
  glUniform1i(glGetUniformLocation(groundProg, "uShadowMap"), 6);
  glUseProgram(gunProg);
  glUniform1i(glGetUniformLocation(gunProg, "uShadowMap"), 6);
  glUseProgram(0);

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

  renderShadowMap();
}

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
    } else if (arg == "--orbit" && i + 2 < argc) {
      cam_azimuth = (float)atof(argv[++i]);
      cam_elevation = (float)atof(argv[++i]);
    } else if (arg == "--dolly" && i + 1 < argc) {
      cam_dist = (float)atof(argv[++i]);
      gDollySet = true;
    } else if (arg == "--width" && i + 1 < argc) {
      gWinW = atoi(argv[++i]);
      gWindowedMode = true;   
    } else if (arg == "--height" && i + 1 < argc) {
      gWinH = atoi(argv[++i]);
    } else if (arg == "--dump-shadow") {
      gDumpShadow = true;
    } else if (arg == "--trace-assets") {
      assetTraceFlag() = true;   // print every resolved asset path
    } else if (arg == "--no-shadow") {
      gNoShadow = true;
    } else if (arg == "--og-only") {
      gFusedEnabled = false; // run only the OG scene even on GL 3.3 devices
    } else if (arg == "--scene-only") {
      gSceneOnly = true; // run only the GL 3.3 ocean scene
    } else if (arg == "--pool-only") {
      gPoolOnly = true; // run only the GL 3.3 pool-room scene
    } else if (arg == "--pole-only") {
      gPoleOnly = true; // run only scene 4: the GL 3.3 power-lines scene
    } else if (arg == "--flyover") {
      gFlyover = true;
      gFlyoverUserSet = true;
    } else if (arg == "--no-flyover") {
      gFlyover = false;
      gFlyoverUserSet = true;
    } else if (arg == "--flyover") {
      gFlyover = true;
      gFlyoverUserSet = true;
    } else if (arg == "--no-flyover") {
      gFlyover = false;
      gFlyoverUserSet = true;
    }
  }
  if (OceanSceneParseArgs(argc, argv) != EXIT_SUCCESS)
    return EXIT_FAILURE;
  if (PoolSceneParseArgs(argc, argv) != EXIT_SUCCESS)
    return EXIT_FAILURE;
  if (PoleSceneParseArgs(argc, argv) != EXIT_SUCCESS)
    return EXIT_FAILURE;
  if (gShotPath != nullptr) {
    OceanSceneSetScreenshot(gShotPath);
    PoolSceneSetScreenshot(gShotPath);
    PoleSceneSetScreenshot(gShotPath);
  }
  if (gShotPath != nullptr) {
    OceanSceneSetShotTime(gShotTime);
    PoolSceneSetShotTime(gShotTime);
    PoleSceneSetShotTime(gShotTime);
  }

  printf("ElectroBench version 0.4 build R1 (Release) (2026-10-09)\n");

  {
    static const char *kKey[] = {
        "shaders/ps14/sea_frag.glsl", "shaders/ps14/sky_frag.glsl",
        "shaders/pool/water_frag.glsl", "shaders/pole/object_frag.glsl"};
    char *b = SDL_GetBasePath();
    printf("Assets: exe dir %s| cwd wins over the exe dir when it has shaders/\n",
           b ? b : "(unknown)");
    SDL_free(b);
    for (int i = 0; i < 4; i++) {
      const std::string resolved = resolveAssetPath(kKey[i]);
      printf("  %-32s %7ld bytes  %s\n", kKey[i], assetSizeOf(resolved),
             resolved.c_str());
    }
  }
  if (assetTraceSelected())
    printf("Assets: (--trace-assets) every resolved path is printed as it loads\n");
  fflush(stdout);

  if (gPoleOnly) {
    PoleSceneSetStandalone(true);
    bool gaveUp = false;
    int rc = RunPoleScene(&gaveUp);
    if (rc == 1) {
      fprintf(stderr, "ElectroBench: no OpenGL 3.3 core context on this device - "
                      "the power-lines (scene 4) scene cannot run here\n");
      return EXIT_FAILURE;
    }
    return 0;
  }

  if (gPoolOnly) {
    PoolSceneSetStandalone(true);
    bool gaveUp = false;
    int rc = RunPoolScene(&gaveUp);
    if (rc == 1) {
      fprintf(stderr, "ElectroBench: no OpenGL 3.3 core context on this device - "
                      "the pool room scene cannot run here\n");
      return EXIT_FAILURE;
    }
    return 0;
  }

  if (gSceneOnly) {
    OceanSceneSetStandalone(true);
    bool gaveUp = false;
    int rc = RunOceanScene(&gaveUp);
    if (rc == 1) {
      fprintf(stderr, "ElectroBench: no OpenGL 3.3 core context on this device - "
                      "the ocean scene cannot run here\n");
      return EXIT_FAILURE;
    }
    return 0;
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
