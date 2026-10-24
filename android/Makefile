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
# The pinned layout fetch_toolchain.sh creates is build-tools/34.0.0, but an
# SDK installed by Android Studio keeps the platform codename (android-14 for
# build-tools 34). Accept either: prefer the pinned name, otherwise use the
# last directory the SDK has, which is the newest one.
BUILD_TOOLS ?= $(shell if [ -d "$(SDK)/build-tools/34.0.0" ]; then \
	  echo "$(SDK)/build-tools/34.0.0"; \
	else ls -d "$(SDK)"/build-tools/* 2>/dev/null | tail -1; fi)
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

API := 24
MIN_SDK := 24
TARGET_SDK := 34

ABIS := arm64-v8a armeabi-v7a x86_64

BUILD := $(ROOT)/build-android
APK := $(ROOT)/ElectroBench-android.apk
.PHONY: all clean check verify doctor apk host-render host-png host-shaders check-artifacts
all: $(APK)

ifneq ($(filter all apk,$(MAKECMDGOALS)),)
ifneq ($(strip $(MAKECMDGOALS)),clean)
ifneq ($(wildcard $(CXX_arm64-v8a) $(CXX_armeabi-v7a) $(CXX_x86_64) $(AAPT2) $(APKSIGNER) $(ZIPALIGN) $(GLUE)),)
$(shell python3 $(ROOT)/buildtools/invalidate_stale_package.py $(APK) "assets/")
endif
endif
endif
ifeq ($(strip $(MAKECMDGOALS)),)
ifneq ($(wildcard $(CXX_arm64-v8a) $(CXX_armeabi-v7a) $(CXX_x86_64) $(AAPT2) $(APKSIGNER) $(ZIPALIGN) $(GLUE)),)
$(shell python3 $(ROOT)/buildtools/invalidate_stale_package.py $(APK) "assets/")
endif
endif
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
INCLUDES := -I$(ROOT)/android/include -I$(ROOT)/vita/include -I$(ROOT)/vita/lib \
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
            $(ROOT)/vita/src/jobs.cxx \
            $(ROOT)/vita/src/scene2.cxx \
            $(ROOT)/vita/src/scene4.cxx
PORT_HEADERS := $(ROOT)/android/jni/android_port.hxx \
                $(ROOT)/android/include/GL/glew.h \
                $(ROOT)/android/include/SDL2/SDL_opengl.h \
                $(ROOT)/vita/include/SDL2/SDL.h \
                $(ROOT)/vita/src/font_atlas.hxx \
                $(ROOT)/vita/src/jobs.hxx
SOURCES := $(PORT_CPP) $(PORT_HEADERS)

# The packed shader tree. Listing the files (not the directory) is deliberate:
# regenerating a shader rewrites it in place, which leaves the directory's
# timestamp alone, so a directory prerequisite would repack the previous
# revision of the shaders.
SHADERS := $(wildcard $(ROOT)/vita/shaders/*/*.glsl)
# One launcher bitmap per density, generated from the shared four-colour mark
# in buildtools/make_icons.py. The launcher does not downsample a single 256x256
# file well (and never sharpens it), so mdpi gets a real 48px drawing.
ICON_DENSITIES := mdpi hdpi xhdpi xxhdpi xxxhdpi
ICON_SIZE_mdpi := 48
ICON_SIZE_hdpi := 72
ICON_SIZE_xhdpi := 96
ICON_SIZE_xxhdpi := 144
ICON_SIZE_xxxhdpi := 192
RES := $(foreach d,$(ICON_DENSITIES),$(BUILD)/res/mipmap-$(d)/ic_launcher.png)
# The pre-density path is left over in build trees made before this rule; it
# would otherwise be compiled alongside the new ones.
STALE_RES := $(BUILD)/res/mipmap
SO_FILES := $(foreach abi,$(ABIS),$(BUILD)/$(abi)/libmain.so)

.PHONY: all clean check verify doctor apk host-render host-png host-shaders FORCE
all: $(APK)

# The project is commonly distributed as a ZIP, which may normalize mtimes.
# Force APK repacking so the embedded shaders can never silently lag the tree.
FORCE:

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
$(BUILD)/res/mipmap-%/ic_launcher.png: $(ROOT)/android/make_android_icon.py $(ROOT)/buildtools/make_icons.py
	@mkdir -p $(dir $@)
	@rm -rf $(STALE_RES)
	python3 $< $@ --size $(ICON_SIZE_$*)

# ----------------------------------------------------------------------- debug
# A throwaway signing identity. Anything installed from this APK must be
# uninstalled before an APK signed with a different key can replace it, and a
# rebuilt APK gets a new key unless this file is kept (it lives in
# build-android/, which is not committed).
# Keeping the identity across builds is the point (an APK signed with a new key
# cannot replace the installed one), so `make -B` must not re-run keytool into
# "alias <androiddebugkey> already exists": reuse the existing keystore.
$(KEYSTORE):
	@mkdir -p $(BUILD)
	@if [ -f $@ ]; then \
	    echo "reusing signing identity $@"; \
	else \
	    $(KEYTOOL) -genkeypair -keystore $@ -storepass android -keypass android \
	        -alias androiddebugkey -dname "CN=ElectroBench Debug,O=ElectroBench,C=US" \
	        -keyalg RSA -keysize 2048 -validity 10000; \
	fi

# -------------------------------------------------------------------------- APK
$(APK): FORCE $(SO_FILES) $(RES) $(SHADERS) $(ROOT)/android/AndroidManifest.xml $(KEYSTORE)
	@rm -rf $(STAGE)
	@mkdir -p $(STAGE)/assets
	cp -R $(ROOT)/vita/shaders $(STAGE)/assets/shaders
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
