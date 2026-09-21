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
bool gDollySet = false;

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

// Scene layout: the 90 UZIs stand on the floor in a 10x9 grid
int grid_rows = 9, grid_cols = 10;
float grid_spacing = 0.68f;
float gun_scale = 1.0f;
float sun_dir_world[3];

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

// Draws the 90 UZIs lying flat on the floor (mag base touching the ground),
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
  float pv[16], lm_colmajor[16];
  mat4Multiply(lproj, lview, pv);
  mat4Multiply(bias, pv, lm_colmajor);
  // mat4Multiply treats its inputs/outputs as row-major (m[row*4+col]), but
  // glUniformMatrix4fv with transpose=GL_FALSE expects COLUMN-major data.
  // Upload the transpose so the shader receives bias*proj*view as written.
  // (This bug made the shadow lookup sample the wrong texels: most of the
  // floor read depth 1.0 and rendered fully lit, so only a few guns showed
  // shadows.)
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++)
      light_matrix[c * 4 + r] = lm_colmajor[r * 4 + c];
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

// Renders the scene (90 UZIs on a shadowed floor!) and calculates FPS
void renderScene() {
  unsigned int timet = SDL_GetTicks();
  applyCamera();

  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // Ground pass
  glUseProgram(groundProg);
  uploadCommonUniforms(groundProg);
  glUniform3fv(glGetUniformLocation(groundProg, "uSunDirWorld"), 1,
               sun_dir_world);
  glUniform3fv(glGetUniformLocation(groundProg, "uSkyColor"), 1, sky_color);
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
  SDL_GL_SwapWindow(window);

  // Headless screenshot capture
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

  frame++;
  final_time = time(NULL);
  if (final_time - init_time > 0) {
    char title[256];
    fps = frame / (final_time - init_time);
    snprintf(title, 256, "ElectroBench - FPS : %d", fps);
    SDL_SetWindowTitle(window, title);
    frame = 0;
    init_time = final_time;
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
          c[col] += light_matrix[r * 4 + col] * p[r];
      float ndcx = c[0] / c[3], ndcy = c[1] / c[3], ndcz = c[2] / c[3];
      float ux = ndcx * 0.5f + 0.5f, uy = ndcy * 0.5f + 0.5f, uz = ndcz * 0.5f + 0.5f;
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
    printf("Benchmark Results - Score : %f\n", (fps * 2) / (1.01 / fps));
    SDL_Quit();
    exit(0);
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

  // fixed warm sun from the upper left-front. Elevation is high enough that
  // each gun's shadow (~0.47 units, ~0.7x its height) stays compact and stays
  // attached to its own contact point instead of sliding under the next row
  // of guns, so every UZI shows its own shadow on the floor.
  float sd[3] = {-0.62f, 0.68f, 0.35f};
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
    }
  }

  initialiseWindow();
  glewInit();

  setup();
  changeSize(gWinW, gWinH);

  SDL_Event event;
  bool quit = false;

  init_time = time(NULL);

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
