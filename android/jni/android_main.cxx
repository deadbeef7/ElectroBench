// ===========================================================================
// ElectroBench on Android: entry point for the port that runs SCENE 2 (dusk
// ocean) and SCENE 4 (power lines / "lain").
//
// This file is to android/jni/android_sdl.cxx what src/vita_main.cxx is to
// vitaport/src/vita_sdl.cxx: it owns the process, decides which scene runs
// first, and hands the scenes a window/GL device through the same tiny SDL2
// subset both ports implement. The two scene translation units, the shader
// tree and the asset resolver are the desktop ones, compiled unchanged.
//
// What it has to do that the Vita port does not:
//
//   * Unpack the shader tree. On the Vita the .vpk mounts its own filesystem
//     (app0:) and the scenes read shaders straight out of it. An APK does not
//     mount: assets are inside the zip, and the scenes load shaders with
//     std::ifstream. So assets/shaders/** is copied into the app's files
//     directory at startup and SDL_GetBasePath() answers with that directory,
//     which is the path resolveAssetPath() already tries last.
//   * Keep the screen awake and the app immersive: a 2x45 s benchmark whose
//     display dims halfway through is not a benchmark.
//   * Put the run where it can be read. There is no console on a phone, so
//     stdout/stderr (the scenes print their FPS and score there) go to
//     <app files>/stdout.txt, the port's own trace goes to portlog.txt, and
//     frames are captured as PNGs - see android_sdl.cxx for why the capture
//     lives in the presentation path.
//
// Controls
//   BACK                     quit the app
//   tap / A / ENTER / SPACE  end the current scene early (the desktop Esc key)
//
// Optional run spec, written to <app files>/electrobench.txt (the external app
// files directory when the device has one). One "key=value" per line:
//
//   start=2|4     which scene runs first (default 2, then the other one)
//   only=2|4      run just that scene
//   shots=off     no captures
//   shots=10,45.1,47   capture at those seconds after the scene's first frame
//                      (default 45.1, 47, 48.5: the benchmark is 45 s and the
//                      results screen then holds for 4 s)
//
// Verify with: adb shell ls /sdcard/Android/data/<package>/files
// ===========================================================================
#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/looper.h>
#include <android/native_activity.h>
#include <android/window.h>  // AWINDOW_FLAG_FULLSCREEN / _KEEP_SCREEN_ON
#include <android_native_app_glue.h>
#include <jni.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "android_port.hxx"

// --------------------------------------------------------------- scene entries
// Same signatures src/vita_main.cxx declares.
int RunOceanScene(bool *gaveUpOut);  // scene 2 (dusk ocean)
int RunPoleScene(bool *gaveUpOut);   // scene 4 (power lines)

// Defined by the desktop main.cxx, which is not part of this build. The scenes
// read it to decide whether to trust their own requested window size or the size
// the platform reports back: false means "adopt the real surface", which is what
// makes them render at the device's native resolution.
bool gWindowedMode = false;

// The glue's android_main() runs on its own thread; the SDL subset reaches the
// activity, the window and the input queue through this.
struct android_app *gApp = nullptr;

namespace {

const int kMaxShots = 8;

struct LaunchSpec {
  int firstScene = 2;
  bool onlyFirst = false;
  double shots[kMaxShots] = {45.1, 47.0, 48.5};
  int shotCount = 3;
};

// ------------------------------------------------------------------- JNI bits
// Everything the port needs from the Java side is read here, from the activity
// the glue hands us: the external files directory (so shots and logs can be
// pulled off the device without a debugger) and the package name (so the log
// says where to look). Failures are never fatal - the port falls back to the
// app-private directory.
struct ActivityPaths {
  std::string packageName;
  std::string externalFilesDir;
};

std::string JavaString(JNIEnv *env, jobject obj, jmethodID mid) {
  jstring s = (jstring)env->CallObjectMethod(obj, mid);
  if (!s) return std::string();
  const char *chars = env->GetStringUTFChars(s, nullptr);
  const std::string out = chars ? chars : "";
  if (chars) env->ReleaseStringUTFChars(s, chars);
  env->DeleteLocalRef(s);
  return out;
}

ActivityPaths QueryActivity(ANativeActivity *activity) {
  ActivityPaths paths;
  if (!activity || !activity->vm || !activity->clazz) return paths;

  JNIEnv *env = nullptr;
  bool attached = false;
  if (activity->vm->GetEnv((void **)&env, JNI_VERSION_1_6) != JNI_OK) {
    if (activity->vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return paths;
    attached = true;
  }

  jclass cls = env->GetObjectClass(activity->clazz);
  if (jmethodID getPackage = env->GetMethodID(cls, "getPackageName", "()Ljava/lang/String;")) {
    paths.packageName = JavaString(env, activity->clazz, getPackage);
  }
  if (jmethodID getExt = env->GetMethodID(cls, "getExternalFilesDir",
                                         "(Ljava/lang/String;)Ljava/io/File;")) {
    jobject file = env->CallObjectMethod(activity->clazz, getExt, (jstring) nullptr);
    if (file) {
      jclass fileCls = env->GetObjectClass(file);
      if (jmethodID abs = env->GetMethodID(fileCls, "getAbsolutePath", "()Ljava/lang/String;")) {
        paths.externalFilesDir = JavaString(env, file, abs);
      }
      env->DeleteLocalRef(fileCls);
      env->DeleteLocalRef(file);
    }
  }
  if (env->ExceptionCheck()) env->ExceptionClear();
  env->DeleteLocalRef(cls);
  if (attached) activity->vm->DetachCurrentThread();
  return paths;
}

// Fullscreen + keep the screen on: the display would otherwise dim halfway
// through the 45 s benchmark, and the status bar would eat the top of it.
//
// ANativeActivity_setWindowFlags is documented as callable from any thread (it
// posts to the Java main thread), which is what makes it usable here: this runs
// on the glue's own android_main thread, not on the UI thread. Hiding the
// navigation bar as well would take setSystemUiVisibility on the decor view,
// which a non-UI thread may not touch and which would need a Java (dex) side to
// post - so the navigation bar stays, and the scenes simply get a surface that
// is inset by it. See the README for that trade-off.
void ApplyWindowFlags(ANativeActivity *activity) {
  if (!activity) return;
  ANativeActivity_setWindowFlags(activity, AWINDOW_FLAG_KEEP_SCREEN_ON | AWINDOW_FLAG_FULLSCREEN, 0);
}

// ------------------------------------------------------------------ filesystem
void MakeDir(const std::string &path) { mkdir(path.c_str(), 0770); }

// An APK's asset directory lists files only - AAssetDir cannot walk into a
// subdirectory - so the port spells the tree out instead of recursing. If a
// scene ever loads a non-shader asset, its directory belongs in this list.
const char *kAssetDirs[] = {"shaders", "shaders/ps14", "shaders/pole"};

int CopyAssetTree(AAssetManager *assets, const char *internalDir) {
  if (!assets) return 0;
  int copied = 0;
  for (const char *dir : kAssetDirs) {
    const std::string dest = std::string(internalDir) + "/" + dir;
    MakeDir(dest);
    AAssetDir *handle = AAssetManager_openDir(assets, dir);
    if (!handle) continue;
    for (const char *name = AAssetDir_getNextFileName(handle); name != nullptr;
         name = AAssetDir_getNextFileName(handle)) {
      const std::string from = std::string(dir) + "/" + name;
      AAsset *asset = AAssetManager_open(assets, from.c_str(), AASSET_MODE_BUFFER);
      if (!asset) continue;
      const void *data = AAsset_getBuffer(asset);
      const off_t length = AAsset_getLength(asset);
      const std::string to = dest + "/" + name;
      FILE *out = std::fopen(to.c_str(), "wb");
      if (out && data != nullptr) {
        // Always rewritten rather than timestamp-compared: 12 files of GLSL are
        // cheaper to copy than the class of bug that a stale shader causes (see
        // the shader-cache note in the README).
        if (std::fwrite(data, 1, (size_t)length, out) == (size_t)length) copied++;
      }
      if (out) std::fclose(out);
      AAsset_close(asset);
    }
    AAssetDir_close(handle);
  }
  return copied;
}

std::string ReadTextFile(const std::string &path) {
  std::string text;
  FILE *f = std::fopen(path.c_str(), "rb");
  if (!f) return text;
  char buf[512];
  size_t n = 0;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
  std::fclose(f);
  return text;
}

// ------------------------------------------------------------------ run spec
bool ParseShots(const std::string &value, LaunchSpec *spec) {
  if (value == "off" || value == "none" || value == "0") {
    spec->shotCount = 0;
    return true;
  }
  spec->shotCount = 0;
  const char *p = value.c_str();
  while (*p) {
    char *end = nullptr;
    const double v = std::strtod(p, &end);
    if (end == p) return false;
    if (v > 0.0 && spec->shotCount < kMaxShots) spec->shots[spec->shotCount++] = v;
    p = end;
    if (*p == ',') p++;
    else if (*p != '\0') return false;
  }
  return true;
}

LaunchSpec ReadLaunchSpec(const std::string &shareDir, const std::string &internalDir,
                          std::string *where) {
  LaunchSpec spec;
  std::string text = ReadTextFile(shareDir + "/electrobench.txt");
  *where = shareDir + "/electrobench.txt";
  if (text.empty()) {
    text = ReadTextFile(internalDir + "/electrobench.txt");
    *where = internalDir + "/electrobench.txt";
  }
  if (text.empty()) return spec;  // defaults

  size_t pos = 0;
  while (pos < text.size()) {
    size_t eol = text.find('\n', pos);
    if (eol == std::string::npos) eol = text.size();
    std::string line = text.substr(pos, eol - pos);
    pos = eol + 1;
    const size_t hash = line.find('#');
    if (hash != std::string::npos) line = line.substr(0, hash);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = line.substr(0, eq);
    const std::string value = line.substr(eq + 1);
    if (key == "start" || key == "only") {
      const int scene = std::atoi(value.c_str());
      if (scene == 2 || scene == 4) spec.firstScene = scene;
      if (key == "only" && (scene == 2 || scene == 4)) spec.onlyFirst = true;
    } else if (key == "shots") {
      if (!ParseShots(value, &spec)) PortLog("unusable shots line: %s", line.c_str());
    }
  }
  return spec;
}

// ------------------------------------------------------------------- lifecycle
// Blocks until the activity has a window (or is going away). The scenes create
// their GL context inside their first frame and have no retry path of their own,
// so the port must not start one before the surface exists.
void WaitForWindow() {
  while (!gApp->window && !gApp->destroyRequested) {
    int events = 0;
    struct android_poll_source *source = nullptr;
    if (ALooper_pollAll(-1, nullptr, &events, (void **)&source) >= 0 && source) {
      source->process(gApp, source);
    }
  }
}

void PumpUntilDestroyed() {
  while (!gApp->destroyRequested) {
    int events = 0;
    struct android_poll_source *source = nullptr;
    if (ALooper_pollAll(-1, nullptr, &events, (void **)&source) >= 0 && source) {
      source->process(gApp, source);
    }
  }
}

}  // namespace

void android_main(struct android_app *app) {
  gApp = app;
  PortAttachApp(app);

  WaitForWindow();
  if (!app->window) {
    PortLog("no window and the activity is going away; nothing to do");
    return;
  }

  const std::string internalDir =
      app->activity && app->activity->internalDataPath ? app->activity->internalDataPath : "";
  if (internalDir.empty()) {
    PortLog("no internal data path; cannot unpack the shader tree");
    ANativeActivity_finish(app->activity);
    return;
  }

  const ActivityPaths paths = QueryActivity(app->activity);
  PortSetDirs(internalDir.c_str(), paths.externalFilesDir.c_str());
  PortLog("package %s", paths.packageName.empty() ? "(unknown)" : paths.packageName.c_str());
  PortLog("internal dir %s", internalDir.c_str());
  PortLog("shots and logs: %s", PortShareDir());

  // No console on a phone: the scenes' FPS/score lines are printf, so capture
  // them in the share directory where they can be read afterwards.
  if (FILE *out = PortShareOpen("stdout.txt", "w")) {
    const int fd = fileno(out);
    fflush(out);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    std::fclose(out);
    setvbuf(stdout, nullptr, _IOLBF, 0);
    PortLog("stdout/stderr -> %s/stdout.txt", PortShareDir());
  }

  ApplyWindowFlags(app->activity);

  if (app->config) {
    PortLog("device sdk=%d density=%d orientation=%d",
            AConfiguration_getSdkVersion(app->config), AConfiguration_getDensity(app->config),
            AConfiguration_getOrientation(app->config));
  }

  const int copied = CopyAssetTree(app->activity->assetManager, internalDir.c_str());
  PortLog("%d shader file(s) unpacked into %s", copied, internalDir.c_str());

  std::string configPath;
  const LaunchSpec spec = ReadLaunchSpec(PortShareDir(), internalDir, &configPath);
  PortLog("run spec from %s: start=%d %s shots=%d at", configPath.c_str(), spec.firstScene,
          spec.onlyFirst ? "(that scene only)" : "(then the other scene)", spec.shotCount);
  for (int i = 0; i < spec.shotCount; i++) PortLog("  shot %d: t=%.1fs", i + 1, spec.shots[i]);

  const int sceneCount = spec.onlyFirst ? 1 : 2;
  for (int i = 0; i < sceneCount; i++) {
    const int scene = i == 0 ? spec.firstScene : (spec.firstScene == 2 ? 4 : 2);
    PortBeginScene(scene == 2 ? "scene2" : "scene4", spec.shots, spec.shotCount);
    bool gaveUp = false;
    const int rc = scene == 2 ? RunOceanScene(&gaveUp) : RunPoleScene(&gaveUp);
    PortLog("scene %d returned %d%s", scene, rc, gaveUp ? " (gave up)" : "");
    // 2 means "the user asked to quit" (BACK), 0 means the scene fused on into
    // the next one; anything else still lets the other scene have its run.
    if (rc == 2 || PortQuitRequested()) {
      PortLog("quit requested; stopping after scene %d", scene);
      break;
    }
  }

  PortLog("benchmark finished after %lu presented frame(s); closing the activity",
          PortFrameCount());
  ANativeActivity_finish(app->activity);
  PumpUntilDestroyed();
  PortLog("android_main done");
}
