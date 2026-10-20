# ===========================================================================
# ElectroBench -- PlayStation Portable port (SCENE 2, the dusk ocean).
#
# The PSP's Graphics Engine has no shaders: this port rebuilds scene 2 as a
# fixed-function pipeline (sceGu) instead of translating GLSL. The wave field,
# sun palette, cloud deck and glitter gates are the same numbers as
# shaders/ps14/{sea,sky}_frag.glsl; the per-fragment work the GE cannot do
# (wave normals, cloud shadows, fresnel) runs per-vertex in VFPU float code on
# the 333 MHz Allegrex, and the reflection/glitter detail that needs to be
# per-pixel is carried by textures and the GE's blend units.
#
# Build (needs a pspdev toolchain, see psp/README.md):
#   make -C psp          # -> psp/EBOOT.PBP
#   make -C psp clean
# ===========================================================================

TARGET := ElectroBench
OBJS := src/psp_main.o src/psp_scene2.o

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
LIBS := -lpspgum -lpspgu -lpspvfpu -lpspctrl -lpsppower -lpsprtc \
        -lpspdebug -lpspkernel -lm

# 333/333/166: the maximum the hardware allows. The port's whole budget (mesh
# shading on the CPU, textures, framebuffers) is planned around it.
PSP_EBOOT_TITLE := ElectroBench PSP
PSP_EBOOT_ICON := ICON0.PNG
PSP_EBOOT_ICON1 := NULL

EXTRA_TARGETS := ICON0.PNG EBOOT.PBP
EXTRA_CLEAN := ICON0.PNG $(TARGET).prx

# PSPSDK's build.mak ships no suffix rules; these are the port's own. The
# sources are C++ because they share src/font_atlas.hxx with the other ports.
%.o: %.cxx
	$(CXX) $(CXXFLAGS) -c $< -o $@
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

include $(PSPSDK)/lib/build.mak

ICON0.PNG: tools/make_icon_psp.py
	python3 $< $@
