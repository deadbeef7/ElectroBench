// ===========================================================================
// GLEW stand-in for the ElectroBench Android port.
//
// The scenes include <GL/glew.h> for the GL types, constants and the 3.3-core
// entry points. Android has no desktop GL 3.3, but it does have OpenGL ES 3.0,
// and the whole surface the two scenes actually use — VAOs, FBOs/renderbuffers,
// glGenerateMipmap, glReadBuffer/glReadPixels, glBlitFramebuffer — is core ES
// 3.0. So "loading the loader" is a no-op here, exactly as on the Vita, and
// android/include/SDL2/SDL_opengl.h, android/jni/android_sdl.cxx and
// android/jni/android_main.cxx create the ES 3.0 context that makes these
// symbols resolve.
//
// This file only has to win the include order: the Android build puts
// android/include ahead of vitaport's include/ (whose GL/glew.h forwards to
// vitaGL), so the scenes compile untouched.
// ===========================================================================
#pragma once

#include <GLES3/gl3.h>

// ---------------------------------------------------------------- GLEW bits
#define GLEW_OK 0
#define GLEW_ERROR_NO_GL_VERSION 1
// The scenes only use this for a "we have the version we asked for" branch.
#define GLEW_VERSION_3_3 1

static inline int glewInit(void) { return GLEW_OK; }
static inline const char *glewGetErrorString(int err) { return err == GLEW_OK ? "no error" : "error"; }
static inline const unsigned char *glewGetString(unsigned int) { return (const unsigned char *)""; }

// GL_TEXTURE_WRAP_R is core ES 3.0, so unlike the Vita header this one does not
// have to define it — kept guarded anyway so a future GLES2-only build of this
// port compiles without a second edit point.
#ifndef GL_TEXTURE_WRAP_R
#define GL_TEXTURE_WRAP_R 0x8072
#endif
