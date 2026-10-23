# ===========================================================================
TARGET := ElectroBench
OBJS := src/psp_main.o src/psp_scene2.o src/psp_scene4.o

BUILD_PRX = 1

CFLAGS := -O3 -G0 -Wall -ffast-math -fno-strict-aliasing
CXXFLAGS := $(CFLAGS) -std=gnu++17 -fno-rtti -I$(CURDIR)/../src
ASFLAGS := $(CFLAGS)

LIBS := -lpspgum -lpspgu -lpspvfpu -lpspctrl -lpsppower -lpsprtc \
        -lpspdebug -lm

PSP_EBOOT_TITLE := ElectroBench PSP
PSP_EBOOT_ICON := ICON0.PNG
PSP_EBOOT_ICON1 := NULL

EXTRA_TARGETS := ICON0.PNG EBOOT.PBP
EXTRA_CLEAN := ICON0.PNG $(TARGET).prx

USE_USER_LIBS = 1

PSP_HEADERS := $(wildcard src/*.hxx) ../src/font_atlas.hxx
%.o: %.cxx $(PSP_HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

include $(PSPSDK)/lib/build.mak

ICON0.PNG: tools/make_icon_psp.py ../buildtools/make_icons.py
	python3 $< $@

.PHONY: check-math check-sky check-shade check-codegen
check-math:
	@mkdir -p ../build
	g++ -O2 -std=c++17 -I../src -Isrc ../buildtools/test_psp_math.cxx -o ../build/test_psp_math
	../build/test_psp_math

check-sky:
	@mkdir -p ../build
	g++ -O2 -std=c++17 -Isrc ../buildtools/test_psp_skycloud.cxx -o ../build/test_psp_skycloud
	../build/test_psp_skycloud $(PGM)

check-shade:
	@mkdir -p ../build
	g++ -O2 -std=c++17 -Isrc ../buildtools/test_psp_shadetail.cxx -o ../build/test_psp_shadetail
	../build/test_psp_shadetail

check-codegen:
	sh ../buildtools/check_codegen.sh
