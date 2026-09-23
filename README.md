# ElectroBench
ElectroBench is a 45-second long benchmark specifiacally designed to run on old and modern PCs, don't critise it by it using OpenGL 2.1, and GLSL 1.2, Even office PCs have low scores at it.
It uses OpenGL 2.1/3.3, and C++, and uses make for compilation. It is designed to be a replacement for glmark (even though it is great and I used it before).

It ships **two** benchmarks :

| Benchmark | Renderer | Scene |
|---|---|---|
| **ElectroBench** (the OG) | OpenGL 2.1 / GLSL 1.2, fixed-function pipeline | **110 UZIs** on a shadow-mapped concrete floor, lit by a warm sun |
| **DuskTide** | OpenGL 3.3 core, pixel-shader workloads | A dusk ocean under volumetric clouds (3DMark2001 SE "Nature" recreation) |



# Screenshots

The 110 UZIs lying on the shadow-mapped concrete floor — every gun casts its own compact shadow anchored at its contact point (warm sun from the upper left):

![Original GL 2.1 benchmark: 110 UZIs on a shadow-mapped concrete floor](docs/screenshots/uzi_wide.png)

Close-up — mags resting on the ground, shadows clearly visible under each gun:

![Close-up: UZIs with mags on the ground and per-gun shadows](docs/screenshots/uzi_close.png)

The PS1.4 sea benchmark — long cloud banks with sunward silver linings, a narrow orange glitter path down the middle of the sea, dark blue-purple water either side, raised swell banks:

![DuskTide dusk ocean benchmark](docs/screenshots/ps14_dusk_t36.png)

# How to build ?

Dependencies : `make`, `g++`, SDL2, GLEW, GLU (+ dev headers). On Debian/Ubuntu that is
`libsdl2-dev libglew-dev libglu1-mesa-dev`; on Windows use MSYS2 (`pacman -S mingw-w64-x86_64-{gcc,SDL2,glew}`); on macOS `brew install sdl2 glew` (you may need `brew install make` for a GNU make).

```sh
make            # builds BOTH benchmarks
make legacy     # only the OG GL 2.1 benchmark
make dusktide   # only DuskTide
```

Binaries land in `build/` :

```sh
./build/ElectroBench          # OG 2.1 benchmark (Linux / macOS)
./build/ElectroBench.exe      # Windows (MSYS2)
./build/DuskTide             # dusk ocean benchmark (Linux / macOS)
./build/DuskTide.exe         # Windows (MSYS2)
```

## Static build

Pass `STATIC=1` to link everything statically (`-static -static-libgcc -static-libstdc++` + static
dependency archives) — handy for dropping a single exe on old machines:

```sh
make STATIC=1            # -> build/ElectroBench-static(.exe), build/DuskTide-static(.exe)
make STATIC=1 legacy     # just the OG, statically linked
make STATIC=1 dusktide   # just DuskTide, statically linked
```

This requires the **static archives** of every dependency (e.g. MSYS2's `mingw-w64-x86_64-SDL2` ships
`libSDL2.a` already; on Linux you need the `.a` variants of SDL2/GLEW/GLU installed). On Windows the
static OG build links `-static-libgcc -static-libstdc++ -lopengl32 -lglu32 -lglew32 -lSDL2main -lSDL2 -mwindows` — no DLLs needed next to the exe.

# The OG GL 2.1 benchmark (110 UZIs)

Renders **110 UZIs** (10×11 grid) on a shadow-mapped concrete floor through the fixed-function
pipeline, exactly like a 2001-era title: per-gun compact shadows anchored at each contact point,
a warm directional sun, and a real-time **FPS + score HUD** drawn in a 5×7 bitmap font.

Controls : long-click + move orbits the camera, mouse wheel zooms (smooth, clamped so you never clip
into the scene), `ESC` quits. The FPS counter is a true frame-count average (SDL performance counter,
every frame accounted) — the on-screen value is a smoothed window, the final score uses **all** frames
of the run.

# DuskTide — the dusk ocean benchmark

DuskTide is written in **OpenGL 3.3 core** and pushes a heavy, realistic dusk-ocean workload —
high-resolution environment reflections, a dense displaced ocean mesh, and a real volumetric
light-transport model for the clouds. It is heavy **on purpose**: the goal is to push old and new
hardware alike, so low single-digit FPS on a low-end machine means the workload is doing its job.

What it renders :
- A procedural **sky dome** rendered **directly at full screen resolution**: dusk gradient with a
  bright horizon band, a compact orange sun, and a handful of **volumetric cloud banks** — each
  visible cloud point ray-marches density toward the sun and shades with real light transport
  (optical-depth self-shadowing, Henyey-Greenstein forward-scatter silver linings, dark anti-sun
  bulk, skylight tops, powder-dense cores, aerial perspective). Coverage is exact by construction:
  a few long banks with real gaps, no noise-texture mottle
- A **4 km ocean patch** on a dense GPU-displaced grid: long rolling swells with crest-skewed banks,
  per-pixel analytic wave normals plus near-camera detail wavelets
- High-resolution **environment cubemap** reflections with roughness-matched LOD (the sun smears
  into a glow, never texel squares), fresnel blending, sun-tinted **glitter** path gated to the
  sun's azimuth (bright path down the middle, dark blue-purple water either side),
  slope-gated crest foam, subsurface glow in thin crests, and distance haze that converges into the
  actual per-azimuth sky colour so the far sea melts into the horizon
- A clean in-engine **FPS readout** (the score belongs to the final results line) and the same score
  formula as the main benchmark, over a 45 second run

Build and run it with :

```sh
make
./build/DuskTide          # Linux / macOS
./build/DuskTide.exe      # Windows (MSYS2)
```

Controls : `F` toggles the automatic fly-over camera, long-click + move orbits the camera, mouse wheel zooms, arrow keys look around, `ESC` quits.

Headless visual-test flags (used to verify the render output in CI-like environments):

```sh
./build/DuskTide --width 960 --screenshot /tmp/shot.ppm --shot-times 6,20,38
./build/ElectroBench --screenshot /tmp/shot.ppm --shot-time 3
```

# How the score is calculated ?

Both benchmarks use the same formula, computed from the **average FPS over the whole run** (all
frames, not the last second):

```
score = fps² × 2
```

It is linear in nothing: twice the frames means twice the score, twice the load means a quarter of
it — a fair curve from office PCs to gaming rigs. Both binaries print
`Time / Average FPS / Score` at the end, and the OG also shows live FPS + score in its HUD.

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

**If `cmake` dies with `Illegal instruction`** — modern MSYS2 `mingw64`/`ucrt64` packages (including `cmake.exe` itself) are built for the x86-64-v2 microarchitecture (SSE4.2 + POPCNT), so they crash on older CPUs that lack those instructions (e.g. Core 2 Duo era laptops). If that happens, skip CMake entirely and build directly with g++, which the compiler will happily target at the baseline ISA:

```sh
g++ -std=c++17 -O2 -march=x86-64 -mtune=generic \
    src/main.cxx \
    -o build/ElectroBench.exe \
    $(pkg-config --cflags --libs sdl2) \
    -lglew32 \
    -lglu32 \
    -lopengl32

# PS1.4 sea benchmark (GL 3.3, no GLU needed)
g++ -std=c++17 -O2 -march=x86-64 -mtune=generic \
    src/ps14_bench.cxx \
    -o build/ElectroBenchPS14.exe \
    $(pkg-config --cflags --libs sdl2) \
    -lglew32 \
    -lopengl32
```

Notes for the direct g++ build :
- `-march=x86-64 -mtune=generic` is the key: it emits baseline x86-64 code that runs on any 64-bit CPU, so the resulting exe won't illegal-instruction even where the prebuilt MSYS2 tools do.
- Keep `-lglew32` before `-lSDL2`, and `-lopengl32` last — link order matters on MinGW.
- Run the exe from the repo root (or copy `SDL2.dll` / `glew32.dll` from `C:\msys64\<env>\bin` next to it) so the DLLs resolve.
- This was verified end-to-end on a Toshiba Satellite P200 (Core 2 Duo, pre-x86-64-v2) — all three shader programs compiled and linked on hardware.

Notes :
- The headless screenshot flags work too — just use a Windows-style path: `./build/ElectroBenchPS14.exe --width 960 --screenshot shot.ppm --shot-times 6,20,38`
- Any GPU with drivers from ~2010 onward handles both targets (PS14 needs GL 3.3; the main bench's GL 2.1 request gets a compatibility context — drivers ignore the profile hint below 3.2, per spec).
- Run the exes from the repo root or via `build\...` — the asset resolver checks the current directory and then the executable's parent, so `shaders/` and `assets/UZI.obj` are found either way.

# Contributions

Contributions are welcome, just post a PR / issue.

**PS** : Sorry guys there are some heavy files I used to fix my tablet, until they're hosted in another repo they'll stay stuck here. :( (nvm when you clone only a symlink exists not the 3.6gb files cus it's LFS files)

# Donating

Send me in my email or post an issue : ayoubprogramming96@outlook.com
