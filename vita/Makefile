# ===========================================================================
# ElectroBench -- PlayStation Vita port (SCENE 2 and SCENE 4 only).
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
ifneq ($(filter all vpk,$(MAKECMDGOALS)),)
ifneq ($(wildcard $(VITA_PACK_VPK) $(VITA_MAKE_FSELF) $(VITA_ELF_CREATE) $(CXX)),)
$(shell python3 ../buildtools/invalidate_stale_package.py $(VPK) "")
endif
endif
ifeq ($(strip $(MAKECMDGOALS)),)
ifneq ($(wildcard $(VITA_PACK_VPK) $(VITA_MAKE_FSELF) $(VITA_ELF_CREATE) $(CXX)),)
$(shell python3 ../buildtools/invalidate_stale_package.py $(VPK) "")
endif
endif

CXXFLAGS := -std=c++17 -O3 -ffast-math -mtune=cortex-a9 -mfpu=neon -ftree-vectorize \
            -DELECTROBENCH_VITA -Iinclude -Ilib \
            -pthread
LDFLAGS := -Wl,-q -pthread
LIBS := -lvitaGL -lvitashark -lSceShaccCgExt -lmathneon \
        -lSceGxm_stub -lSceDisplay_stub -lSceCtrl_stub -lSceSysmodule_stub \
        -lSceCommonDialog_stub -lSceAppUtil_stub -lSceAppMgr_stub \
        -lSceLibKernel_stub -lSceKernelDmacMgr_stub -lSceShaccCg_stub \
        -lScePower_stub -lSceTouch_stub -lSceKernelModulemgr_stub -ltaihen_stub \
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

$(BUILD)/icon0.png: ../buildtools/make_icon_vita.py ../buildtools/make_icons.py
	@mkdir -p $(BUILD)
	python3 ../buildtools/make_icon_vita.py $@
