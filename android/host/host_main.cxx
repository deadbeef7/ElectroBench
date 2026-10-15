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
#include <cstdio>
#include <cstdlib>

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

int main(int argc, char **argv) {
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
