// ===========================================================================
// GLEW stand-in for the ElectroBench PS Vita port.
//
// The desktop scenes include <GL/glew.h> to get the GL types, constants and
// 3.3-core entry points. On the Vita all of that comes from vitaGL, which
// exports the same gl* symbols (plus the small GL3-ish set these two scenes
// use: VAOs, FBOs/renderbuffers, mipmap generation, readback). So "loading the
// loader" is a no-op here and the scenes compile unchanged.
//
// Only the two call sites matter: `glewInit() != GLEW_OK` and the includes.
// ===========================================================================
#pragma once

#include <vitaGL.h>

// ---------------------------------------------------------------- GLEW bits
#define GLEW_OK 0
#define GLEW_ERROR_NO_GL_VERSION 1
#define GLEW_VERSION_3_3 1

static inline int glewInit(void) { return GLEW_OK; }
static inline const char *glewGetErrorString(int err) { return err == GLEW_OK ? "no error" : "error"; }
static inline const unsigned char *glewGetString(unsigned int) { return (const unsigned char *)""; }

// vitaGL has no cube-map wrap-R parameter. The scenes set it on their
// environment cube; GL semantics for the constant exist, so define it and let
// vitaGL ignore the unknown pname (its glTexParameteri only acts on the pnames
// it implements).
#ifndef GL_TEXTURE_WRAP_R
#define GL_TEXTURE_WRAP_R 0x8072
#endif
