ALL:
	clang++ ./src/main.cxx -o ElectroBench -O3 -I/usr/include -L/usr/include -lGL -lGLU -lGLEW -lSDL2
