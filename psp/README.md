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

`EBOOT.PBP` draws the dusk sky, the sea (body + additive reflection +
glitter) and the HUD at 333/333/166 MHz. Verified under PPSSPP (SDL/GLES) by
rendering a frame and screenshotting it: no GE errors, no rejected block
transfers, both framebuffers presenting. The 45 s benchmark loop and the
`ms0:/ElectroBench-psp.txt` result file are wired the same as the other ports.

`DATA.PSP` carries a **PRX** (`BUILD_PRX = 1`), not the ELF that earlier builds
packed. The ELF form is what the console rejected with
`The game could not be started. (8002013C)` - see note 7.

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
2. **`sceGumPerspective`/`sceGumLookAt` do not draw here.** With this
   toolchain a probe triangle 25 units in front of the eye never appears with
   them, in degrees or radians, while the same matrices loaded by hand with
   `sceGuSetMatrix` render immediately. `SetSceneMatrix()` therefore builds the
   projection and view explicitly (row-vector form: `w' = z`, `z' = z*A + B`
   with near -> 0). Only `sceGumOrtho` is still used, for the sky and HUD.
3. **The depth buffer is inverted-Z: use `GU_GEQUAL`.** `sceGuDepthRange(65535,
   0)` with `sceGuClearDepth(0)` - the pairing every PSPSDK sample uses - means
   nearer fragments carry *larger* depth values. With `GU_LEQUAL` every
   fragment loses against the cleared 0, so any pass that leaves the depth test
   enabled (the entire sea) is silently discarded while the depth-disabled sky
   and HUD still draw. `sceGuDepthMask(GU_TRUE)` *disables* depth writes, so the
   opaque body pass takes `GU_FALSE` and the additive passes `GU_TRUE`.
4. **`sceGumOrtho` takes (left, right, BOTTOM, top).** Passing `(…, 1, -1, …)`
   draws clip-space y=+1 at the bottom of the screen, which flips the whole sky
   and puts its sun in the wrong place.
5. **Do not copy textures into VRAM with `sceGuCopyImage`.** The GE samples
   textures straight out of main RAM, which is what the PSPSDK `blit` sample
   does (a static RAM array with `sceGuTexImage`). A block transfer with the
   destination/pitch given the wrong way is rejected, and a rejected transfer
   stops the display list, so every draw after it disappears. Textures are
   therefore sampled in place; `sceKernelDcacheWritebackAll()` after rebuilding
   them is all that is needed.
6. `sceGuDrawArray` does not upload GUM matrices; `sceGumUpdateMatrix()` does.
   (Only matters for `sceGuDrawArray`; `sceGumDrawArray` pushes them itself.)
7. **Pack a PRX, not the ELF.** This Makefile originally left `BUILD_PRX`
   unset, so PSPSDK's build.mak packed the stripped *ELF* into `DATA.PSP`
   (its first four bytes were `\x7fELF`). PPSSPP loads that - it sniffs an ELF
   in the PBP - and every emulator screenshot looked fine, so the mistake only
   showed up on a console, where the XMB loader hands that blob to the module
   manager, which cannot read a `~PSP` module header out of it and fails with
   `8002013C` (`SCE_KERNEL_ERROR_LIBRARY_NOTFOUND`). With `BUILD_PRX = 1` the
   link uses `prxspecs` + `linkfile.prx`, `psp-prxgen` emits an `ET_SCE_PRX`
   with a real `.rodata.sceModuleInfo`, and *that* is what `DATA.PSP` holds.
   Check with `psp-readelf -h ElectroBench.prx`: `Type: (ffa0)` is correct; a
   plain `EXEC` means the PBP will not boot on hardware even if it runs in an
   emulator.

   The PRX stays unsigned, which every CFW (PRO/ARK) accepts; only OFW would
   need a Sony key we do not have.
