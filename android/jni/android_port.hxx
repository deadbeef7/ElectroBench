#pragma once

#include <cstdio>

struct android_app;
void PortAttachApp(struct android_app *app);
void PortLog(const char *fmt, ...);
void PortSetDirs(const char *internalDir, const char *shareDir);
const char *PortShareDir(void);
FILE *PortShareOpen(const char *name, const char *mode);
void PortBeginScene(const char *label, const double *shotTimes, int shotCount);
int PortQuitRequested(void);
unsigned long PortFrameCount(void);
