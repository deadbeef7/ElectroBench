# ElectroBench PSP

Scene 2 (the dusk ocean) rebuilt on the PSP's fixed-function Graphics Engine.
There are no shaders on the GE, so `psp/src/psp_scene2.cxx` does the wave field,
cloud shadows, fresnel and sea palette per vertex in float code, and the GE's
blend units carry the reflection/glitter passes additively. The HUD uses the
same 8x8 atlas as the Vita and Android ports.

## Build

```sh
export PSPDEV=/usr/local/pspdev
export PSPSDK=$PSPDEV/psp/sdk
export PATH=$PSPDEV/bin:$PATH
make -C psp            # -> psp/EBOOT.PBP
make -C psp clean
```

Needs a pspdev toolchain (`psp-gcc`, `mksfo`, `pack-pbp`, `psp-g++`).

## Status

`EBOOT.PBP` boots, draws the dusk sky, the sea (body + additive reflection +
glitter) and the HUD at 333/333/166 MHz. Verified under PPSSPP (SDL/GLES) by
rendering a frame and screenshotting it: no GE errors, no rejected block
transfers, both framebuffers presenting. The 45 s benchmark loop and the
`ms0:/ElectroBench-psp.txt` result file are wired the same as the other ports.

Not yet done: the on-screen score matches the GL slabs only approximately (the
GE cannot tone-map per pixel), and performance has only been measured under
emulation, not on hardware.

## Bring-up notes (bugs that cost the most time)

These all present the same way - the clear colour appears, nothing else - so
they are recorded here:

1. **GUM's MODEL matrix starts uninitialised.** The GE always transforms by
   world * view * projection. `sceGumLoadIdentity()` on PROJECTION and VIEW is
   not enough; the model stack must be set to identity too (done once in
   `GuInit`), or every draw collapses to a point while the clear still works.
2. **`sceGumPerspective` takes radians.** `Scene2FovY()` is degrees (the GL
   ports feed it to glm), so it must be converted before the call.
3. **Do not copy textures into VRAM with `sceGuCopyImage`.** The GE samples
   textures straight out of main RAM, which is what the PSPSDK `blit` sample
   does (a static RAM array with `sceGuTexImage`). A block transfer with the
   destination/pitch given the wrong way is rejected, and a rejected transfer
   stops the display list, so every draw after it disappears. Textures are
   therefore sampled in place; `sceKernelDcacheWritebackAll()` after rebuilding
   them is all that is needed.
4. `sceGuDrawArray` does not upload GUM matrices; `sceGumUpdateMatrix()` does.
   (Only matters for `sceGuDrawArray`; `sceGumDrawArray` pushes them itself.)
