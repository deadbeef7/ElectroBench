# ===========================================================================
# ElectroBench -- PlayStation Portable port (SCENE 2 and SCENE 4).
#
# The PSP's Graphics Engine has no shaders: this port rebuilds both scenes as
# fixed-function pipelines (sceGu) instead of translating GLSL.
#
# Scene 2 (dusk ocean): the wave field, sun palette, cloud deck and glitter
# gates are the same numbers as shaders/ps14/{sea,sky}_frag.glsl; the
# per-fragment work the GE cannot do (wave normals, cloud shadows, fresnel)
# runs per-vertex in VFPU float code on the 333 MHz Allegrex, and the
# reflection/glitter detail that needs to be per-pixel is carried by textures
# and the GE's blend units.
#
# Scene 4 (power lines): the static geometry is baked once into one indexed
# vertex array with per-vertex lighting from the object shader's terms, the
# cables are drawn as lines (a 3 cm conductor is sub-pixel at 480x272), depth
# haze is the GE's linear fog, and the sky is a CPU-baked equirect panorama
# plus an additive sun sprite.
#
# Build (needs a pspdev toolchain, see psp/README.md):
#   make -C psp          # -> psp/EBOOT.PBP
#   make -C psp clean
# ===========================================================================

TARGET := ElectroBench
OBJS := src/psp_main.o src/psp_scene2.o src/psp_scene4.o

# BUILD_PRX=1 is what makes this EBOOT bootable on a console. Without it
# build.mak packs the *stripped ELF* into DATA.PSP, and PPSSPP happily loads
# that while the PSP's own module manager rejects it (XMB: "The game could not
# be started. (8002013C)"). With it, the link uses prxspecs/linkfile.prx and
# psp-prxgen output (~PSP) is what DATA.PSP carries, which is what the loader
# on a CFW expects. The PRX stays unsigned - every CFW (PRO/ARK) accepts that;
# only OFW needs a Sony key we do not have.
BUILD_PRX = 1

# -O3 -ffast-math: the Allegrex FPU is the VFPU; gcc vectorises the per-vertex
# math into it and the LUT-based trig below is what keeps the shading inside a
# frame budget. -G0 keeps small globals in real memory (the GE DMAs vertex
# arrays and textures straight out of RAM, so they must not live in $gp).
CFLAGS := -O3 -G0 -Wall -ffast-math -fno-strict-aliasing
CXXFLAGS := $(CFLAGS) -std=gnu++17 -fno-rtti -I$(CURDIR)/../src
ASFLAGS := $(CFLAGS)

# The GE (sceGu), its VFPU matrix helper (sceGum), the VFPU library the
# compiler emits calls into under -ffast-math, and the system modules the app
# uses: controller (pause/exit), power (333 MHz clock), rtc, kernel.
# Order matters: libpspgum (sceGumDrawArray) calls sceGuSetMatrix out of
# libpspgu, so gum has to come first for the single-pass ld to resolve it.
#
# -lpspkernel is deliberately absent. Naming it here puts the *kernel* stubs in
# front of build.mak's user-mode tail, so ld satisfies sceKernelCreateThread,
# sceIoOpen, sceKernelGetSystemTimeWide and friends out of ThreadManForKernel /
# IoFileMgrForKernel / ... and the EBOOT asks the console for kernel libraries a
# user-mode module cannot import: "The game could not be started. (8002013C)".
# PPSSPP resolves them anyway, which is why only hardware failed. USE_USER_LIBS
# below keeps the tail itself kernel-free too.
LIBS := -lpspgum -lpspgu -lpspvfpu -lpspctrl -lpsppower -lpsprtc \
        -lpspdebug -lm

# build.mak links one of three default tails; the default one (neither flag set)
# ends in "-lpspuser -lpspkernel". A homebrew EBOOT must be a user-mode module,
# so take the user-only tail: no kernel stub is on the link line at all.

# 333/333/166: the maximum the hardware allows. The port's whole budget (mesh
# shading on the CPU, textures, framebuffers) is planned around it.
PSP_EBOOT_TITLE := ElectroBench PSP
PSP_EBOOT_ICON := ICON0.PNG
PSP_EBOOT_ICON1 := NULL

EXTRA_TARGETS := ICON0.PNG EBOOT.PBP
EXTRA_CLEAN := ICON0.PNG $(TARGET).prx

# User-only PSPSDK tail: -lpspuser without -lpspkernel (see LIBS above).
USE_USER_LIBS = 1

# PSPSDK's build.mak ships no suffix rules; these are the port's own. The
# sources are C++ because they share src/font_atlas.hxx with the other ports.
%.o: %.cxx
	$(CXX) $(CXXFLAGS) -c $< -o $@
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

include $(PSPSDK)/lib/build.mak

# The drawing lives in the shared generator so the PSP, Vita and Android icons
# are one design; the local script is only the 144x80 entry point.
ICON0.PNG: tools/make_icon_psp.py ../buildtools/make_icons.py
	python3 $< $@

# Host-side checks for the CPU math the VFPU kernel replaced (fast atan2/asin
# and the packed wave constants). The PSP cannot execute code on this host, so
# these run with the host compiler against the same headers the port uses.
.PHONY: check-math check-sky check-shade check-codegen
check-math:
	@mkdir -p ../build
	g++ -O2 -std=c++17 -I../src -Isrc ../buildtools/test_psp_math.cxx -o ../build/test_psp_math
	../build/test_psp_math

# The baked sky's cloud deck: bakes the panorama on the host with and without
# the texel-footprint filter and reports coverage / isolated-dot / variation
# counts (see buildtools/test_psp_skycloud.cxx). Pass PGM=path to also write
# the two coverage fields out for inspection.
check-sky:
	@mkdir -p ../build
	g++ -O2 -std=c++17 -Isrc ../buildtools/test_psp_skycloud.cxx -o ../build/test_psp_skycloud
	../build/test_psp_skycloud $(PGM)

# The baked object shading and the GE fog colour, against the colours the
# desktop/Vita frame carries (see buildtools/test_psp_shadetail.cxx).
check-shade:
	@mkdir -p ../build
	g++ -O2 -std=c++17 -Isrc ../buildtools/test_psp_shadetail.cxx -o ../build/test_psp_shadetail
	../build/test_psp_shadetail

check-codegen:
	sh ../buildtools/check_codegen.sh
