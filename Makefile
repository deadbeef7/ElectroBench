# ============================================================
# ElectroBench Makefile
# Windows (MSYS2), macOS (Homebrew), Linux
#
# ONE executable: build/ElectroBench contains ALL THREE scenes (the OG GL 2.1
# gun scene, the GL 3.3 dusk-ocean scene, and the GL 3.3 pool-room scene).
# There is no second binary and no child process: src/tidebench.cxx and
# src/pool.cxx are linked straight into this program as scene modules and
# handed the same SDL session by src/main.cxx.
#
# This file defines exactly ONE binary target ($(BIN)). There is no tidebench,
# poolbench, legacy or per-scene target, and no per-scene second compile: the
# three .cxx files are just the three object files of the one executable.
# ============================================================

CXX ?= g++

CXXFLAGS := -std=c++17 -O2 -march=x86-64 -mtune=generic
CPPFLAGS := -Ilib

# STATIC=1 -> fully static link (example, Windows/MSYS2):
# Requires the static archives of every dependency (SDL2, GLEW, GL, libc++).
STATIC ?= 0
STATIC_SUFFIX :=
STATIC_LDFLAGS :=

BUILD := build
ifeq ($(STATIC),1)
    # GLEW's static and DLL headers use different symbol decorations. Keep
    # static objects separate so a prior dynamic build cannot leave import
    # references in the static executable.
    OBJDIR := $(BUILD)/static
else
    OBJDIR := $(BUILD)
endif

OBJS := $(OBJDIR)/main.o $(OBJDIR)/tidebench.o $(OBJDIR)/pool.o

BIN = $(BUILD)/ElectroBench$(STATIC_SUFFIX)

# Obsolete second-executable artifacts. Older revisions built the ocean scene as
# its own binary (build/TideBench, ElectroBenchPS14, the CMake tree in
# build-debug/). Nothing builds them any more, so `make` deletes any that an
# older build left behind.
OBSOLETE := $(BUILD)/TideBench $(BUILD)/TideBench.exe \
            $(BUILD)/TideBench-static $(BUILD)/TideBench-static.exe \
            $(BUILD)/ElectroBenchPS14 $(BUILD)/ElectroBenchPS14.exe \
            $(BUILD)/PoolBench $(BUILD)/PoolBench.exe \
            ElectroBenchPS14 ElectroBenchPS14.exe PoolBench PoolBench.exe

# ------------------------------------------------------------
# Platform detection
# ------------------------------------------------------------

UNAME_S := $(shell uname -s)

ifeq ($(findstring MINGW,$(UNAME_S)),MINGW)
    PLATFORM := windows
else ifeq ($(findstring MSYS,$(UNAME_S)),MSYS)
    PLATFORM := windows
else ifeq ($(findstring CYGWIN,$(UNAME_S)),CYGWIN)
    PLATFORM := windows
else ifeq ($(UNAME_S),Darwin)
    PLATFORM := macos
else ifeq ($(UNAME_S),Linux)
    PLATFORM := linux
else
    PLATFORM := unknown
endif

# ------------------------------------------------------------
# Libraries
# ------------------------------------------------------------

ifeq ($(PLATFORM),windows)

    ifeq ($(STATIC),1)
        STATIC_SUFFIX := -static
        STATIC_LDFLAGS := -static -static-libgcc -static-libstdc++
        # Without this define MinGW's GLEW header emits __imp___glew* DLL
        # imports, which cannot be resolved by libglew32.a.
        GLEW_CFLAGS := -DGLEW_STATIC
        PKG_CFLAGS := $(shell pkg-config --static --cflags sdl2 glew)
        PKG_LIBS   := $(shell pkg-config --static --libs sdl2 glew)
        LDLIBS := $(PKG_LIBS) \
                  -lglu32 \
                  -lopengl32 \
                  -lSDL2main \
                  -lSDL2 \
                  -mwindows
    else
        GLEW_CFLAGS :=
        PKG_CFLAGS := $(shell pkg-config --cflags sdl2 glew)
        PKG_LIBS   := $(shell pkg-config --libs sdl2 glew)
        LDLIBS := $(PKG_LIBS) \
                  -lglu32 \
                  -lopengl32
    endif

    BIN := $(BUILD)/ElectroBench$(STATIC_SUFFIX).exe

else ifeq ($(PLATFORM),macos)

    ifeq ($(STATIC),1)
        STATIC_SUFFIX := -static
        STATIC_LDFLAGS := -static-libgcc -static-libstdc++
        PKG_CFLAGS := $(shell pkg-config --static --cflags sdl2 glew)
        PKG_LIBS   := $(shell pkg-config --static --libs sdl2 glew)
    else
        PKG_CFLAGS := $(shell pkg-config --cflags sdl2 glew)
        PKG_LIBS   := $(shell pkg-config --libs sdl2 glew)
    endif

    LDLIBS := $(PKG_LIBS) \
              -framework OpenGL

else ifeq ($(PLATFORM),linux)

    ifeq ($(STATIC),1)
        STATIC_SUFFIX := -static
        STATIC_LDFLAGS := -static -static-libgcc -static-libstdc++
        PKG_CFLAGS := $(shell pkg-config --static --cflags sdl2 glew)
        PKG_LIBS   := $(shell pkg-config --static --libs sdl2 glew)
    else
        PKG_CFLAGS := $(shell pkg-config --cflags sdl2 glew)
        PKG_LIBS   := $(shell pkg-config --libs sdl2 glew)
    endif

    LDLIBS := $(PKG_LIBS) \
              -lGLU \
              -lGL

else

    $(error Unsupported platform: $(UNAME_S))

endif

# ------------------------------------------------------------
# Default target
# ------------------------------------------------------------

.PHONY: all
all: clean-obsolete $(BIN)

.PHONY: electrobench
electrobench: $(BIN)

# Removes only the obsolete second-executable artifacts listed above.
.PHONY: clean-obsolete
clean-obsolete:
	@rm -f $(OBSOLETE)

# ------------------------------------------------------------
# Build directory
# ------------------------------------------------------------

$(OBJDIR):
	mkdir -p $@

# ------------------------------------------------------------
# ElectroBench (the single binary: OG gun scene + ocean scene + pool scene)
# ------------------------------------------------------------

$(OBJDIR)/main.o: src/main.cxx src/font_atlas.hxx | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(PKG_CFLAGS) $(GLEW_CFLAGS) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/tidebench.o: src/tidebench.cxx src/font_atlas.hxx | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(PKG_CFLAGS) $(GLEW_CFLAGS) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/pool.o: src/pool.cxx src/font_atlas.hxx | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(PKG_CFLAGS) $(GLEW_CFLAGS) $(CXXFLAGS) -c $< -o $@

$(BIN): $(OBJS) | $(OBJDIR)
	@echo "========================================"
	@echo " Building ElectroBench (all three scenes in one binary)"
	@echo " Platform: $(PLATFORM)"
	@echo "========================================"
ifeq ($(STATIC),1)
	$(CXX) $(CXXFLAGS) $(STATIC_LDFLAGS) $(OBJS) -o $@ $(LDLIBS)
else
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@ $(LDLIBS)
endif

# ------------------------------------------------------------
# Clean
# ------------------------------------------------------------

.PHONY: clean
clean: clean-obsolete
	rm -rf $(BUILD)

# ------------------------------------------------------------
# Run
# ------------------------------------------------------------

.PHONY: run
run: $(BIN)
	./$(BIN)

# ------------------------------------------------------------
# Info
# ------------------------------------------------------------

.PHONY: info
info:
	@echo "ElectroBench build configuration"
	@echo "---------------------------------"
	@echo "Platform : $(PLATFORM)"
	@echo "Compiler : $(CXX)"
	@echo "CXXFLAGS : $(CXXFLAGS)"
	@echo "Static   : $(STATIC)"
	@echo "Binary   : $(BIN)"
	@echo "Libraries: $(LDLIBS)"
