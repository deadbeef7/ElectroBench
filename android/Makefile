# ===========================================================================
# ElectroBench -- Android port (SCENE 2 and SCENE 4 only).
#
# Builds one APK: a NativeActivity whose libmain.so is android/jni's entry point
# plus the two scene translation units from src/, compiled against the GLES3 and
# SDL2 subsets in android/include and linked against EGL, GLESv3, log and
# android. The shader tree is packed into the APK's assets/ and unpacked by the
# app itself at startup (see android/jni/android_main.cxx).
#
# Output: ./ElectroBench-android.apk at the branch root, next to the code.
#
# From the branch root:
#   sh android/tools/fetch_toolchain.sh   # once: NDK + build-tools + platform
#   make -f android/Makefile              # -> ./ElectroBench-android.apk
#   make -f android/Makefile verify       # manifest, signature, ABIs, symbols
#   make -f android/Makefile check        # shader validation (same as Vita)
#   make -f android/Makefile host-shaders # ES 3 link check for all six programs
#
# There is no gradle step and no Java source anywhere: the APK contains no
# classes.dex (android:hasCode="false" plus the framework's own NativeActivity),
# so a JDK is only needed for apksigner and keytool.
# ===========================================================================

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)

NDK ?= $(ANDROID_NDK_HOME)
SDK ?= $(ANDROID_SDK_ROOT)

# Without these, make resolves $(GLUE) inside the NDK to '/sources/...' and stops
# with "No rule to make target", which says nothing about the actual problem.
# A warning (not an error) so that `make clean` and `make doctor` still work.
ifeq ($(strip $(NDK)),)
$(warning ANDROID_NDK_HOME is not set: run 'sh android/fetch_toolchain.sh', or pass NDK=/path/to/ndk)
endif
ifeq ($(strip $(SDK)),)
$(warning ANDROID_SDK_ROOT is not set: run 'sh android/fetch_toolchain.sh', or pass SDK=/path/to/sdk)
endif
BUILD_TOOLS ?= $(SDK)/build-tools/34.0.0
PLATFORM_JAR ?= $(SDK)/platforms/android-34/android.jar

# The NDK ships one prebuilt per host; the name is what decides which one.
HOST_TAG ?= $(shell uname -s | tr '[:upper:]' '[:lower:]')-x86_64
TOOLCHAIN := $(NDK)/toolchains/llvm/prebuilt/$(HOST_TAG)
GLUE := $(NDK)/sources/android/native_app_glue/android_native_app_glue.c

AAPT2 ?= $(BUILD_TOOLS)/aapt2
ZIPALIGN ?= $(BUILD_TOOLS)/zipalign
APKSIGNER ?= $(BUILD_TOOLS)/apksigner
STRIP := $(TOOLCHAIN)/bin/llvm-strip
READELF := $(TOOLCHAIN)/bin/llvm-readelf
KEYTOOL ?= keytool

# 24 is the minimum that makes sense for this port: it is the oldest API level
# with a documented ES 3.0 EGL context and an up-to-date NDK sysroot, and it is
# what the SDK's own APK Signature Scheme v2 verification starts at.
API := 24
MIN_SDK := 24
TARGET_SDK := 34

# arm64-v8a covers every device that matters now, armeabi-v7a the older 32-bit
# ones, x86_64 the emulator (so the APK can be tried without a phone).
ABIS := arm64-v8a armeabi-v7a x86_64

BUILD := $(ROOT)/build-android
APK := $(ROOT)/ElectroBench-android.apk
KEYSTORE := $(BUILD)/debug.keystore
STAGE := $(BUILD)/apk

# Per-ABI compiler and architecture flags. The toolchain wrappers encode the API
# level in their name, so there is no --target/--sysroot juggling here.
CXX_arm64-v8a := $(TOOLCHAIN)/bin/aarch64-linux-android$(API)-clang++
CC_arm64-v8a := $(TOOLCHAIN)/bin/aarch64-linux-android$(API)-clang
ARCH_arm64-v8a := -march=armv8-a

CXX_armeabi-v7a := $(TOOLCHAIN)/bin/armv7a-linux-androideabi$(API)-clang++
CC_armeabi-v7a := $(TOOLCHAIN)/bin/armv7a-linux-androideabi$(API)-clang
ARCH_armeabi-v7a := -march=armv7-a -mfpu=neon -mfloat-abi=softfp

CXX_x86_64 := $(TOOLCHAIN)/bin/x86_64-linux-android$(API)-clang++
CC_x86_64 := $(TOOLCHAIN)/bin/x86_64-linux-android$(API)-clang
ARCH_x86_64 := -march=x86-64

# -Iandroid/include comes first: it is what makes <GL/glew.h> and
# <SDL2/SDL_opengl.h> resolve to the GLES3 stand-ins instead of vitaport's
# vitaGL ones. <SDL2/SDL.h> is the shared subset header both ports implement.
# android_native_app_glue.h comes from the NDK itself (the glue is part of the
# NDK, not vendored here), so its directory is on the include path too.
INCLUDES := -I$(ROOT)/android/include -I$(ROOT)/include -I$(ROOT)/lib \
            -I$(NDK)/sources/android/native_app_glue
# -pthread: src/jobs.hxx hands the scenes' startup CPU work to worker threads
# (pthreads on every target; see that header for why). The flag is on the
# compile AND the link line, and for Android it must also reach the host
# harness below, which builds the same translation units.
CXXFLAGS := -std=c++17 -O3 -ffast-math -fPIC -Wall -pthread $(INCLUDES)
CFLAGS := -O2 -fPIC
# -static-libstdc++: the NDK's clang++ defaults to the *shared* libc++ runtime,
# which would leave libmain.so with a NEEDED entry for libc++_shared.so that the
# APK does not contain (dlopen would fail on the device). One self-contained .so
# per ABI is the whole point of this port, so the runtime is linked in.
LDFLAGS := -shared -static-libstdc++ -pthread -Wl,-u,ANativeActivity_onCreate -Wl,--no-undefined
LIBS := -lEGL -lGLESv3 -llog -landroid -lm

# lib/lodepng.c is deliberately absent: the vendored lodepng.h is a header-only
# amalgamation that includes it, so android_sdl.cxx compiles the encoder by
# including the header. Compiling the .c separately as well double-defines every
# lodepng symbol.
PORT_CPP := $(ROOT)/android/jni/android_main.cxx \
            $(ROOT)/android/jni/android_sdl.cxx \
            $(ROOT)/src/jobs.cxx \
            $(ROOT)/src/scene2.cxx \
            $(ROOT)/src/scene4.cxx
PORT_HEADERS := $(ROOT)/android/jni/android_port.hxx \
                $(ROOT)/android/include/GL/glew.h \
                $(ROOT)/android/include/SDL2/SDL_opengl.h \
                $(ROOT)/include/SDL2/SDL.h \
                $(ROOT)/src/font_atlas.hxx \
                $(ROOT)/src/jobs.hxx
SOURCES := $(PORT_CPP) $(PORT_HEADERS)

# The packed shader tree. Listing the files (not the directory) is deliberate:
# regenerating a shader rewrites it in place, which leaves the directory's
# timestamp alone, so a directory prerequisite would repack the previous
# revision of the shaders.
SHADERS := $(wildcard $(ROOT)/shaders/*/*.glsl)
RES := $(BUILD)/res/mipmap/ic_launcher.png
SO_FILES := $(foreach abi,$(ABIS),$(BUILD)/$(abi)/libmain.so)

.PHONY: all clean check verify doctor apk host-render host-png host-shaders
all: $(APK)

# ------------------------------------------- host OpenGL ES 3 render check
# The APK cannot be run here: no device, no emulator, no KVM. So the scenes and
# the shader tree are instead rendered through the host's own OpenGL ES 3 driver
# (Mesa, software rasteriser) by the small harness in android/host/. It is a
# check of the scenes and shaders under ES 3.0 - the API level the APK targets -
# and it is where the frames in android/screenshots/ come from. It says nothing
# about the Android activity/EGL glue, which only a device can exercise.
HOST_CXX ?= g++
HOST_CFLAGS := -std=c++17 -O2 -pthread -I$(ROOT)/android/include -I$(ROOT)/include -I$(ROOT)/lib
HOST_SRCS := $(ROOT)/android/host/host_main.cxx $(ROOT)/android/host/host_sdl.cxx \
             $(ROOT)/src/jobs.cxx $(ROOT)/src/scene2.cxx $(ROOT)/src/scene4.cxx
HOST_BIN := $(BUILD)/host/host_bench
HOST_SHOT_SECONDS ?= 3.0

$(HOST_BIN): $(HOST_SRCS) $(SHADERS)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_CFLAGS) -o $@ $(HOST_SRCS) -lEGL -lGLESv2

host-render: $(HOST_BIN)
	@for scene in 2 4; do \
	    pp="$(BUILD)/host/scene$$scene.ppm"; \
	    echo "--- scene $$scene"; \
	    if command -v xvfb-run >/dev/null 2>&1; then \
	        LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1366x768x24" \
	            $(HOST_BIN) $$scene $$pp $(HOST_SHOT_SECONDS); \
	    else \
	        LIBGL_ALWAYS_SOFTWARE=1 $(HOST_BIN) $$scene $$pp $(HOST_SHOT_SECONDS); \
	    fi; \
	done
	@ls -l $(BUILD)/host/*.ppm

# host-shaders is deliberately separate from host-render: it needs a GL context
# but no frames, so it runs in about a second and can gate every change to a
# shader. It is the only check here that sees a program's two stages together -
# the sea shipped dead once with each stage valid on its own and the pair
# unlinkable (see android/host/host_main.cxx).
host-shaders: $(HOST_BIN)
	@if command -v xvfb-run >/dev/null 2>&1; then \
	    LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 640x480x24" \
	        $(HOST_BIN) --check-shaders; \
	else \
	    LIBGL_ALWAYS_SOFTWARE=1 $(HOST_BIN) --check-shaders; \
	fi

host-png: host-render
	@mkdir -p $(ROOT)/android/screenshots
	python3 $(ROOT)/android/ppm_to_png.py $(BUILD)/host/scene2.ppm $(ROOT)/android/screenshots/scene2_dusk_ocean.png
	python3 $(ROOT)/android/ppm_to_png.py $(BUILD)/host/scene4.ppm $(ROOT)/android/screenshots/scene4_power_lines.png

# ------------------------------------------------------------------ native lib
# One rule per ABI, generated so the three toolchains cannot drift apart.
define ABI_RULE
$(BUILD)/$(1)/libmain.so: $(SOURCES) $(GLUE)
	@mkdir -p $(BUILD)/$(1)
	$(CC_$(1)) $(CFLAGS) -c $(GLUE) -o $(BUILD)/$(1)/native_app_glue.o
	$(CXX_$(1)) $(CXXFLAGS) $(ARCH_$(1)) $(LDFLAGS) -o $$@ $(PORT_CPP) \
	    $(BUILD)/$(1)/native_app_glue.o $(LIBS)
	$(STRIP) --strip-unneeded $$@
	@echo "linked $$@"
endef
$(foreach abi,$(ABIS),$(eval $(call ABI_RULE,$(abi))))

# ------------------------------------------------------------------------ icon
$(RES): $(ROOT)/android/make_android_icon.py
	@mkdir -p $(dir $@)
	python3 $< $@

# ----------------------------------------------------------------------- debug
# A throwaway signing identity. Anything installed from this APK must be
# uninstalled before an APK signed with a different key can replace it, and a
# rebuilt APK gets a new key unless this file is kept (it lives in
# build-android/, which is not committed).
$(KEYSTORE):
	@mkdir -p $(BUILD)
	$(KEYTOOL) -genkeypair -keystore $@ -storepass android -keypass android \
	    -alias androiddebugkey -dname "CN=ElectroBench Debug,O=ElectroBench,C=US" \
	    -keyalg RSA -keysize 2048 -validity 10000

# -------------------------------------------------------------------------- APK
$(APK): $(SO_FILES) $(RES) $(SHADERS) $(ROOT)/android/AndroidManifest.xml $(KEYSTORE)
	@rm -rf $(STAGE)
	@mkdir -p $(STAGE)/assets
	cp -R $(ROOT)/shaders $(STAGE)/assets/shaders
	@for abi in $(ABIS); do \
	    mkdir -p $(STAGE)/lib/$$abi; \
	    cp $(BUILD)/$$abi/libmain.so $(STAGE)/lib/$$abi/; \
	done
	$(AAPT2) compile --dir $(BUILD)/res -o $(BUILD)/res.zip
	$(AAPT2) link -o $(BUILD)/app-unsigned.apk -I $(PLATFORM_JAR) \
	    --manifest $(ROOT)/android/AndroidManifest.xml -A $(STAGE)/assets \
	    --min-sdk-version $(MIN_SDK) --target-sdk-version $(TARGET_SDK) \
	    --version-code 1 --version-name 1.0 $(BUILD)/res.zip
	# -0 keeps the .so files stored rather than deflated, which is what
	# zipalign -p 4 then page-aligns; extractNativeLibs="true" makes the app
	# independent of that, but a page-aligned APK stays installable either way.
	cd $(STAGE) && zip -q -0 -r -X $(BUILD)/app-unsigned.apk lib
	$(ZIPALIGN) -p -f 4 $(BUILD)/app-unsigned.apk $(BUILD)/app-aligned.apk
	# --v4-signing-enabled false: the v4 .idsig sidecar is only used by adb's
	# incremental install and would otherwise be left next to the APK.
	$(APKSIGNER) sign --ks $(KEYSTORE) --ks-pass pass:android --key-pass pass:android \
	    --ks-key-alias androiddebugkey --min-sdk-version $(MIN_SDK) \
	    --v4-signing-enabled false --out $@ $(BUILD)/app-aligned.apk
	@echo "APK: $@ ($$(stat -c%s $@) bytes)"

# ----------------------------------------------------------------------- checks
# Same shader validation the Vita port runs: every shader must be valid GLSL ES
# 1.00, which is also what an ES 3.0 context compiles them as (no #version line
# means 1.00). Kept in the shared tools/ script so the two ports cannot drift.
check:
	sh $(ROOT)/tools/check_shaders.sh

verify: $(APK)
	@echo "--- manifest (binary manifest read back by aapt2)"
	@ls -l $(SO_FILES) 2>/dev/null || true
	$(AAPT2) dump badging $(APK) | grep -E "^(package|sdkVersion|targetSdkVersion|application-label:|launchable-activity|native-code|uses-feature|feature-group)" || true
	@echo "--- signature (every scheme apksigner can check)"
	$(APKSIGNER) verify -v --min-sdk-version $(MIN_SDK) --print-certs $(APK)
	@echo "--- archive"
	unzip -l $(APK)
	@echo "--- native libraries"
	@for abi in $(ABIS); do \
	    echo "  $$abi:"; \
	    $(READELF) -h $(BUILD)/$$abi/libmain.so | grep -E "Class|Machine|Type"; \
	    $(READELF) -d $(BUILD)/$$abi/libmain.so | grep NEEDED; \
	    $(READELF) --dyn-syms $(BUILD)/$$abi/libmain.so | grep -E "ANativeActivity_onCreate|android_main" || echo "    MISSING entry point"; \
	done

doctor:
	@echo "NDK          : $(NDK)"
	@echo "SDK          : $(SDK)"
	@echo "build-tools  : $(BUILD_TOOLS)"
	@echo "android.jar  : $(PLATFORM_JAR)"
	@echo "toolchain    : $(TOOLCHAIN)"
	@for f in $(TOOLCHAIN)/bin/aarch64-linux-android$(API)-clang++ $(GLUE) $(AAPT2) $(APKSIGNER) $(ZIPALIGN) $(PLATFORM_JAR); do \
	    if [ -e "$$f" ]; then echo "ok      $$f"; else echo "MISSING $$f"; fi; \
	done

clean:
	rm -rf $(BUILD) $(APK)
