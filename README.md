# ElectroBench
ElectroBench is a 45-second long benchmark specifiacally designed to run on old and modern PCs, don't critise it by it using OpenGL 2.1, and GLSL 1.2, Even office PCs have low scores at it.
It uses OpenGL 2.1, and C++, and uses make for compilation. It is designed to be a replacement for glmark (even though it is great and I used it before).

# Effects

In this benchmark, we are using realistic lighting techniques, thanks to the shaders (with some limitations, of course), then we load 90 UZIs (!!) with 6 textures each onto the screen.

You can move the camera by long-clicking and moving the mouse.

# How the score is calculated ?
The score is calculated using this formula : ```fps*2/(1.01/fps)```

# How to run ?

Make sure you have `clang and glew, libglvnd-dev and sdl2` installed and then run the following
command on your machine after cloning repo:


```sh
make
./ElectroBench
```

# Contributions

Contributions are welcome, just post a PR / issue.

# Donating

Send me in my email or post an issue : ayoubprogramming96@outlook.com
