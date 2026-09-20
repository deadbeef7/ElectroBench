// Asset path resolution shared by both benchmarks.
//
// Assets (OBJ models, shader sources) live in the project root, while CMake
// places the binaries in build/. When a path does not exist relative to the
// current working directory (repo checkout / make builds), try the
// executable's parent directory so "./build/ElectroBench" just works.
#pragma once

#include <fstream>
#include <string>

#include <SDL2/SDL.h>

inline bool assetFileExists(const std::string &p) {
  std::ifstream f(p);
  return f.good();
}

// Returns the first variant of the path that exists: CWD-relative, then
// relative to the executable's parent directory. Falls back to the original
// path so the caller reports its usual "file not found" error.
inline std::string resolveAssetPath(const char *path) {
  const std::string p(path);
  if (assetFileExists(p))
    return p;

  char *base = SDL_GetBasePath();
  if (base) {
    const std::string exeDir(base);
    SDL_free(base);
    const std::string fromParent = exeDir + "../" + p;
    if (assetFileExists(fromParent))
      return fromParent;
    const std::string fromExeDir = exeDir + p;
    if (assetFileExists(fromExeDir))
      return fromExeDir;
  }
  return p;
}
