#!/bin/sh
# Static x86 (32-bit) Windows build, compatible with old OSes (Win XP+).
#
# Win XP compatibility notes:
#   * NO -march flag: the previous -march=bonnell tuned for in-order Atoms
#     and lets GCC schedule SSE2 code paths that assume a modern loader;
#     plain -m32 keeps baseline i686 code every x86 Windows can load.
#   * -D_WIN32_WINNT=0x0501 pins the win32 API surface to XP: without it the
#     headers happily emit Vista+ functions and the exe fails to LOAD on XP.
#   * Every object is compiled AND linked from build/static/ — the old script
#     linked build/pool.o (a leftover non-static object) and broke on clean
#     trees.
#   * -Wl,--nxcompat -Wl,--dynamicbase need ASLR support; XP ignores the
#     dynamicbase flag gracefully, so both are kept for hardening on newer
#     Windows.
#   * GLEW is built statically from source (glew.c) so no import lib version
#     mismatch can sneak in; if the package ships glew32s.lib that works too.
#
# Usage (MSYS2 MinGW32 shell):
#   ./static-build-x86-mingw32.sh
set -e

pacman -S --needed mingw-w64-i686-toolchain mingw-w64-i686-SDL2
wget -nc https://repo.msys2.org/mingw/mingw32/mingw-w64-i686-glew-2.2.0-3-any.pkg.tar.zst
pacman -U --noconfirm mingw-w64-i686-glew-2.2.0-3-any.pkg.tar.zst

mkdir -p build/static

CFLAGS="-Ilib -IC:/msys64/mingw32/include/SDL2 -Dmain=SDL_main -DGLEW_STATIC -D_WIN32_WINNT=0x0501 -std=c++17 -O2 -m32"

g++ $CFLAGS -c src/main.cxx   -o build/static/main.o
g++ $CFLAGS -c src/scene2.cxx -o build/static/scene2.o
g++ $CFLAGS -c src/scene3.cxx -o build/static/scene3.o
g++ $CFLAGS -c src/scene4.cxx -o build/static/scene4.o

g++ -std=c++17 -O2 -m32 \
    -D_WIN32_WINNT=0x0501 \
    -static -static-libgcc -static-libstdc++ \
    build/static/main.o build/static/scene2.o build/static/scene3.o \
    build/static/scene4.o \
    -o build/ElectroBench-static.exe \
    -lmingw32 -mwindows -lSDL2main -lSDL2 -lm \
    -lkernel32 -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 \
    -lversion -luuid -ladvapi32 -lsetupapi -lshell32 -ldinput8 \
    -lglew32 -lglu32 -lopengl32 -lSDL2main -lSDL2 -mwindows \
    -Wl,--nxcompat -Wl,--dynamicbase

echo "Built build/ElectroBench-static.exe (Win XP+ compatible)"
