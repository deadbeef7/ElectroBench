# ===========================================================================
# ElectroBench -- PlayStation Vita port (SCENE 2 and SCENE 4 only).
#
# Builds one homebrew: src/vita_main.cxx plus the two scene translation units
# from src/, compiled against the SDL2/GLEW stand-ins in include/ and linked
# against vitaGL. Output is ./ElectroBench-vita.vpk at the repo root, next to
# the code. (tools/ is intentionally untracked from git; it lives locally.)
#
# From the repo root:
#   Install/build vitaGL, vitaShaRK and math-neon into $VITASDK (see README.md)
#   make                     # -> ./ElectroBench-vita.vpk
#   make check               # shader validation only
# ===========================================================================

VITASDK ?= /usr/local/vitasdk
TOOLPREFIX := $(VITASDK)/bin/arm-vita-eabi-

CC := $(TOOLPREFIX)gcc
CXX := $(TOOLPREFIX)g++
VITA_ELF_CREATE := $(VITASDK)/bin/vita-elf-create
VITA_MAKE_FSELF := $(VITASDK)/bin/vita-make-fself
VITA_MKSFOEX := $(VITASDK)/bin/vita-mksfoex
VITA_PACK_VPK := $(VITASDK)/bin/vita-pack-vpk

TITLE := ElectroBench
TITLE_ID := ELEB00002

BUILD := build-vita
ELF := $(BUILD)/ElectroBench-vita.elf
VELF := $(BUILD)/ElectroBench-vita.velf
EBOOT := $(BUILD)/eboot.bin
SFO := $(BUILD)/param.sfo
VPK := ElectroBench-vita.vpk

# A checkout can give source files and a tracked VPK identical mtimes. When the
# Vita tools are installed, compare embedded shader bytes before Make decides
# the package is up to date; remove only a confirmed-stale package.
ifneq ($(filter all vpk,$(MAKECMDGOALS)),)
ifneq ($(wildcard $(VITA_PACK_VPK) $(VITA_MAKE_FSELF) $(VITA_ELF_CREATE) $(CXX)),)
$(shell python3 buildtools/invalidate_stale_package.py $(VPK) "")
endif
endif
ifeq ($(strip $(MAKECMDGOALS)),)
ifneq ($(wildcard $(VITA_PACK_VPK) $(VITA_MAKE_FSELF) $(VITA_ELF_CREATE) $(CXX)),)
$(shell python3 buildtools/invalidate_stale_package.py $(VPK) "")
endif
endif

# -Wl,-q keeps the relocation sections vita-elf-create needs. -DELECTROBENCH_VITA
# switches lib/asset_path.hxx to the app0: branch and marks the port's sources.
CXXFLAGS := -std=c++17 -O3 -ffast-math -mtune=cortex-a9 -mfpu=neon -ftree-vectorize \
            -DELECTROBENCH_VITA -Iinclude -Ilib \
            -pthread
# -g gives vita-elf-create usable symbol names and costs nothing at runtime.
# -pthread for the worker threads in src/jobs.hxx: the scenes build their
# startup data on the console's spare cores while the GL thread compiles
# shaders. vitaSDK ships libpthread.a, which is what -pthread resolves to here.
LDFLAGS := -Wl,-q -pthread

# vitaGL pulls in the rest (SceGxm, SceDisplay, the shader compiler, the
# allocator and thread plumbing, taiHEN's kernel bridge for a couple of paths).
# SceShaccCgExt + taiHEN: the real extension library statically patches the
# loaded SceShaccCg module to inject our intrinsic set (bit_cast and friends)
# and re-enable extension pragmas, which the previous no-op stub destroyed —
# that stub is why every runtime GLSL compile failed with
# "unexpected undeclared identifier 'bit_cast'" on device and in Vita3K.
LIBS := -lvitaGL -lvitashark -lSceShaccCgExt -lmathneon \
        -lSceGxm_stub -lSceDisplay_stub -lSceCtrl_stub -lSceSysmodule_stub \
        -lSceCommonDialog_stub -lSceAppUtil_stub -lSceAppMgr_stub \
        -lSceLibKernel_stub -lSceKernelDmacMgr_stub -lSceShaccCg_stub \
        -lScePower_stub -lSceTouch_stub -lSceKernelModulemgr_stub -ltaihen \
        -lstdc++ -lm -lc

OBJS := $(BUILD)/vita_main.o $(BUILD)/vita_sdl.o $(BUILD)/jobs.o \
        $(BUILD)/scene2.o $(BUILD)/scene4.o

# The packed shader tree. Listing the files (not just the directory) matters:
# regenerating a shader rewrites it in place, which does not touch the
# directory's timestamp, so a directory-only prerequisite would leave the .vpk
# holding the previous revision of the shaders.
SHADERS := $(wildcard shaders/*/*.glsl)

.PHONY: all clean check vpk check-codegen check-artifacts host-render
all: $(VPK)

# ------------------------------------------------------------------ compile
$(BUILD)/%.o: src/%.cxx
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# The two ported scenes compile from the shared sources, unchanged.
$(BUILD)/scene2.o: src/scene2.cxx
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/scene4.o: src/scene4.cxx
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# libvitaGL.a is a prerequisite so a rebuilt/patched vitaGL (build_deps.sh or a
# manual library fix) always relinks into the eboot — objects alone would leave
# the VPK shipping a stale GL library.
$(ELF): $(OBJS) $(VITASDK)/arm-vita-eabi/lib/libvitaGL.a
	$(CXX) $(LDFLAGS) $(OBJS) $(LIBS) -o $@
	$(TOOLPREFIX)size $@

$(VELF): $(ELF)
	$(VITA_ELF_CREATE) $< $@

# No -s (safe) eboot: the runtime shader compiler has to be able to load
# libshacccg.suprx from ur0:/data, which a safe eboot cannot reach.
$(EBOOT): $(VELF)
	$(VITA_MAKE_FSELF) -c -na $< $@

$(SFO):
	$(VITA_MKSFOEX) -s TITLE_ID=$(TITLE_ID) -d ATTRIBUTE2=12 "$(TITLE)" $@

# The packed tree is the app0: filesystem: shaders/ is what
# resolveAssetPath() reads on Vita (shaders/ holds the GLSL 1.00
# rewrites of the ported scenes' shaders).
$(VPK): $(EBOOT) $(SFO) $(BUILD)/icon0.png $(SHADERS)
	$(VITA_PACK_VPK) -s $(SFO) -b $(EBOOT) \
		-a $(BUILD)/icon0.png=sce_sys/icon0.png \
		-a shaders=shaders \
		$@
	@echo "VPK: $@ ($$(stat -c%s $@ ) bytes)"

# Shared drawing (buildtools/make_icons.py) so the Vita icon cannot drift from
# the PSP and Android ones; make_icon_vita.py is only the 128x128 palettised
# entry point. buildtools/ is tracked precisely because `make` needs it.
$(BUILD)/icon0.png: buildtools/make_icon_vita.py buildtools/make_icons.py
	@mkdir -p $(BUILD)
	python3 buildtools/make_icon_vita.py $@

# ------------------------------------------------- host render, Vita code paths
# Vita3K cannot hand back a frame in this sandbox (its readback comes out black
# whatever the VPK contains), so the way to actually LOOK at the port's shaders
# is to run the same scene translation units on the host's own OpenGL ES 3
# driver and let the scenes' screenshot path write the frame:
#
#   make host-render    # -> build-vita/host/scene2.ppm, scene4.ppm
#
# The scenes are compiled with -DELECTROBENCH_VITA, exactly like the VPK, so the
# frames follow the port's own branches: uEnvCube = 0 (the sea evaluates the sky
# gradient analytically instead of sampling the cubemap), no environment probe
# pass, no MSAA, the same shaders/*.glsl bytes that get packed into the archive.
# ELECTROBENCH_ASSET_PREFIX="" makes resolveAssetPath() read the checkout
# instead of the console's app0: mount. What this does NOT cover is everything
# below the GL layer: SceShaccCg's fp16 mediump, vitaGL's GL->GXM translation and
# the SceCtrl/display glue. Only a console (or Vita3K) exercises those.
HOST_CXX ?= g++
HOST_CFLAGS := -std=c++17 -O2 -pthread -DELECTROBENCH_VITA \
               -DELECTROBENCH_ASSET_PREFIX='""' \
               -Iandroid/include -Iinclude -Ilib
HOST_SRCS := android/host/host_main.cxx android/host/host_sdl.cxx \
             src/jobs.cxx src/scene2.cxx src/scene4.cxx
HOST_BIN := $(BUILD)/host/host_vita
HOST_SHOT_SECONDS ?= 3.0

$(HOST_BIN): $(HOST_SRCS) $(SHADERS)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_CFLAGS) -o $@ $(HOST_SRCS) -lEGL -lGLESv2

host-render: $(HOST_BIN)
	@for scene in 2 4; do \
	    echo "--- scene $$scene"; \
	    if command -v xvfb-run >/dev/null 2>&1; then \
	        LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1366x768x24" \
	            $(HOST_BIN) $$scene $(BUILD)/host/scene$$scene.ppm $(HOST_SHOT_SECONDS); \
	    else \
	        LIBGL_ALWAYS_SOFTWARE=1 \
	            $(HOST_BIN) $$scene $(BUILD)/host/scene$$scene.ppm $(HOST_SHOT_SECONDS); \
	    fi; \
	done
	@ls -l $(BUILD)/host/*.ppm

# -------------------------------------------------------------------- checks
check:
	sh buildtools/check_shaders.sh

# Verify that the tracked VPK/APK actually contain the current shader bytes.
# This is content-based because checkout timestamps cannot reliably detect stale archives.
check-artifacts:
	python3 buildtools/check_artifacts.py

# Asserts the PSP VFPU kernel and this build's NEON codegen are present in the
# objects (see buildtools/check_codegen.sh). Needs the PSP port built too.
check-codegen:
	sh buildtools/check_codegen.sh

clean:
	rm -rf $(BUILD) $(VPK)
