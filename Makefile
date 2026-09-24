# ============================================================
# ElectroBench Makefile
# Windows (MSYS2), macOS (Homebrew), Linux
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

LEGACY_SRC := src/main.cxx
TIDEBENCH_SRC := src/ps14_bench.cxx

# Fused ElectroBench: the OG binary contains BOTH scenes. src/ps14_bench.cxx is
# compiled a second time with -DFUSED_INTO_OG (entry point renamed, no main()),
# and after the 60 s gun scene the binary probes a GL 3.3 core context: found,
# it runs TideBench and shows per-scene + average scores; not found, TideBench
# skips itself and the OG result stands. build/TideBench remains standalone.
FUSED_CXXFLAGS := -DFUSED_INTO_OG
LEGACY_OBJS := $(BUILD)/main.o $(BUILD)/ps14_bench_fused.o

LEGACY_BIN = $(BUILD)/ElectroBench$(STATIC_SUFFIX)
TIDEBENCH_BIN   = $(BUILD)/TideBench$(STATIC_SUFFIX)

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
        PKG_CFLAGS := $(shell pkg-config --static --cflags sdl2 glew)
        PKG_LIBS   := $(shell pkg-config --static --cflags --libs sdl2 glew)
        LDLIBS := $(PKG_LIBS) \
                  -lglew32 \
                  -lglu32 \
                  -lopengl32 \
                  -lSDL2main \
                  -lSDL2 \
                  -mwindows
    else
        PKG_CFLAGS := $(shell pkg-config --cflags sdl2 glew)
        PKG_LIBS   := $(shell pkg-config --libs sdl2)
        LDLIBS := $(PKG_LIBS) \
                  -lglew32 \
                  -lglu32 \
                  -lopengl32
    endif

ifeq ($(PLATFORM),windows)
    LEGACY_BIN := $(BUILD)/ElectroBench$(STATIC_SUFFIX).exe
    TIDEBENCH_BIN   := $(BUILD)/TideBench$(STATIC_SUFFIX).exe
endif

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
all: legacy tidebench

# ------------------------------------------------------------
# Build directory
# ------------------------------------------------------------

$(BUILD):
        mkdir -p $(BUILD)

# ------------------------------------------------------------
# ElectroBench (fused: OG gun scene + TideBench ocean scene)
# ------------------------------------------------------------

.PHONY: legacy
legacy: $(LEGACY_BIN)

$(BUILD)/main.o: src/main.cxx | $(BUILD)
        $(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD)/ps14_bench_fused.o: src/ps14_bench.cxx | $(BUILD)
        $(CXX) $(CPPFLAGS) $(CXXFLAGS) $(FUSED_CXXFLAGS) -c $< -o $@

$(LEGACY_BIN): $(LEGACY_OBJS) | $(BUILD)
        @echo "========================================"
        @echo " Building ElectroBench (OG + TideBench fused)"
        @echo " Platform: $(PLATFORM)"
        @echo "========================================"
ifeq ($(STATIC),1)
        $(CXX) $(CXXFLAGS) $(STATIC_LDFLAGS) $(LEGACY_OBJS) -o $@ $(LDLIBS)
else
        $(CXX) $(CXXFLAGS) $(LEGACY_OBJS) -o $@ $(LDLIBS)
endif

# ------------------------------------------------------------
# Clean
# ------------------------------------------------------------

.PHONY: clean
clean:
        rm -rf $(BUILD)



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
        @echo "Libraries: $(LDLIBS)"
