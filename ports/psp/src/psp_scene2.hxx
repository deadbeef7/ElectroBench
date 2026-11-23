// ElectroBench — PlayStation Portable port, scene interface.
#pragma once

// VRAM byte offsets shared with psp_main.cxx.
//
// Only the frame/depth buffers live in EDRAM. The textures are sampled
// straight out of main RAM: the GE reads them from wherever sceGuTexImage
// points, and the official blit sample does exactly that from a static array.
// Copying them into VRAM with sceGuCopyImage instead is what wedged the GE --
// the block transfer is rejected (the destination and pitch must be given the
// way the hardware wants them), and a rejected transfer stops the display
// list, so every draw after the first textured one silently disappeared.
constexpr unsigned int kPspBufWidth = 512;
constexpr unsigned int kPspScreenH = 272;
constexpr unsigned int kPspFrameBytes = kPspBufWidth * kPspScreenH * 2;
constexpr unsigned int kPspDrawVram = 0;                     // colour, front
constexpr unsigned int kPspDispVram = kPspFrameBytes;        // colour, back
constexpr unsigned int kPspDepthVram = kPspFrameBytes * 2;   // depth

// One-time setup: math tables, grid buffers, glitter texture, cloud deck.
void Scene2Init();

// Advances the auto camera to time t (seconds since scene start).
void Scene2UpdateCamera(double t);

// CPU work for one frame: clouds, one band of the sky panorama, the ray-cast
// sky grid and every sea vertex's shading. Ends with a data-cache writeback
// because the GE DMAs these arrays out of RAM.
void Scene2Build(double timeSeconds);

// Emits the GL command list for the sky backdrop (depth test off).
void Scene2DrawSky();

// Emits the sea: opaque body, additive reflection, additive glitter.
void Scene2Draw();

// Camera readback for the projection.
void Scene2Camera(float outEye[3], float *yaw, float *pitch);
float Scene2FovY();
