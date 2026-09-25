pacman -S mingw-w64-i686-toolchain mingw-w64-i686-SDL2
wget https://repo.msys2.org/mingw/mingw32/mingw-w64-i686-glew-2.2.0-3-any.pkg.tar.zst
pacman -U mingw-w64-i686-glew-2.2.0-3-any.pkg.tar.zst
mkdir -p build/static
g++ -Ilib -IC:/msys64/mingw32/include/SDL2 -Dmain=SDL_main -DGLEW_STATIC -DGLEW_STATIC -std=c++17 -O2 -m32 -march=bonnell -mtune=bonnell -c src/main.cxx -o build/static/main.o
g++ -Ilib -IC:/msys64/mingw32/include/SDL2 -Dmain=SDL_main -DGLEW_STATIC -DGLEW_STATIC -std=c++17 -O2 -m32 -march=bonnell -mtune=bonnell -c src/tidebench.cxx -o build/static/tidebench.o
g++ -Ilib -IC:/msys64/mingw32/include/SDL2 -Dmain=SDL_main -DGLEW_STATIC -DGLEW_STATIC -std=c++17 -O2 -m32 -march=bonnell -mtune=bonnell -c src/pool.cxx -o build/static/pool.o
g++ -std=c++17 -O2 -m32 -march=bonnell -mtune=bonnell -static -static-libgcc -static-libstdc++ -lmingw32 build/static/main.o build/static/tidebench.o  build/pool.o -o build/ElectroBench-static.exe -lmingw32 -mwindows -lSDL2main -lSDL2 -lm -lkernel32 -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lversion -luuid -ladvapi32 -lsetupapi -lshell32 -ldinput8 -lglew32 -lglu32 -lopengl32 -lSDL2main -lSDL2 -mwindows
