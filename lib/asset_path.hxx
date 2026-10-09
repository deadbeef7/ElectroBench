// Asset path resolution shared by all four scenes.
//
// Assets (OBJ models, shader sources) live in the project root, while the build
// places binaries in build/. When a path does not exist relative to the current
// working directory (repo checkout / make builds), try the executable's
// directory and its parent so "./build/ElectroBench" just works.
#pragma once

#include <cstdio>
#include <fstream>
#include <string>

#include <SDL2/SDL.h>

// BUILD-P25: opt-in tracing of every resolved asset path. Shaders load at
// RUNTIME, so a stale shaders/ folder produces a current-binary/old-shader
// mixture that looks like neither build. See main.cxx's --trace-assets.
inline bool &assetTraceFlag() {
  static bool on = false;
  return on;
}
inline bool assetTraceSelected() { return assetTraceFlag(); }

inline bool assetFileExists(const std::string &p) {
  std::ifstream f(p);
  return f.good();
}

// Returns the first variant of the path that exists: CWD-relative, then
// relative to the executable's parent directory, then the executable's own
// directory. Falls back to the original path so the caller reports its usual
// "file not found" error.
//
// The CWD-first order is a real trap: run the exe from a checkout that still
// holds an old shaders/ folder and THAT folder wins, silently ignoring a
// freshly copied one next to the exe. The trace exists so that is visible
// instead of inferred from a frame that matches nothing.
inline std::string resolveAssetPath(const char *path) {
  const std::string p(path);
#ifdef ELECTROBENCH_VITA
  // PS Vita port (vitaport/): assets are packed into the .vpk and read from the
  // app0: mount, so there is no CWD-relative or exe-relative search to do --
  // and no SDL_GetBasePath() to do it with either. The path is still existence
  // checked so a missing shader reports the same way as on desktop.
  const std::string fromApp = "app0:/" + p;
  if (assetFileExists(fromApp)) {
    if (assetTraceSelected()) std::printf("  asset %-34s -> %s\n", path, fromApp.c_str());
    return fromApp;
  }
  if (assetTraceSelected()) std::printf("  asset %-34s -> NOT FOUND (%s)\n", path, fromApp.c_str());
  return fromApp;
#else
  if (assetFileExists(p)) {
    if (assetTraceSelected()) std::printf("  asset %-34s -> %s\n", path, p.c_str());
    return p;
  }

  char *base = SDL_GetBasePath();
  if (base) {
    const std::string exeDir(base);
    SDL_free(base);
    const std::string fromParent = exeDir + "../" + p;
    if (assetFileExists(fromParent)) {
      if (assetTraceSelected()) std::printf("  asset %-34s -> %s\n", path, fromParent.c_str());
      return fromParent;
    }
    const std::string fromExeDir = exeDir + p;
    if (assetFileExists(fromExeDir)) {
      if (assetTraceSelected()) std::printf("  asset %-34s -> %s\n", path, fromExeDir.c_str());
      return fromExeDir;
    }
  }
  if (assetTraceSelected()) std::printf("  asset %-34s -> NOT FOUND\n", path);
  return p;
#endif
}

// Byte size of an ALREADY-RESOLVED path, or -1. Takes the resolved path rather
// than the logical one so a caller that prints the path and its size together
// resolves once: resolving twice would print each asset line twice under
// --trace-assets, and a diagnostic nobody can read is no diagnostic.
inline long assetSizeOf(const std::string &resolvedPath) {
  std::ifstream f(resolvedPath, std::ios::binary | std::ios::ate);
  if (!f.good()) return -1;
  return static_cast<long>(f.tellg());
}