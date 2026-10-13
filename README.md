# ElectroBench on the PlayStation Vita (scenes 2 and 4)

A homebrew port of **scene 2 (dusk ocean)** and **scene 4 (power lines / "lain")**
to the PS Vita, built as a single installable `ElectroBench-vita.vpk` that sits
at the repo root next to the code. Scenes 1 and 3 are not part of this port.

```
include/GL/glew.h      GLEW stand-in  -> vitaGL exports the same gl* symbols
include/SDL2/          SDL2 subset    (window, GL context, events, clock)
lib/                   shared scene helpers (asset paths, textures, font)
shaders/ps14, shaders/pole   GLSL 1.00 rewrites of the ported scenes' shaders
src/scene2.cxx         desktop scene translation unit, compiled unchanged
src/scene4.cxx
src/font_atlas.hxx     shared HUD font data and atlas layout
src/vita_main.cxx      entry point: runs scene 2 and scene 4 in one process
src/vita_sdl.cxx       SDL subset implementation over vitaGL + SceCtrl
tools/                 build helpers (kept on disk, untracked from git)
ElectroBench-vita.vpk  the installable homebrew
```

`lib/asset_path.hxx` gained one `#ifdef ELECTROBENCH_VITA` branch so the scenes
read assets from `app0:/` (the packed VPK) instead of walking the CWD.

**`tools/` is untracked from git on purpose** — it is build tooling only. It is
still used locally: `tools/build_deps.sh` (deps), `tools/make_icon.py` (the
LiveArea icon), `tools/check_shaders.sh` (shader validation) and
`tools/kaggle-vita3k.ipynb` (run the VPK under Vita3K on a GPU cloud box).

## Requirements

* **vitaSDK** (autobuild, x86_64 Linux) at `$VITASDK` — for the toolchain, the
  `vita-*` packaging tools and the Sce* import stubs. The 2026 autobuild
  binaries need a host with **glibc >= 2.38**, i.e. Ubuntu 24.04 or newer.
* **On the console:** `libshacccg.suprx` extracted to `ur0:/data/` — vitaGL
  compiles the shaders at runtime through SceShaccCg, so a console without it
  will fail to build a single shader. (Same prerequisite as any vitaGL
  homebrew; see vitaGL's README for the extraction guide.)
* `glslangValidator` on the host, only for `make check`.

## Build

From the repo root:

```sh
sh tools/build_deps.sh   # once: vitaGL + vitaShaRK + math-neon -> $VITASDK
make                     # -> ./ElectroBench-vita.vpk
make check               # shader validation only
```

`build_deps.sh` clones `Rinnegatamante/vitaGL`, `Rinnegatamante/vitaShaRK` and
`Rinnegatamante/math-neon` (none of them are packaged by `vdpm`: the public
`[vita]` repository only carries glm/libftpvita/vitagprof) and installs them into
`$VITASDK/arm-vita-eabi`. vitaShaRK needs one local shim: it includes
`<shacccg_ext.h>` and calls `sceShaccCgExt{Enable,Disable}Extensions()`, which
only exist in newer on-device `libshacccg.suprx` builds and are absent from
vitaSDK's `libSceShaccCg_stub.a`. The script injects a header plus no-op
definitions, which is the pre-extension code path older vitaGL releases used;
`shark_compile_shader_extended()` still drives SceShaccCg.

## Install and run

Copy `ElectroBench-vita.vpk` to `ux0:/` and install it with VitaShell (or any
VPK installer). **Hold L while launching to start with scene 4** instead of
scene 2.

| Input | Effect |
| --- | --- |
| hold **L** at launch | start with scene 4, then scene 2 |
| **SELECT** or **CIRCLE** | end the current scene, continue with the other one |
| **START** | quit the app |

The app exits after the second scene. `sceClibPrintf` output (which scene is
running, shader/compile diagnostics relayed from the scenes' stdout/stderr) is
visible in VitaShell's log or a debug console.

## How the port is put together

* **Window/context.** The Vita has one 960x544 panel and no window manager, so
  the SDL shim creates a single window at that size and `vglInitExtended()`
  takes over. `gWindowedMode` is defined `false` in `vita_main.cxx`, which makes
  both scenes adopt the reported size for their viewport, projection and HUD —
  they render natively instead of 1280x720 into a scaled surface.
* **Input.** The pad is edge-detected in `SDL_PollEvent()`; START becomes
  `SDL_QUIT`, SELECT/CIRCLE become the `SDLK_ESCAPE` keydown the scenes already
  treat as "leave this scene". So the scene loops run their real code paths.
* **MSAA.** Scene 4 asks for 4x MSAA, scene 2 for none; the requested samples
  select `SCE_GXM_MULTISAMPLE_4X` / `NONE` at `vglInitExtended()` time, and
  `SDL_GL_GetAttribute` reports back what was granted. (SceGxm has no 8x.)
* **Shaders.** `shaders/` holds GLSL 1.00 rewrites of the two ported scenes'
  shaders, hand-maintained on this branch. vitaGL rewrites
  `attribute`/`varying` into Cg for SceShaccCg and deletes `#version`,
  `#extension` and `precision` directives, but it does **not** understand
  `in`/`out`, `layout(location = N)`, `texture()`, `fwidth()` or `flat`; every
  occurrence in this tree has been rewritten to what vitaGL's translator
  accepts, and `make check` validates the result with `glslangValidator`.
* **Scene switching.** The two scenes run back to back in one process and the
  vitaGL device is *not* torn down between them (`SDL_Quit`/`SDL_GL_DeleteContext`
  are no-ops for GXM): re-initialising GXM mid-run is the riskier path, at the
  cost of scene 2's GL objects staying resident while scene 4 runs.

## Vita3K notes

Verified under Vita3K (`tools/kaggle-vita3k.ipynb` workflow): both scenes run
their full 45 s benchmark, the results screen shows, and the app exits cleanly
(`scene 2 returned 0`, `scene 4 returned 0` in the app's persistent spinlog at
`ux0:/data/ELEB00002/spinlog.txt`). Launch params understood by the port:

* `start=4` — start with scene 4 (Vita3K: `-Z 'start=4'` or a launch URI with
  `start=4`)
* `shot2=<seconds>` / `shot4=<seconds>` — schedule an in-scene RGBA
  `glReadPixels` dump to `ux0:/data/ELEB00002/vita_shot_scene{2,4}.ppm`
  (diagnostics prove the trigger path fires; see below for why the pixel
  export stays black under Vita3K)

## What was verified, and what was not

Verified:

* `make check` (`tools/check_shaders.sh`): every shader is valid **GLSL ES
  1.00** per `glslangValidator`, and no `in`/`out`/`layout`/`texture()`/
  `fwidth()`/`flat` declaration survives for vitaGL's translator.
* Clean `make`: compiles, links against `libvitaGL.a`, and
  `vita-elf-create`/`vita-make-fself`/`vita-pack-vpk` emit a module with
  `.sceModuleInfo.rodata`, `.sceLib.stubs`, `.sceImport.rodata` and a `SCE\0`
  SELF header.
* End-to-end under Vita3K with the above VPK: full benchmark cycle, both
  scenes return 0, results screen, clean exit.
* At the time of the Vita3K runs: `ffmpeg` frame dumps plus
  `Screenshot/PPM` results of the trigger path were **not** intested.

**Not verified:** nothing was executed on a real PS Vita. In-emulator
screenshots of the *scene pixels* remain unsolved: Vita3K's guest-side
`glReadPixels` on the default framebuffer reads zeros (RGBA/GL_RGB/GL_FRONT
tried; Vita3K owns the default framebuffer via `SceGxm` semantics and does not
route it back), the `F10` host screenshot key is not wired in the SDL
frontend, and X-window grabs of the Vita3K window return stale or blank
content (page-flipped GL/Vulkan surface without a compositor). If a shader
fails to compile on hardware, the app's own stderr text plus `printf`
diagnostics come out through `sceClibPrintf`, and the first thing to try is
`make check` after editing a shader in `shaders/`.
