# ===========================================================================
# ElectroBench -- PlayStation Vita port (SCENE 2 and SCENE 4 only).
#
# Builds one homebrew: src/vita_main.cxx plus the two scene translation units
# from src/, compiled against the SDL2/GLEW stand-ins in include/ and linked
# against vitaGL. Output is ./ElectroBench-vita.vpk at the repo root, next to
# the code. (tools/ is intentionally untracked from git; it lives locally.)
#
# From the repo root:
#   sh tools/build_deps.sh   # once: builds vitaGL + vitaShaRK into $VITASDK
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

# -Wl,-q keeps the relocation sections vita-elf-create needs. -DELECTROBENCH_VITA
# switches lib/asset_path.hxx to the app0: branch and marks the port's sources.
CXXFLAGS := -std=c++17 -O3 -ffast-math -mtune=cortex-a9 -mfpu=neon -ftree-vectorize \
            -DELECTROBENCH_VITA -Iinclude -Ilib
# -g gives vita-elf-create usable symbol names and costs nothing at runtime.
LDFLAGS := -Wl,-q

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

OBJS := $(BUILD)/vita_main.o $(BUILD)/vita_sdl.o $(BUILD)/scene2.o $(BUILD)/scene4.o

# The packed shader tree. Listing the files (not just the directory) matters:
# regenerating a shader rewrites it in place, which does not touch the
# directory's timestamp, so a directory-only prerequisite would leave the .vpk
# holding the previous revision of the shaders.
SHADERS := $(wildcard shaders/*/*.glsl)

.PHONY: all clean check vpk
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

$(BUILD)/icon0.png: tools/make_icon.py
	@mkdir -p $(BUILD)
	python3 tools/make_icon.py $@

# -------------------------------------------------------------------- checks
check:
	sh tools/check_shaders.sh

clean:
	rm -rf $(BUILD) $(VPK)
