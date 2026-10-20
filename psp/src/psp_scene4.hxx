// ElectroBench — PlayStation Portable port, SCENE 4 (power lines / "lain").
#pragma once

// Builds the pole line, the road, the houses and the horizon, bakes their
// vertex lighting, and generates the dusk sky panorama and the sun sprite.
// One-time: the geometry is static, only the camera moves.
void Scene4Init();

// Advances the drive-along-the-road camera to t seconds.
void Scene4Update(double t);

// Camera readback in the convention psp_main.cxx's matrix builder uses:
// fwd = (sin(yaw)cos(pitch), sin(pitch), cos(yaw)cos(pitch)).
void Scene4Camera(float outEye[3], float *yaw, float *pitch);

// Scene 4's vertical field of view, in degrees (the desktop scene uses 52).
float Scene4FovY();

// Emits the sky panorama (depth test off, drawn under an orthographic
// projection by the caller) and the additive sun sprite.
void Scene4DrawSky();

// Emits the ground, the ground shadows, the poles/wires/houses and the HUD is
// left to the caller. Expects the perspective view/projection already set.
void Scene4Draw();
