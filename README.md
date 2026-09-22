
# ElectroBench
ElectroBench is a 45-second long benchmark specifiacally designed to run on old and modern PCs, don't critise it by it using OpenGL 2.1, and GLSL 1.2, Even office PCs have low scores at it.
It uses OpenGL 2.1/3.3, and C++, and uses make for compilation. It is designed to be a replacement for glmark (even though it is great and I used it before).



# Screenshots

The 90 UZIs lying on the shadow-mapped concrete floor — every gun now casts its own compact shadow anchored at its contact point (warm sun from the upper left):

![Original GL 2.1 benchmark: 90 UZIs on a shadow-mapped concrete floor](docs/screenshots/uzi_wide.png)

Close-up — mags resting on the ground, shadows clearly visible under each gun:

![Close-up: UZIs with mags on the ground and per-gun shadows](docs/screenshots/uzi_close.png)

# PS1.4 Sea benchmark (3DMark2001 SE "Nature" recreation)

The `ps14-sea-benchmark` branch adds a second, heavier benchmark written in **OpenGL 3.3 core** that recreates the pixel shader 1.4 workload from 3DMark2001 SE: an ocean under a cloudy sky with real-time reflections.

What it renders :
- A procedural **sky dome** with big smooth dusk cloud banks, rendered **directly at full screen resolution** — plus a cubemap capture of the same sky used for the sea's reflections (low-res there is invisible and cheap)
- A **4 km ocean patch** (far plane 6000) displaced on the GPU by a 6-octave wave function, haze-matched to the per-azimuth horizon colour so it melts into the sky
- Water shading structured like an asm `ps_1_4` shader: ripple-gradient **addressing** phase, a **dependent read** into the environment cubemap for reflections, then fresnel blending, sun **glitter**, foam crests and distance haze
- The same score formula as the main benchmark, over a 45 second run

Build and run it with :

```sh
make
./build/PS14SeaBenchmark          # Linux / macOS
./build/PS14SeaBenchmark.exe      # Windows (MSYS2)
```

Controls : `F` toggles the automatic fly-over camera, long-click + move orbits the camera, mouse wheel zooms, arrow keys look around, `ESC` quits.

Headless visual-test flags (used to verify the render output in CI-like environments):

```sh
./build/PS14SeaBenchmark --width 960 --screenshot /tmp/shot.ppm --shot-times 6,20,38
```

The sunset scene at 35s into the run (big cloud banks, warm sun with glitter reflection, dark sea haze-matched into the horizon):

![PS1.4 sea benchmark](docs/screenshots/ps14_dusk_t36.png)

# How the score is calculated ?
The score is calculated using this formula : ```fps*2/(1.01/fps)```

# How to run ?

Make sure you have `cmake, glew, libglvnd-dev, sdl2 and glu` installed and then run the following
command on your machine after cloning repo:


```sh
make legacy
```

Controls (original GL 2.1 benchmark) : long-click + move orbits the camera, mouse wheel zooms (smooth, clamped so you never clip into the scene), `ESC` quits. The 90 UZIs stand on a shadow-mapped concrete floor lit by a warm sun.

# Contributions

Contributions are welcome, just post a PR / issue.

**PS** : Sorry guys there are some heavy files I used to fix my tablet, until they're hosted in another repo they'll stay stuck here. :( (nvm when you clone only a symlink exists not the 3.6gb files cus it's LFS files)

# Donating

Send me in my email or post an issue : ayoubprogramming96@outlook.com
