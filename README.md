
# ElectroBench
ElectroBench is a 45-second long benchmark specifiacally designed to run on old and modern PCs, don't critise it by it using OpenGL 2.1, and GLSL 1.2, Even office PCs have low scores at it.
It uses OpenGL 2.1, and C++, and uses make for compilation. It is designed to be a replacement for glmark (even though it is great and I used it before).

# Effects

In this benchmark, we are using realistic lighting techniques, thanks to the shaders (with some limitations, of course), then we load 90 UZIs (!!) with 6 textures each onto the screen.

You can move the camera by long-clicking and moving the mouse.

# Screenshots

The 90 UZIs lying on the shadow-mapped concrete floor — every gun now casts its own compact shadow anchored at its contact point (warm sun from the upper left):

![Original GL 2.1 benchmark: 90 UZIs on a shadow-mapped concrete floor](docs/screenshots/uzi_wide.png)

Close-up — mags resting on the ground, shadows clearly visible under each gun:

![Close-up: UZIs with mags on the ground and per-gun shadows](docs/screenshots/uzi_close.png)

# PS1.4 Sea benchmark (3DMark2001 SE "Nature" recreation)

The `ps14-sea-benchmark` branch adds a second, heavier benchmark written in **OpenGL 3.3 core** that recreates the pixel shader 1.4 workload from 3DMark2001 SE: an ocean under a cloudy sky with real-time reflections.

What it renders :
- A procedural **sky dome** with two layers of drifting fBm clouds, captured every frame into a **cubemap** (the PS1.4-era trick for dynamic reflections)
- A **256x256 ocean grid** displaced on the GPU by a 6-octave wave function
- Water shading structured like an asm `ps_1_4` shader: ripple-gradient **addressing** phase, a **dependent read** into the environment cubemap for reflections, then fresnel blending, sun **glitter**, foam crests and distance haze
- The same score formula as the main benchmark, over a 45 second run

Build and run it with :

```sh
cmake -S . -B build
cmake --build build
./build/ElectroBenchPS14
```

Controls : `F` toggles the automatic fly-over camera, long-click + move orbits the camera, mouse wheel zooms, arrow keys look around, `ESC` quits.

Headless visual-test flags (used to verify the render output in CI-like environments):

```sh
./build/ElectroBenchPS14 --width 960 --screenshot /tmp/shot.ppm --shot-times 6,20,38
```

The sunset scene at 35s into the run (golden horizon, dark blue sky away from the sun, yellow sun with glitter reflection, choppy seas):

![PS1.4 sea benchmark](docs/screenshots/ps14_dusk_t36.png)

# How the score is calculated ?
The score is calculated using this formula : ```fps*2/(1.01/fps)```

# How to run ?

Make sure you have `cmake, glew, libglvnd-dev, sdl2 and glu` installed and then run the following
command on your machine after cloning repo:


```sh
cmake -S . -B build
cmake --build build
./build/ElectroBench
```

Controls (original GL 2.1 benchmark) : long-click + move orbits the camera, mouse wheel zooms (smooth, clamped so you never clip into the scene), `ESC` quits. The 90 UZIs stand on a shadow-mapped concrete floor lit by a warm sun.

# Windows (MSYS2)

On Windows the easiest route is [MSYS2](https://www.msys2.org/), which provides gcc, cmake and prebuilt SDL2/GLEW/GLU packages. Both benchmarks build and run unmodified.

**1. Install MSYS2** from [msys2.org](https://www.msys2.org/) to the default `C:\msys64`.

**2. Open the UCRT64 shell** — from the Start menu pick **"MSYS2 UCRT64"** (pink/flamingo icon). Not "MSYS2 MSYS" (black icon): that's the wrong environment and the packages below won't be found.

**3. Update pacman (first launch only)** — run twice if it asks to close the window:

```sh
pacman -Syu
```

**4. Install toolchain + libraries** (one command; if you prefer the MINGW64 shell instead of UCRT64, replace `ucrt-x86_64` with `x86_64` in every name — but stick to one and stay in the matching shell):

```sh
pacman -S --needed git mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-glew mingw-w64-ucrt-x86_64-glu
```

**5. Clone and build** (Ninja is much faster than the default MinGW generator):

```sh
git clone https://github.com/deadbeef7/ElectroBench.git
cd ElectroBench
cmake -S . -B build -G Ninja
cmake --build build
```

**6. Run** — from the same UCRT64 shell:

```sh
./build/ElectroBench.exe        # original GL 2.1 / GLSL 1.2 — 90 UZIs + shadows
./build/ElectroBenchPS14.exe    # GL 3.3 — the PS1.4 dusk sea benchmark
```

Running from the shell matters: the SDL2/GLEW/GLU DLLs live in `C:\msys64\ucrt64\bin`, which is only on `PATH` inside that shell. To launch from Explorer instead, copy `SDL2.dll`, `glew32.dll`, `glu32.dll` (and `zlib1.dll` if it complains) next to the exe.

Notes :
- The headless screenshot flags work too — just use a Windows-style path: `./build/ElectroBenchPS14.exe --width 960 --screenshot shot.ppm --shot-times 6,20,38`
- Any GPU with drivers from ~2010 onward handles both targets (PS14 needs GL 3.3; the main bench's GL 2.1 request gets a compatibility context — drivers ignore the profile hint below 3.2, per spec).
- Run the exes from the repo root or via `build\...` — the asset resolver checks the current directory and then the executable's parent, so `shaders/` and `assets/UZI.obj` are found either way.

# Contributions

Contributions are welcome, just post a PR / issue.

**PS** : Sorry guys there are some heavy files I used to fix my tablet, until they're hosted in another repo they'll stay stuck here. :( (nvm when you clone only a symlink exists not the 3.6gb files cus it's LFS files)

# Donating

Send me in my email or post an issue : ayoubprogramming96@outlook.com
