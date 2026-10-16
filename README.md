# ElectroBench on the PlayStation Vita and Android (scenes 2 and 4)

Homebrew ports of **scene 2 (dusk ocean)** and **scene 4 (power lines /
"lain")** to the PS Vita and to Android. Scenes 1 and 3 are not part of either
port. Two installable artifacts sit at the repo root next to the code:
`ElectroBench-vita.vpk` and the sideloadable `ElectroBench-android.apk`.

```
include/GL/glew.h      GLEW stand-in  -> vitaGL exports the same gl* symbols
include/SDL2/          SDL2 subset    (window, GL context, events, clock)
lib/                   shared scene helpers (asset paths, textures, font)
shaders/ps14, shaders/pole   GLSL 1.00 rewrites of the ported scenes' shaders
src/scene2.cxx         desktop scene translation unit, compiled unchanged
src/scene4.cxx
src/font_atlas.hxx     shared HUD font data and atlas layout
src/vita_main.cxx      Vita entry point: runs scene 2 and scene 4 in one process
src/vita_sdl.cxx       SDL subset implementation over vitaGL + SceCtrl
tools/                 build helpers (kept on disk, untracked from git)
android/               Android port: NativeActivity APK, GLES3 + EGL, harness
ElectroBench-vita.vpk  the installable homebrew
ElectroBench-android.apk  the installable APK (same two scenes)
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

## Android port (same two scenes)

`ElectroBench-android.apk` is the same two scenes built for Android: a
**NativeActivity** app whose `libmain.so` is `android/jni`'s entry point plus the
two scene translation units from `src/`, compiled against the GLES3 and SDL2
subsets in `android/include` and packed with the same `shaders/` tree.

```
android/AndroidManifest.xml   NativeActivity, landscape, android:hasCode="false"
android/include/GL/glew.h     GLEW stand-in -> OpenGL ES 3.0 (not vitaGL)
android/include/SDL2/         SDL_opengl.h stand-in -> GLES3
android/jni/android_main.cxx  entry point: assets, paths, scene order, logs
android/jni/android_sdl.cxx   SDL subset over EGL/GLES3 + input + PNG capture
android/host/                 host OpenGL ES 3 render check (see below)
android/Makefile              APK build (NDK clang + aapt2 + zipalign + apksigner)
android/fetch_toolchain.sh    fetches the pinned NDK / build-tools / platform
android/make_android_icon.py  launcher icon (pure Python, no image library)
android/ppm_to_png.py         PPM -> PNG for the frames the checks produce
android/screenshots/          frames rendered by the host OpenGL ES 3 check
```

There is **no Java source, no `classes.dex` and no gradle step**: the framework's
own `android.app.NativeActivity` is the Java side, and it `dlopen`s `libmain.so`
and calls `android_main()` in it. A JDK is needed only for `apksigner` and
`keytool`. The APK is one ~3.4 MB file carrying `arm64-v8a`, `armeabi-v7a` and
`x86_64` (the last one so it can also be tried in an emulator).

### Build

```sh
sh android/fetch_toolchain.sh   # once: NDK r26d, build-tools 34.0.0, platform android-34
make -f android/Makefile \
     ANDROID_NDK_HOME=build-android/toolchain/ndk/android-ndk-r26d \
     ANDROID_SDK_ROOT=build-android/toolchain/sdk
make -f android/Makefile verify ...   # manifest, signature, ABIs, entry point
make -f android/Makefile check ...    # shader validation (same as the Vita port)
make -f android/Makefile host-shaders # link all six programs through host ES 3
```

The two environment variables are the ones `fetch_toolchain.sh` prints at the
end. `make -f android/Makefile doctor` says which tool it resolved to what.

`check` validates each shader on its own; `host-shaders` compiles and links every
program the two scenes load, which is the only check that can see the two stages
of one program disagree (see *What was verified, and what was not*). It needs no
device and takes about a second.

### Install and run

`adb install -r ElectroBench-android.apk`, or copy the APK to the device and tap
it. It is signed with a throwaway debug key generated into `build-android/`:
anything installed from it must be uninstalled before an APK signed with a
different key can replace it. The app runs scene 2's full 45 s benchmark and its
4 s results screen, then scene 4's, then closes itself.

| Input | Effect |
| --- | --- |
| **BACK** | quit the app |
| **tap**, **A**, **ENTER** or **SPACE** | end the current scene early (the desktop Esc key) |

A phone has no console, so the run reports itself into the app's files directory
(`/sdcard/Android/data/org.electrobench.android/files/` on a device with external
storage, the app-private directory otherwise):

* `stdout.txt` — the scenes' own output, including `Benchmark Results - Time :
  45.0s, Average FPS : N, Score : N` for each scene.* `portlog.txt` — the port's trace: EGL/ES version and the sample count it really
  got, how many shader files were unpacked (and each one's byte count), one line
  per capture, and the device report the two scenes cannot make themselves: the
  GPU vendor/renderer/GLSL version, the uniform/varying/texture limits, whether
  the extensions scene 2's RGBA16F environment cube needs are present, a
  compile-and-link verdict with the driver's own log for **all six** programs,
  their active uniforms/attributes with locations, and the first GL errors
  raised while rendering, decoded, with frame numbers. That is what turns "the
  sea does not show up" from a screenshot into a line of text.
* `shots/*.png` — captured by default at **t=45.1 s** (a clean frame from the end
  of the benchmark, after the score is computed), **t=47 s** and **t=48.5 s**
  (the results screen), the same three for scene 4. A capture happens inside
  `SDL_GL_SwapWindow`, before the frame is presented, and goes through a blit
  into a single-sample FBO — the only way ES 3.0 can read the 4x-MSAA default
  framebuffer scene 4 asks for. Every line logs `lit=<n>/<total>`, so "did
  anything get drawn" is answerable from the log alone.

Optional run spec at `<app files>/electrobench.txt`, one `key=value` per line:

```
start=4                # scene 4 first, then scene 2
only=2                 # just scene 2
shots=10,45.1,47       # capture at those seconds after the scene's first frame
shots=off              # no captures
```

### How it differs from the Vita port

* **GL level.** Android has no desktop GL 3.3, but it has OpenGL ES 3.0, which is
  where VAOs, `glReadBuffer`, `glFramebufferTexture2D` and `glBlitFramebuffer`
  come from — the whole surface the scenes actually use. The ES 3.0 context
  compiles the same `#version 100` shaders the Vita port does.
* **A context per scene.** `SDL_GL_DeleteContext` really tears the EGL device
  down here, so scene 4's 4x-MSAA request is answered by its own config rather
  than by scene 2's, and scene 2's GL objects do not stay resident into scene 4.
* **Assets are unpacked, not mounted.** A VPK mounts as `app0:`; an APK does
  not. The app copies `assets/shaders/**` into its files directory at startup and
  `SDL_GetBasePath()` answers with that directory, which is the last variant
  `resolveAssetPath()` already tries — `lib/asset_path.hxx` is untouched by this
  port.
* **The resolution is the device's.** `gWindowedMode` is false and
  `SDL_GetWindowSize()` returns the real surface, so both scenes render at the
  phone's native landscape resolution instead of the Vita's 960x544.
* **The navigation bar is not hidden.** Immersive mode needs
  `View.setSystemUiVisibility` on the UI thread, and with no dex there is no Java
  side to post that from; the manifest's `Theme.NoTitleBar.Fullscreen` plus
  `AWINDOW_FLAG_FULLSCREEN` hide the status bar only, so the scenes get a surface
  inset by the navigation bar. `AWINDOW_FLAG_KEEP_SCREEN_ON` is set, because a
  benchmark whose display dims halfway through is not a benchmark.

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

### What was verified for the Android port, and what was not

Verified on the committed `ElectroBench-android.apk`, by
`make -f android/Makefile verify`:

* `aapt2 dump badging` reads the binary manifest back as package
  `org.electrobench.android`, `sdkVersion:'24'`, `targetSdkVersion:'34'`,
  `launchable-activity: android.app.NativeActivity`, and
  `native-code: 'arm64-v8a' 'armeabi-v7a' 'x86_64'`.
* `apksigner verify` verifies it with APK Signature Scheme v2 **and** v3 (v1 is
  off by design at minSdk 24), one signer.
* Every `libmain.so` is an ELF shared object of the right class and machine,
  exports `ANativeActivity_onCreate` and `android_main`, and needs only
  `libEGL.so`, `libGLESv3.so`, `liblog.so`, `libandroid.so`, `libm.so`,
  `libdl.so`, `libc.so`. The C++ runtime is linked in
  (`-static-libstdc++`), so nothing else has to be shipped inside the APK.
* `make -f android/Makefile check`: every shader is valid GLSL ES 1.00, which is
  also what the ES 3.0 context in the APK compiles them as (`#version 100` is
  the default in an ES 3.0 context, and no `#version 300 es` rewrite is needed).

Verified by rendering the port's own code under a real OpenGL ES 3 driver: the
harness in `android/host/` (`make -f android/Makefile host-render`) runs the
**same** scene translation units and the **same** `shaders/` tree through Mesa's
llvmpipe on the host — ES 3.2, the API level the APK targets — and lets the
scenes write their own frames out. Both scenes come up and both produce full
frames: scene 2 `lit=1049088/1049088` and scene 4 `lit=1048911/1049088` at
1366x768. Scene 4's exact numbers drift by a pixel or two between runs, because
its camera advances on real elapsed time (`gSimTime`) rather than on the pinned
shot time scene 2 uses, so the frame it captures is a few hundredths of a second
apart each run; scene 2's frames are reproducible byte for byte.

**A `lit` count is exactly what hid the sea.** Scene 2's sea program did not link
at all in every revision before this one, so the sea contributed no pixels while
the sky dome filled the same rows and the frame still reported every pixel lit —
an entirely plausible "dusk sky over dark water" frame with no water in it. The
driver's reason, reproduced on the host:

```
Program shaders/ps14/sea_vert.glsl/shaders/ps14/sea_frag.glsl failed to link:
error: declarations for uniform `uTime` have mismatching precision qualifiers
```

`uTime` is the one uniform the sea's two stages share (the vertex stage displaces
the surface with it, the fragment stage scrolls foam and ripple with it). A vertex
shader's `float` defaults to `highp`, and `shaders/ps14/sea_frag.glsl`'s
`precision mediump float;` made its copy `mediump`, which an ES 3.0 linker must
reject; the sea program was therefore never valid, while the sky and HUD programs
linked fine because no uniform of theirs is shared. The fix is one explicit
`highp` on that declaration in the fragment stage; the sea's ALU stays `mediump`.
The Vita port packs this same `shaders/` tree (`-a shaders=shaders` in the root
Makefile), so the fix reaches it too — whether SceShaccCg was linking the pair into
a broken program or silently tolerating it is untested, since no Vita was
available.

`make -f android/Makefile host-shaders` closes the hole: it compiles and links all
six programs through the host's ES 3 driver, prints each verdict with the driver's
own log, and exits non-zero if any pair fails. Run against the revision that
shipped the sea-less frame it reproduces the failure above; on the fixed tree it
prints `SHADER_LINK_CHECK: 6 linked, 0 failure(s)`. Measured on the frame, the fix
changes 482,007 pixels — 45.9% of the image, every row from the horizon down —
while the sky above the horizon stays identical to within 0.1/255 on the mean. The
images are committed as `android/screenshots/scene2_dusk_ocean.png` (which
replaced a sea-less one, byte-identical to a fresh pre-fix render) and
`android/screenshots/scene4_power_lines.png`.

**Not verified:** the fixed APK has not been run anywhere. Nothing was executed
on a device or in an emulator *from this container* — no phone, no Android
emulator, no KVM — so the Android-specific layer — activity lifecycle,
`ANativeActivity`, `AInputQueue`, `eglCreateWindowSurface` on a real surface, the
external-files path, and the capture blit on a real driver — is covered by
compilation, link-time symbol and dependency checks, and the app's own logging,
but not by execution. That is exactly why the host harness above is part of the
repo: it is the closest thing to a pixel-level check available here, and it covers
the scenes and the shaders rather than the glue around them.

What *was* seen on a device is the symptom that led here: scene 2 rendered its sky
dome and its HUD, reported no error on screen, and showed no sea at all — no black
rectangle, just sky continuing down. That is this link failure as a user sees it.
The fix itself is host-verified (link check plus the 45.9% frame change above), so
confirming it on that device is the remaining step.

### Two more rendering bugs, in the shared shader tree

Two further defects showed up in the committed frames. Both are in `shaders/`,
which both ports pack, so both are fixed for both ports.

**Scene 2: a near-black band between the sea and the sky.** `sea_frag.glsl`
received the world position as a `mediump` varying, and the sea patch is
`kSeaSize` (4096) across, so that varying carries coordinates of ±2048 — past what
`mediump` holds. Interpolated at `mediump` the far half of the patch came apart:
probe renders that print `dist = length(vWorld.xz - uEyePos.xz)` show the value
pinned at ≥16384 (`mediump`'s own maximum) for fragments whose `vWorld` was an
ordinary (1847, 1044), i.e. a real distance of 2081. That drove `horizonMix` and
`haze` to 1.0 while the un-hazed `body` value was discarded with them, painting a
24-row band of flat (17,20,32) across the full width, straddling the horizon: the
"black gap where the sea should stretch". The fix is one word in each stage —
`varying highp vec3 vWorld;` — and nothing else changes; the sea's ALU stays
`mediump`. Declared on both sides the two stages agree explicitly, which is what
`make -f android/Makefile host-shaders` checks. On the frame, 95,163 pixels change,
all of them on or below the sea's top row (row 415 of 768); the sky above is
untouched, and the band's own pixel at row 420 goes from (17,20,32) to
(164,133,103).

**Scene 4: background objects burning out to flat white.** `pole/object_frag.glsl`
divided the GGX specular by `4 * NoV` on top of a `G` that is already Schlick's
Smith *visibility* term, `V = 0.25 / ((NoL(1-k)+k)(NoV(1-k)+k))`, which already
contains the microfacet BRDF's `1/(4·NoL·NoV)`. The duplicate factor was invisible
near normal incidence but grew without bound as a surface turned edge-on, and
`NoV` is only clamped to `1e-3`. Everything sitting on the horizon is seen
edge-on, which is why the background houses and the ridge behind them burned out:
a probe render that prints each pixel's material, `rough` and `sp` shows the
blown-out facets at `rough = 1.0` — where `D` is a flat 0.318 and so does nothing
to suppress the term — and `sp ≥ 4`, and `sunTint * sp` alone is (4.0, 2.6, 1.4),
which is pure white once the scene's tonemap runs. Dropping the duplicate factor
(`spR = D * G * Fs * NoL`) leaves the highlight otherwise intact and bounds it at
grazing angles. Measured on the frame, pixels with every channel above 195 fall
from 12,106 to 969, and counting 8-connected runs of at least 16 such pixels the
count of blobs falls from 30 — the four largest covering 3126, 2323, 917 and 715
px — to exactly one, which is the vending machine's lit front panel (0.88, 0.93,
1.00 through the `kMatGlow` branch), a light source by design. The named object goes from (254,254,208) to (207,182,149)
and the peak luminance of its region from 253 to 186, while the whole frame gets
slightly *darker* (mean 127.9 → 123.8), so the correction removes brightness rather
than adding it; 20.2% of the pixels change. Objects with a genuinely smooth
material still glint — the same term, now scaled by the geometry instead of by
`1/NoV`.

Both artifacts are repacked from the fixed tree (`make -f android/Makefile` for the
APK, `make` for the VPK) and the two committed screenshots regenerated, so the
shipped APK, the VPK and the images all carry the fix.

**Both fixes re-checked from the shipped bytes, on both ports.** Android side:
`make -f android/Makefile host-shaders` links all six programs on the host's ES 3.2
driver, and fresh `host-render` frames reproduce the numbers above — scene 2 has no
row in 380-469 with a row mean below 35 (the pre-fix frame has 14, of which 24 rows
carry the flat (17,20,32) value at the mid column), and scene 4's over-195 pixels
are 969 with a single blob of at least 16 px. Vita side: the repacked VPK was booted
under Vita3K, and *every* program of both scenes went through SceShaccCg and was
accepted. All six files of scene 2's three programs (sea, sky, HUD) were loaded in one run and
all six of scene 4's (sky, object, HUD) in another. The app reported no `failed to
compile` and no `failed to link` in either; the only diagnostics were warnings, and
two of them name identifiers that exist in exactly one file — `reflDist` (only in
`shaders/ps14/sea_frag.glsl`, the edited stage) and `toSun` (only in
`shaders/pole/object_frag.glsl`) — which is what ties a warning to the shader it
came from. Both scenes then ran their benchmark and fired their scheduled shot
(`shot trigger: t=6.01 target=6.00` in scene 2, `shot trigger: sim=6.06
target=6.00` in scene 4).

**What is still not verified for these two fixes:** nothing has run on a real
console. Scene 2's *pixels* cannot be captured under Vita3K at all (the shader-bank
limit above drops its draws), so the sea's `highp` varying is confirmed to be
*accepted* by SceShaccCg, not confirmed to remove the band on hardware. `highp` in a
fragment shader stays optional in ES 1.00 (`GL_FRAGMENT_PRECISION_HIGH`); this
shader already relied on it unguarded for the earlier `uTime` fix, and a device
without it would reject the program rather than draw the band — the alternative is
to stop shipping raw world coordinates to the fragment stage at all.

### Cross-port scan: what matches, and what does not

The two ports share the shader tree (`shaders/`, packed into the APK's assets and
the VPK's `app0:`) and the two scene translation units (`src/scene2.cxx`,
`src/scene4.cxx`); only the platform layer differs (`android/jni` + ES 3.0 stand-ins
against `src/vita_main.cxx`/`src/vita_sdl.cxx` + vitaGL). So every shader change and
every scene change lands on both, and `tools/check_shaders.sh` is one script run by
both `make check` and `make -f android/Makefile check`. What the scan turned up that
is *not* fixed here:

* **Scene 2's environment cube map is a Vita-only gap.** `src/scene2.cxx` allocates
  it with `glTexImage2D(..., GL_RGBA16F, ..., GL_RGBA, GL_FLOAT, nullptr)`, asks for
  `GL_TEXTURE_WRAP_R` and calls `glGenerateMipmap(GL_TEXTURE_CUBE_MAP)`. All three
  are core ES 3.0, and all three are refused by vitaGL: its cube path
  (`source/textures.c`, `_glTexImage2D_CubeIMPL`) accepts only
  `GL_UNSIGNED_BYTE`/`GL_UNSIGNED_SHORT_5_5_5_1`/`GL_UNSIGNED_SHORT_4_4_4_4` for
  `format = GL_RGBA`, its `glTexParameteri` has no wrap-R case, and its
  `glGenerateMipmap` rejects the cube target. The guest logged exactly those three
  (`_glTexImage2D_CubeIMPL set GL_INVALID_ENUM (type: 0x1406)`,
  `_glTexParameteri set GL_INVALID_ENUM (pname: 0x8072)`, `glGenerateMipmap set
  GL_INVALID_ENUM (target: 0x8513)`) during the run, so the reflection probe the sea
  samples is very likely unallocated on hardware. The scene never calls
  `glCheckFramebufferStatus`, so nothing would report it, and scene 2's pixels
  cannot be checked under Vita3K to see it — this one needs a console.
* **`clip` shadows a Cg builtin** in `shaders/ps14/hud_vert.glsl`. SceShaccCg says
  `'clip' shadows previous declaration` / `previous declaration was builtin` and
  compiles the program anyway; it is informational, and renaming a variable in a
  HUD shader is not worth a second round of both ports' checks.
* **Android has no device-side run either**: the APK builds, packs the current
  shader bytes, and passes `apksigner verify` for all three ABIs, and the scenes and
  shaders are exercised through the host's ES 3 driver - but the activity/EGL glue
  in `android/jni/` is only as good as the compiler and the emulator that does not
  exist here.
