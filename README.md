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

A full run under Vita3K (`tools/kaggle-vita3k.ipynb` workflow, or
`Vita3K --frontend sdl -B OpenGL -r ELEB00002`) takes roughly 2.5 minutes and
ends with the app exiting cleanly: the spinlog at
`ux0:/data/ELEB00002/spinlog.txt` shows `scene 2 returned 0` and
`scene 4 returned 0`, `ux0:/data/ELEB00002/frames.txt` counts the presented
frames (3330 on the last run), and each scene runs its full 45 s benchmark.
Give the machine an idle CPU for it: the emulated shader compiler is the whole
cost of a run (about 75 s for `sea_frag.glsl` alone), and a busy host turns
that into minutes.

Launch specs the port understands, taken from the app param (Vita3K's
`--launch-uri "psgm:play?titleid=ELEB00002&shot2=12"`) or, because a plain
`-r ELEB00002` launch passes no params at all, from the file
`ux0:/data/ELEB00002/autoshot.txt` (contents are parsed with the same tokens,
so an automated run can drop `start=4\nshot4=16\n` next to the app data before
booting it):

* `start=4` — start with scene 4 instead of scene 2
* `shot2=<seconds>` / `shot4=<seconds>` — schedule a one-shot RGBA
  `glReadPixels` dump of the scene's framebuffer to
  `ux0:/data/ELEB00002/vita_shot_scene{2,4}.ppm` and exit after writing it
  (the emulator itself then traps on `SIGTRAP` as the app leaves the benchmark
  early — the PPM is already on disk by then)

The capture reads `GL_FRONT` (the last frame the display queue presented) with
`GL_RGBA`/`GL_UNSIGNED_BYTE`, which is the one combination vitaGL answers with
a plain CPU copy of the display surface; its `GL_RGB` path goes through
`sceGxmTransferCopy`, which the emulator only stubs out and which would hand
back an untouched buffer.

### Reading Vita3K's shader translation

When a shader misbehaves, Vita3K writes everything needed to identify it under
`~/.cache/Vita3K/`:

* `shaderlog/<TITLE_ID>/<self>/v13-<sha256>.gxp` — the GXP the guest produced,
  with the uniform names still in it, so `strings` maps a hash to a port
  shader (`uCloudAzim`/`uCloudElev`/`uSkyEnvTex` -> `shaders/ps14/sky_frag.glsl`,
  and `uCloudPhase` -> `shaders/ps14/sea_frag.glsl`).
* `shaderlog/<TITLE_ID>/<self>/v13-<sha256>.dsm` — its USSE disassembly, which
  reports the registers each program really uses (`r20`-`r26` above).
* `shaders/<TITLE_ID>/<self>/v13-<sha256>.{frag,vert}` — the GLSL Vita3K
  generates for that program. It is **preloaded when present**, which is both
  how to inspect the translation and how to test a fix for an emulator-side
  limit without touching the port.

The hash in a `[compile_shader]` or `Error in get/compile` log line is the same
hash as those file names, which is what makes the mapping possible.

## What was verified, and what was not

Verified:

* `make check` (`tools/check_shaders.sh`): every shader is valid **GLSL ES
  1.00** per `glslangValidator`, and no `in`/`out`/`layout`/`texture()`/
  `fwidth()`/`flat` declaration survives for vitaGL's translator.
* Clean `make`: compiles, links against `libvitaGL.a`, and
  `vita-elf-create`/`vita-make-fself`/`vita-pack-vpk` emit a module with
  `.sceModuleInfo.rodata`, `.sceLib.stubs`, `.sceImport.rodata` and a `SCE\0`
  SELF header.
* End-to-end under Vita3K with the above VPK: full benchmark cycle in both
  scenes, 3330 frames presented, both scenes returning 0, and a clean exit.
  Nothing in the port's own control flow (window/context, pad edge detection,
  scene hand-over, timing, asset loading via `app0:/`) needed a workaround.
* The `shot2=`/`shot4=` trigger path: the shot fires at its scheduled sim time
  (`shot trigger: t=30.01 target=30.00` in the emulator log), writes a
  `P6 960 544` PPM and exits as intended.

**Not verified:** nothing was executed on a real PS Vita, and the scene
*pixels* could not be captured in the emulator either. Two independent
emulator-side limits, both measured on the current tree:

* **Vita3K cannot compile two of the port's programs, and drops their draws.**
  Its shader recompiler models the USSE temporary register bank as
  `vec4 r[20]` — `REG_TEMP_COUNT = 20 * 4` in
  `vita3k/shader/src/spirv_recompiler.cpp` — while SceShaccCg (running inside
  the emulator) compiles `shaders/ps14/sky_frag.glsl` and
  `shaders/ps14/sea_frag.glsl` into GXP that uses temporaries up to `r26`.
  Vita3K's own disassembly of those two GXPs shows it
  (`VMOV r20.xy`, `VMAD r24.xy`, `VMUL r26.x`), and its generated GLSL
  references `r[24]`/`r[25]` against the `r[20]` declaration, so the host
  compiler rejects it (`0:2336(2): error: array index must be < 20`) and every
  draw using those programs is skipped: `[draw]: Fail to get program!`
  7944-10437 times per full run, which is exactly scene 2's sky and sea.
  Scene 4's programs stay inside the bank and produce no dropped draws at all.
  Widening those two cached GLSL files locally drops the count to 0, which
  confirms the diagnosis; whether a real Vita accepts an `r20`-`r26`
  allocation is not something the public SceGxm material documents, so treat
  20 as Vita3K's own model rather than a hardware limit.
* **The frame it leaves behind is empty, not just hard to grab.** An
  `LD_PRELOAD` interceptor on `glXSwapBuffers`/`eglSwapBuffers`
  (`tools/glshot.c`, auto-capture every 2 s) reads the emulator's own back
  buffer *and* the front buffer after the swap for 960x544: for the whole of
  scene 2's benchmark, and for scene 4's 420 presented frames, every frame has
  **not one non-black pixel**, while the frames taken while the guest compiles
  shaders are byte-identical to each other (mean 2.13) and show Vita3K's own
  overlay. An X grab of the same window agrees, so the capture path does see
  whatever the emulator draws; it just draws none of this app. That holds with
  the drop count at 0 too, so it is not a second effect of the shader
  failure — the emulator's guest-to-display path produces nothing for this
  title in this environment. The port is not implicated: the same shaders pass
  `make check`, and `docs/screenshots/scene2_ps14_t1.ppm` is a desktop-GL
  render of exactly this scene tree.
* **The emulator never writes the display surface back to guest memory.** The
  guest-side readback of that surface returns exactly the zeros vitaGL writes
  into it at init, even after a `glClear` to a known colour immediately before
  the read, and whichever of `GL_FRONT`/`GL_BACK` is read. Vita3K keeps the
  surface (and its cached segments) inside its renderer and only hands it to
  the host display; `glReadPixels` from the guest cannot see it. So the
  `vita_shot_*.ppm` dump is a hardware-only check: expect it to be black under
  Vita3K, and to work on a console. The same emulator cache is why X-window
  and `ffmpeg` grabs of the Vita3K window show nothing useful, and why
  `--color-surface-debug` writes no PNG either. A scene pixel from the
  emulator therefore needs the draw-drop above fixed first — with the port's
  own shader source the only lever on it — or a real console.

If a shader fails to compile on hardware, the app's own stderr text plus
`printf` diagnostics come out through `sceClibPrintf`, and the first thing to
try is `make check` after editing a shader in `shaders/`.
