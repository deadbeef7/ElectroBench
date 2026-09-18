
# ElectroBench
ElectroBench is a 45-second long benchmark specifiacally designed to run on old and modern PCs, don't critise it by it using OpenGL 2.1, and GLSL 1.2, Even office PCs have low scores at it.
It uses OpenGL 2.1, and C++, and uses make for compilation. It is designed to be a replacement for glmark (even though it is great and I used it before).

# Effects

In this benchmark, we are using realistic lighting techniques, thanks to the shaders (with some limitations, of course), then we load 90 UZIs (!!) with 6 textures each onto the screen.

You can move the camera by long-clicking and moving the mouse.

# Screenshots

<img width="1366" height="768" alt="screenshot-20260815-151152" src="https://github.com/user-attachments/assets/81cc2bcf-45fd-4bb9-b2fd-ecf357a5e0ff" />

# PS1.4 Sea benchmark (3DMark2001 SE "Nature" recreation)

The `ps14-sea-benchmark` branch adds a second, heavier benchmark written in **OpenGL 3.3 core** that recreates the pixel shader 1.4 workload from 3DMark2001 SE: an ocean under a cloudy sky with real-time reflections.

What it renders :
- A procedural **sky dome** with two layers of drifting fBm clouds, captured every frame into a **cubemap** (the PS1.4-era trick for dynamic reflections)
- A **256x256 ocean grid** displaced on the GPU by a 6-octave wave function
- Water shading structured like an asm `ps_1_4` shader: ripple-gradient **addressing** phase, a **dependent read** into the environment cubemap for reflections, then fresnel blending, sun **glitter**, foam crests and distance haze
- The same score formula as the main benchmark, over a 45 second run

Build and run it with :

```sh
make ps14
./ElectroBenchPS14
```

Controls : `F` toggles the automatic fly-over camera, long-click + move orbits the camera, mouse wheel zooms, arrow keys look around, `ESC` quits.

# How the score is calculated ?
The score is calculated using this formula : ```fps*2/(1.01/fps)```

# How to run ?

Make sure you have `clang and glew, libglvnd-dev and sdl2 and glu` installed and then run the following
command on your machine after cloning repo:


```sh
make
./ElectroBench
```

# Contributions

Contributions are welcome, just post a PR / issue.

**PS** : Sorry guys there are some heavy files I used to fix my tablet, until they're hosted in another repo they'll stay stuck here. :( (nvm when you clone only a symlink exists not the 3.6gb files cus it's LFS files)

# Donating

Send me in my email or post an issue : ayoubprogramming96@outlook.com
