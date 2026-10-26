// ===========================================================================
// Fake glew stand-in for the vita
#pragma once

#include <vitaGL.h>

#define GLEW_OK 0
#define GLEW_ERROR_NO_GL_VERSION 1
#define GLEW_VERSION_3_3 1

static inline int glewInit(void) { return GLEW_OK; }
static inline const char *glewGetErrorString(int err) { return err == GLEW_OK ? "no error" : "error"; }
static inline const unsigned char *glewGetString(unsigned int) { return (const unsigned char *)""; }

#ifndef GL_TEXTURE_WRAP_R
#define GL_TEXTURE_WRAP_R 0x8072
#endif
