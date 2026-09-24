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
# Legacy ElectroBench
# ------------------------------------------------------------

.PHONY: legacy
legacy: $(LEGACY_BIN)

$(LEGACY_BIN): $(LEGACY_SRC) | $(BUILD)
	@echo "========================================"
	@echo " Building ElectroBench Legacy"
	@echo " Platform: $(PLATFORM)"
	@echo "========================================"
ifeq ($(STATIC),1)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(STATIC_LDFLAGS) $< -o $@ $(LDLIBS)
else
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@ $(LDLIBS)
endif

# ------------------------------------------------------------
# TideBench (dusk ocean benchmark)
# ------------------------------------------------------------

.PHONY: tidebench
tidebench: $(TIDEBENCH_BIN)

$(TIDEBENCH_BIN): $(TIDEBENCH_SRC) | $(BUILD)
	@echo "========================================"
	@echo " Building TideBench"
	@echo " Platform: $(PLATFORM)"
	@echo "========================================"
ifeq ($(STATIC),1)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(STATIC_LDFLAGS) $< -o $@ $(LDLIBS)
else
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@ $(LDLIBS)
endif

# ------------------------------------------------------------
# Clean
# ------------------------------------------------------------

.PHONY: clean
clean:
	rm -rf $(BUILD)

# ------------------------------------------------------------
# Run
# ------------------------------------------------------------

.PHONY: run
run: legacy
	./$(LEGACY_BIN)

.PHONY: run-tidebench
run-tidebench: tidebench
	./$(TIDEBENCH_BIN)

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
