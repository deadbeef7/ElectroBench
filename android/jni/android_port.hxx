// ===========================================================================
// Shared declarations for the ElectroBench Android port.
//
// android/jni/android_main.cxx owns the process (entry point, assets, scene
// order, log files); android/jni/android_sdl.cxx owns the SDL subset the scenes
// are written against (EGL/GLES3 window + context, input, clock, capture).
// Only this header is shared between them.
//
// The SDL subset itself is declared in include/SDL2/SDL.h — the same header the
// Vita port implements in vitaport/src/vita_sdl.cxx. Android reuses it verbatim
// because the contract is identical (one window, no window manager, WAIT: the
// Android window has a real size, which is why SDL_GetWindowSize here answers
// with the surface size instead of a fixed panel size).
// ===========================================================================
#pragma once

#include <cstdio>

// android_native_app_glue's app handle. Only a pointer is needed here, so the
// header stays free of the NDK's glue types.
struct android_app;

// Installs the glue's lifecycle and input callbacks and remembers the app
// handle. Called first thing by android_main, before anything touches the
// window or the GL device.
void PortAttachApp(struct android_app *app);

// logcat + the port's own log file, whichever of the two the platform allows.
void PortLog(const char *fmt, ...);

// Where the port may write: internalDir always exists (app-private), shareDir is
// the app's external files directory when the device has one. Shots and the
// stdout log go to shareDir so they can be pulled off the phone without a
// debugger, and fall back to internalDir when there is no external storage.
void PortSetDirs(const char *internalDir, const char *shareDir);
const char *PortShareDir(void);
FILE *PortShareOpen(const char *name, const char *mode);

// Screenshot schedule for the scene that is about to start: times are seconds
// after that scene's first presented frame. Shot N is written to
// <shareDir>/<scene label>_t<seconds>.png.
void PortBeginScene(const char *label, const double *shotTimes, int shotCount);

// 1 once the user asked to leave the whole app (BACK).
int PortQuitRequested(void);

// Presented frames so far, for the closing line of the port's log.
unsigned long PortFrameCount(void);
