// ===========================================================================
// SDL2 subset for the ElectroBench PS Vita port.
//
// The desktop scenes use SDL for exactly four things: a window + GL context, a
// polled event loop, a high-resolution timer, and error strings. The Vita has
// one screen, no window manager and no alternate GL contexts, so this header
// declares that subset and vitaport/src/vita_sdl.cxx implements it on top of
// vitaGL and SceCtrl.
//
// This is deliberately NOT SDL. There is one window, it is always the native
// 960x544 panel, its flags are accepted and ignored, GL attributes are recorded
// only for the ones that map onto SceGxm (depth bits, MSAA), and input is the
// Vita's pad, with
//     START  -> SDL_QUIT          (leave the app / current scene)
//     SELECT -> SDLK_ESCAPE       (the desktop "quit scene" key, also X/CIRCLE-free)
// ===========================================================================
#pragma once

#include <stdint.h>

typedef uint8_t Uint8;
typedef int32_t Sint32;
typedef uint32_t Uint32;
typedef uint64_t Uint64;

// -------------------------------------------------------------- window/GL bits
typedef struct SDL_Window SDL_Window;
typedef void *SDL_GLContext;

#define SDL_INIT_VIDEO 0x00000020u

#define SDL_WINDOWPOS_UNDEFINED 0x1FFF0000
#define SDL_WINDOW_FULLSCREEN 0x00000001u
#define SDL_WINDOW_OPENGL 0x00000002u
#define SDL_WINDOW_SHOWN 0x00000004u
#define SDL_WINDOW_RESIZABLE 0x00000020u
#define SDL_WINDOW_FULLSCREEN_DESKTOP (SDL_WINDOW_FULLSCREEN | 0x00001000u)

// GL attributes: the scenes request a 3.3 core context, a depth buffer and
// (scene 4 only) 4x MSAA. Only depth + MSAA change anything on the Vita; the
// version request is what the Vita port answers with "yes, you may render".
enum {
  SDL_GL_CONTEXT_MAJOR_VERSION = 1,
  SDL_GL_CONTEXT_MINOR_VERSION,
  SDL_GL_CONTEXT_PROFILE_MASK,
  SDL_GL_CONTEXT_PROFILE_CORE,
  SDL_GL_DOUBLEBUFFER,
  SDL_GL_DEPTH_SIZE,
  SDL_GL_MULTISAMPLEBUFFERS,
  SDL_GL_MULTISAMPLESAMPLES,
  SDL_GL_SAMPLES,
  SDL_GL_ACCELERATED_VISUAL,
};

// ---------------------------------------------------------------- event types
#define SDL_QUIT 0x100
#define SDL_WINDOWEVENT 0x200
#define SDL_KEYDOWN 0x300
#define SDL_KEYUP 0x301
#define SDL_WINDOWEVENT_RESIZED 1

// SDL2's keycode for Escape; beware the scenes' ProcessKeys() only checks for it.
#define SDLK_ESCAPE 27

typedef int32_t SDL_Keycode;

typedef struct SDL_Keysym {
  int32_t scancode;
  SDL_Keycode sym;
  uint16_t mod;
  uint32_t unused;
} SDL_Keysym;

typedef struct SDL_KeyboardEvent {
  uint32_t type;
  uint32_t timestamp;
  uint32_t windowID;
  uint8_t state;
  uint8_t repeat;
  uint8_t padding2;
  uint8_t padding3;
  SDL_Keysym keysym;
} SDL_KeyboardEvent;

typedef struct SDL_WindowEvent {
  uint32_t type;
  uint32_t timestamp;
  uint32_t windowID;
  uint8_t event;
  uint8_t padding1;
  uint8_t padding2;
  uint8_t padding3;
  Sint32 data1;
  Sint32 data2;
} SDL_WindowEvent;

typedef union SDL_Event {
  uint32_t type;
  SDL_KeyboardEvent key;
  SDL_WindowEvent window;
  uint8_t padding[56];
} SDL_Event;

// ------------------------------------------------------------------ functions
#ifdef __cplusplus
extern "C" {
#endif

int SDL_Init(Uint32 flags);
void SDL_Quit(void);

int SDL_GL_SetAttribute(int attr, int value);
int SDL_GL_GetAttribute(int attr, int *value);
SDL_GLContext SDL_GL_CreateContext(SDL_Window *window);
void SDL_GL_DeleteContext(SDL_GLContext context);
void SDL_GL_SwapWindow(SDL_Window *window);
int SDL_GL_SetSwapInterval(int interval);

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags);
void SDL_DestroyWindow(SDL_Window *window);
void SDL_SetWindowTitle(SDL_Window *window, const char *title);
void SDL_GetWindowSize(SDL_Window *window, int *w, int *h);
Uint32 SDL_GetWindowFlags(SDL_Window *window);

int SDL_PollEvent(SDL_Event *event);
const char *SDL_GetError(void);

Uint64 SDL_GetPerformanceCounter(void);
Uint64 SDL_GetPerformanceFrequency(void);

void SDL_Delay(Uint32 ms);
void SDL_free(void *mem);
char *SDL_GetBasePath(void);

#ifdef __cplusplus
}
#endif
