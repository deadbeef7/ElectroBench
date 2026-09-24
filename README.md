# ElectroBench
ElectroBench is a 45-second long benchmark specifiacally designed to run on old and modern PCs, don't critise it by it using OpenGL 2.1, and GLSL 1.2, Even office PCs have low scores at it.
It uses OpenGL 2.1/3.3, and C++, and uses make for compilation. It is designed to be a replacement for glmark (even though it is great and I used it before).

It ships **two** benchmarks :

| Benchmark | Renderer | Scene |
|---|---|---|
| **ElectroBench** (the OG) | OpenGL 2.1 / GLSL 1.2, fixed-function pipeline | **110 UZIs** on a shadow-mapped concrete floor, lit by a warm sun |
| **TideBench** | OpenGL 3.3 core, pixel-shader workloads | A dusk ocean under volumetric clouds (3DMark2001 SE "Nature" recreation) |



# Screenshots

The 110 UZIs lying on the shadow-mapped concrete floor — every gun casts its own compact shadow anchored at its contact point (warm sun from the upper left):

![Original GL 2.1 benchmark: 110 UZIs on a shadow-mapped concrete floor](docs/screenshots/uzi_wide.png)

Close-up — mags resting on the ground, shadows clearly visible under each gun:

![Close-up: UZIs with mags on the ground and per-gun shadows](docs/screenshots/uzi_close.png)

The PS1.4 sea benchmark — long cloud banks with sunward silver linings, a narrow orange glitter path down the middle of the sea, dark blue-purple water either side, raised swell banks:

![TideBench dusk ocean benchmark](docs/screenshots/ps14_dusk_t36.png)

# How to build ?

Dependencies : `make`, `g++`, SDL2, GLEW, GLU (+ dev headers). On Debian/Ubuntu that is
`libsdl2-dev libglew-dev libglu1-mesa-dev`; on Windows use MSYS2 (`pacman -S mingw-w64-x86_64-{gcc,SDL2,glew}`); on macOS `brew install sdl2 glew` (you may need `brew install make` for a GNU make).

```sh
make            # builds BOTH benchmarks
make legacy     # only the OG GL 2.1 benchmark
make tidebench   # only TideBench
```

Binaries land in `build/` :

```sh
./build/ElectroBench          # OG 2.1 benchmark (Linux / macOS)
./build/ElectroBench.exe      # Windows (MSYS2)
./build/TideBench             # dusk ocean benchmark (Linux / macOS)
./build/TideBench.exe         # Windows (MSYS2)
```

## Static build

Pass `STATIC=1` to link everything statically (`-static -static-libgcc -static-libstdc++` + static
dependency archives) — handy for dropping a single exe on old machines:

```sh
make STATIC=1            # -> build/ElectroBench-static(.exe), build/TideBench-static(.exe)
make STATIC=1 legacy     # just the OG, statically linked
make STATIC=1 tidebench   # just TideBench, statically linked
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

# TideBench — the dusk ocean benchmark

TideBench is written in **OpenGL 3.3 core** and pushes a heavy, realistic dusk-ocean workload —
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
  slope-gated crest foam, sun-path-gated subsurface glow in thin crests, and distance haze that converges into the
  actual per-azimuth sky colour so the far sea melts into the horizon
- **Cloud shadows on the water**: each sea fragment is projected along its sun ray into the same
  analytic cloud layout the sky renders — where a cloud blocks the sun the direct light dies, foam
  stops breaking and the sea goes much darker, in coherent patches that sit exactly under the
  clouds that cast them. Off-sun water sinks to near-black indigo
- A clean in-engine **FPS readout** (the score belongs to the final results line) and the same score
  formula as the main benchmark, over a 45 second run

Build and run it with :

```sh
make
./build/TideBench          # Linux / macOS
./build/TideBench.exe      # Windows (MSYS2)
```

Controls : `F` toggles the automatic fly-over camera, long-click + move orbits the camera, mouse wheel zooms, arrow keys look around, `ESC` quits.

Headless visual-test flags (used to verify the render output in CI-like environments):

```sh
./build/TideBench --width 960 --screenshot /tmp/shot.ppm --shot-times 6,20,38
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

# The results screen

When the 45/60 second run ends, **the benchmark clears the window and prints the final score
on screen** — a big centred score with the time and average FPS underneath — and leaves it up
for about ten seconds (or until you press `ESC`). The same line is also printed to stdout.

```sh
Benchmark Results - Time : 45.0s, Average FPS : 12.4, Score : 308
```

# One binary, two scenes

`build/ElectroBench` now contains **both scenes**. It runs the OG 60-second gun benchmark
first, then probes an OpenGL 3.3 core context:

- **found** — TideBench (the dusk ocean) runs as scene 2 on the same session, and the final
  results screen shows **per-scene scores and the average**:
  `Fused Results - ElectroBench : 308 | TideBench : 42 | Average : 175`
- **not found** (GL 2.1-only drivers, old iGPUs) — TideBench skips itself cleanly and the OG
  result stands, so the binary still runs on the ancient hardware it targets

Pass `--og-only` to run just the gun scene even on GL 3.3-capable devices.
`./build/TideBench` remains available to run the ocean scene on its own.

# Windows (MSYS2)

On Windows the easiest route is [MSYS2](https://www.msys2.org/), which provides gcc, cmake and prebuilt SDL2/GLEW/GLU packages. Both benchmarks build and run unmodified.

**1. Install MSYS2** from [msys2.org](https://www.msys2.org/) to the default `C:\msys64`.

**2. Run:**

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
    -o build/TideBench.exe \
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
- The headless screenshot flags work too — just use a Windows-style path: `./build/TideBench.exe --width 960 --screenshot shot.ppm --shot-times 6,20,38`
- Any GPU with drivers from ~2010 onward handles both targets (PS14 needs GL 3.3; the main bench's GL 2.1 request gets a compatibility context — drivers ignore the profile hint below 3.2, per spec).
- Run the exes from the repo root or via `build\...` — the asset resolver checks the current directory and then the executable's parent, so `shaders/` and `assets/UZI.obj` are found either way.

# Contributions

Contributions are welcome, just post a PR / issue.

**PS** : Sorry guys there are some heavy files I used to fix my tablet, until they're hosted in another repo they'll stay stuck here. :( (nvm when you clone only a symlink exists not the 3.6gb files cus it's LFS files)

# Donating

Send me in my email or post an issue : ayoubprogramming96@outlook.com
