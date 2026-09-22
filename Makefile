# ============================================================
# ElectroBench Makefile
# Windows (MSYS2), macOS (Homebrew), Linux
# ============================================================

CXX ?= g++

CXXFLAGS := -std=c++17 -O2 -march=x86-64 -mtune=generic
CPPFLAGS := -Ilib

BUILD := build

LEGACY_SRC := src/main.cxx
PS14_SRC   := src/ps14_bench.cxx

LEGACY_BIN := $(BUILD)/ElectroBench
PS14_BIN   := $(BUILD)/PS14SeaBenchmark

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

    PKG_CFLAGS := $(shell pkg-config --cflags sdl2 glew)
    PKG_LIBS   := $(shell pkg-config --libs sdl2)

    CXXFLAGS += $(PKG_CFLAGS)

    LDLIBS := $(PKG_LIBS) \
              -lglew32 \
              -lglu32 \
              -lopengl32

    LEGACY_BIN := $(BUILD)/ElectroBench.exe
    PS14_BIN   := $(BUILD)/PS14SeaBenchmark.exe

else ifeq ($(PLATFORM),macos)

    PKG_CFLAGS := $(shell pkg-config --cflags sdl2 glew)
    PKG_LIBS   := $(shell pkg-config --libs sdl2 glew)

    CXXFLAGS += $(PKG_CFLAGS)

    LDLIBS := $(PKG_LIBS) \
              -framework OpenGL

else ifeq ($(PLATFORM),linux)

    PKG_CFLAGS := $(shell pkg-config --cflags sdl2 glew)
    PKG_LIBS   := $(shell pkg-config --libs sdl2 glew)

    CXXFLAGS += $(PKG_CFLAGS)

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
all: legacy ps14

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
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@ $(LDLIBS)

# ------------------------------------------------------------
# PS1.4 Sea Benchmark
# ------------------------------------------------------------

.PHONY: ps14
ps14: $(PS14_BIN)

$(PS14_BIN): $(PS14_SRC) | $(BUILD)
	@echo "========================================"
	@echo " Building PS1.4 Sea Benchmark"
	@echo " Platform: $(PLATFORM)"
	@echo "========================================"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@ $(LDLIBS)

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

.PHONY: run-ps14
run-ps14: ps14
	./$(PS14_BIN)

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
	@echo "Libraries: $(LDLIBS)"
