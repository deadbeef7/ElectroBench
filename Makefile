ALL:
	clang++ ./src/main.cxx -o ElectroBench -O3 -I/usr/include -L/usr/include -lGL -lGLU -lGLEW -lSDL2

ps14:
	clang++ ./src/ps14_bench.cxx -o ElectroBenchPS14 -O3 -std=c++17 -I/usr/include -L/usr/include -lGL -lGLEW -lSDL2

ps14-gcc:
	g++ ./src/ps14_bench.cxx -o ElectroBenchPS14 -O3 -std=c++17 -lGL -lGLEW -lSDL2

clean:
	rm -f ElectroBenchPS14
