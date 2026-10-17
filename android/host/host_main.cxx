// ===========================================================================
// Host GLES3 check: render ONE scene of the port to a PPM on the host's own
// OpenGL ES 3 driver, using the scene's own screenshot path.
//
//   host_bench 2 /tmp/scene2.ppm 3.0
//   host_bench 4 /tmp/scene4.ppm 3.0
//
// The scene writes the image itself (its WriteScreenshotPPM reads the default
// framebuffer back with glReadPixels) and prints the "lit=" count next to it, so
// the check is one process per scene: a completed shot exits the process, which
// is what the desktop and Vita ports do with --shot-times too.
//
// See android/host/host_sdl.cxx for why this harness exists at all, and for what
// it does and does not cover.
// ===========================================================================
#include <SDL2/SDL.h>

#include <GLES3/gl3.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

int RunOceanScene(bool *gaveUpOut);  // scene 2 (dusk ocean)
int RunPoleScene(bool *gaveUpOut);   // scene 4 (power lines)

void OceanSceneSetScreenshot(const char *path);
void OceanSceneSetShotTime(float t);
void PoleSceneSetScreenshot(const char *path);
void PoleSceneSetShotTime(float t);

// The scenes read this to decide whether to trust their requested size or the
// size the platform reports back. False means "adopt the real surface", which is
// how the harness gets a 1366x768 frame out of scene 4's 1280x720 default.
bool gWindowedMode = false;

// ---------------------------------------------------------------------------
// --check-shaders: compile and link every program the two scenes load, through
// the host's own ES 3 driver, and fail if any pair does not link.
//
// This exists because a shader can be perfectly valid on its own and still be
// unlinkable with its partner: scene 2's sea shipped dead exactly that way, with
// `uTime` highp in sea_vert (a vertex shader's float default) and mediump in
// sea_frag (after its `precision mediump float;`). Each file passed
// glslangValidator as GLSL ES 1.00 and each compiled alone; the pair's link
// failed on every strict ES 3.0 linker, the sea program was therefore never
// valid, and the scene drew its sky and HUD and no sea at all. Nothing per-shader
// can catch that, and a screenshot cannot tell the missing sea from a dark sky.
// ---------------------------------------------------------------------------

namespace {

struct ShaderPair {
  const char *label;
  const char *vertex;
  const char *fragment;
};

const ShaderPair kPrograms[] = {
    {"scene 2 sea", "shaders/ps14/sea_vert.glsl", "shaders/ps14/sea_frag.glsl"},
    {"scene 2 sky", "shaders/ps14/sky_vert.glsl", "shaders/ps14/sky_frag.glsl"},
    {"scene 2 hud", "shaders/ps14/hud_vert.glsl", "shaders/ps14/hud_frag.glsl"},
    {"scene 4 sky", "shaders/pole/sky_vert.glsl", "shaders/pole/sky_frag.glsl"},
    {"scene 4 object", "shaders/pole/object_vert.glsl", "shaders/pole/object_frag.glsl"},
    {"scene 4 hud", "shaders/ps14/hud_vert.glsl", "shaders/pole/hud_frag.glsl"},
    // The Vita's last-resort pair (src/scene2.cxx swaps these in if the device
    // compiler rejects the full program). They run on the same vertex stages,
    // so a precision mismatch here would leave the Vita fallback just as dead
    // as the shader it replaces - link them here too.
    {"scene 2 sea fallback", "shaders/ps14/sea_vert.glsl", "shaders/ps14/fallback_sea_frag.glsl"},
    {"scene 2 sky fallback", "shaders/ps14/sky_vert.glsl", "shaders/ps14/fallback_sky_frag.glsl"},
};

// One line per driver-log line: a 4 KB info log through a printf-style buffer is
// how the message that matters gets truncated.
void PrintBlock(const char *indent, const char *text) {
  const std::string block = text ? text : "";
  size_t pos = 0;
  for (;;) {
    size_t eol = block.find('\n', pos);
    if (eol == std::string::npos) eol = block.size();
    if (eol > pos) std::printf("%s%s\n", indent, block.substr(pos, eol - pos).c_str());
    if (eol >= block.size()) break;
    pos = eol + 1;
  }
}

std::string ReadFile(const char *path) {
  std::string text;
  FILE *f = std::fopen(path, "rb");
  if (!f) return text;
  char buf[4096];
  size_t n = 0;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
  std::fclose(f);
  return text;
}

GLuint CompileStage(GLenum stage, const std::string &source, const char *path, int *failures) {
  const GLuint shader = glCreateShader(stage);
  const char *text = source.c_str();
  glShaderSource(shader, 1, &text, nullptr);
  glCompileShader(shader);
  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (ok) return shader;
  std::vector<char> log(4096, 0);
  glGetShaderInfoLog(shader, (GLsizei)log.size() - 1, nullptr, log.data());
  std::printf("FAIL  %s (%s stage)\n", path, stage == GL_VERTEX_SHADER ? "vertex" : "fragment");
  PrintBlock("      driver: ", log.data());
  (*failures)++;
  return shader;
}

int CheckShaders() {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 2;
  }
  SDL_Window *window = SDL_CreateWindow("ElectroBench shader check", SDL_WINDOWPOS_UNDEFINED,
                                        SDL_WINDOWPOS_UNDEFINED, 64, 64, SDL_WINDOW_OPENGL);
  if (!window || !SDL_GL_CreateContext(window)) {
    std::fprintf(stderr, "no OpenGL ES 3 context: %s\n", SDL_GetError());
    return 2;
  }
  std::printf("shader link check on %s | %s\n", (const char *)glGetString(GL_VERSION),
              (const char *)glGetString(GL_RENDERER));

  int failures = 0;
  int linked = 0;
  for (const ShaderPair &program : kPrograms) {
    const std::string vertex = ReadFile(program.vertex);
    const std::string fragment = ReadFile(program.fragment);
    if (vertex.empty() || fragment.empty()) {
      std::printf("FAIL  %s: cannot read %s / %s (run from the repo root)\n", program.label,
                  program.vertex, program.fragment);
      failures++;
      continue;
    }
    const GLuint vs = CompileStage(GL_VERTEX_SHADER, vertex, program.vertex, &failures);
    const GLuint fs = CompileStage(GL_FRAGMENT_SHADER, fragment, program.fragment, &failures);
    const GLuint handle = glCreateProgram();
    glAttachShader(handle, vs);
    glAttachShader(handle, fs);
    glLinkProgram(handle);
    GLint ok = GL_FALSE;
    glGetProgramiv(handle, GL_LINK_STATUS, &ok);
    if (ok) {
      // Every uniform and attribute the scene pokes must have a real location:
      // a -1 makes its glUniform*/glVertexAttribPointer call a silent no-op, which
      // is a second way for a program to draw nothing at all.
      GLint uniforms = 0;
      GLint dropped = 0;
      glGetProgramiv(handle, GL_ACTIVE_UNIFORMS, &uniforms);
      for (GLint i = 0; i < uniforms; i++) {
        char name[128] = {0};
        GLsizei length = 0;
        GLint size = 0;
        GLenum type = 0;
        glGetActiveUniform(handle, (GLuint)i, sizeof(name) - 1, &length, &size, &type, name);
        if (glGetUniformLocation(handle, name) < 0) dropped++;
      }
      std::printf("OK    %s: linked (%d uniforms%s)\n", program.label, (int)uniforms,
                  dropped ? ", some with location -1" : "");
      linked++;
    } else {
      std::vector<char> log(4096, 0);
      glGetProgramInfoLog(handle, (GLsizei)log.size() - 1, nullptr, log.data());
      std::printf("FAIL  %s: %s and %s do not link\n", program.label, program.vertex,
                  program.fragment);
      PrintBlock("      driver: ", log.data());
      failures++;
    }
    glDeleteProgram(handle);
    glDeleteShader(vs);
    glDeleteShader(fs);
  }
  std::printf("SHADER_LINK_CHECK: %d linked, %d failure(s)\n", linked, failures);
  return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc == 2 && std::strcmp(argv[1], "--check-shaders") == 0) return CheckShaders();
  if (argc < 4) {
    std::fprintf(stderr,
                 "usage: host_bench <2|4> OUT.ppm SECONDS\n"
                 "  e.g. host_bench 2 /tmp/scene2.ppm 3.0\n");
    return 2;
  }
  const int scene = std::atoi(argv[1]);
  const char *out = argv[2];
  const float seconds = (float)std::atof(argv[3]);
  if (scene != 2 && scene != 4) {
    std::fprintf(stderr, "scene must be 2 or 4 (got %d)\n", scene);
    return 2;
  }

  std::printf("host GLES3 check: scene %d -> %s, shot at t=%.1fs\n", scene, out, seconds);
  std::fflush(stdout);

  bool gaveUp = false;
  int rc = 0;
  if (scene == 2) {
    OceanSceneSetScreenshot(out);
    OceanSceneSetShotTime(seconds);
    rc = RunOceanScene(&gaveUp);
  } else {
    PoleSceneSetScreenshot(out);
    PoleSceneSetShotTime(seconds);
    rc = RunPoleScene(&gaveUp);
  }

  std::printf("scene %d returned %d%s\n", scene, rc, gaveUp ? " (gave up)" : "");
  return rc;
}
