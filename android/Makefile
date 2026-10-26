ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)

# Both spellings: local toolchain scripts set ANDROID_NDK_HOME/ANDROID_SDK_ROOT,
# F-Droid's build server exports ANDROID_NDK/ANDROID_HOME. NDK=/SDK= still wins.
NDK ?= $(or $(ANDROID_NDK_HOME),$(ANDROID_NDK))
SDK ?= $(or $(ANDROID_SDK_ROOT),$(ANDROID_HOME))
ifeq ($(strip $(NDK)),)
$(warning ANDROID_NDK_HOME is not set: run 'sh android/fetch_toolchain.sh', or pass NDK=/path/to/ndk)
endif
ifeq ($(strip $(SDK)),)
$(warning ANDROID_SDK_ROOT is not set: run 'sh android/fetch_toolchain.sh', or pass SDK=/path/to/sdk)
endif
# Newest NUMERIC build-tools: a plain sort puts "android-14" after "34.0.0".
BUILD_TOOLS ?= $(or $(wildcard $(SDK)/build-tools/34.0.0),\
	 $(lastword $(shell ls -d $(SDK)/build-tools/[0-9]* 2>/dev/null | sort -V)),\
	 $(lastword $(sort $(wildcard $(SDK)/build-tools/*))))
PLATFORM_JAR ?= $(or $(wildcard $(SDK)/platforms/android-34/android.jar),\
	 $(lastword $(shell ls -d $(SDK)/platforms/android-*/android.jar 2>/dev/null | sort -V)))

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

# Bump together with the fastlane changelog and the F-Droid recipe's Builds entry.
VERSION_CODE ?= 1
VERSION_NAME ?= 0.5

ABIS := arm64-v8a armeabi-v7a x86_64

BUILD := $(ROOT)/build-android
APK := $(ROOT)/ElectroBench-android.apk
.PHONY: all clean check verify doctor apk unsigned host-render host-png host-shaders check-artifacts
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

INCLUDES := -I$(ROOT)/android/include -I$(ROOT)/vita/include -I$(ROOT)/vita/lib \
            -I$(NDK)/sources/android/native_app_glue
CXXFLAGS := -std=c++17 -O3 -ffast-math -fPIC -Wall -pthread $(INCLUDES)
CFLAGS := -O2 -fPIC
LDFLAGS := -shared -static-libstdc++ -pthread -Wl,-u,ANativeActivity_onCreate -Wl,--no-undefined
LIBS := -lEGL -lGLESv3 -llog -landroid -lm

PORT_CPP := $(ROOT)/android/jni/android_main.cxx \
            $(ROOT)/android/jni/android_sdl.cxx \
            $(ROOT)/vita/src/jobs.cxx \
            $(ROOT)/vita/src/scene1.cxx \
            $(ROOT)/vita/src/scene2.cxx
PORT_HEADERS := $(ROOT)/android/jni/android_port.hxx \
                $(ROOT)/android/include/GL/glew.h \
                $(ROOT)/android/include/SDL2/SDL_opengl.h \
                $(ROOT)/vita/include/SDL2/SDL.h \
                $(ROOT)/vita/src/font_atlas.hxx \
                $(ROOT)/vita/src/jobs.hxx
SOURCES := $(PORT_CPP) $(PORT_HEADERS)

SHADERS := $(wildcard $(ROOT)/vita/shaders/*/*.glsl)
ICON_DENSITIES := mdpi hdpi xhdpi xxhdpi xxxhdpi
ICON_SIZE_mdpi := 48
ICON_SIZE_hdpi := 72
ICON_SIZE_xhdpi := 96
ICON_SIZE_xxhdpi := 144
ICON_SIZE_xxxhdpi := 192
RES := $(foreach d,$(ICON_DENSITIES),$(BUILD)/res/mipmap-$(d)/ic_launcher.png)
STALE_RES := $(BUILD)/res/mipmap
SO_FILES := $(foreach abi,$(ABIS),$(BUILD)/$(abi)/libmain.so)
# A store build (F-Droid) takes the unsigned APK and signs it with its own key.
ALIGNED := $(BUILD)/app-aligned.apk
UNSIGNED := $(BUILD)/ElectroBench-$(VERSION_NAME)-unsigned.apk

.PHONY: all clean check verify doctor apk unsigned host-render host-png host-shaders FORCE
all: $(APK)

# The project is commonly distributed as a ZIP, which may normalize mtimes.
# Force APK repacking so the embedded shaders can never silently lag the tree.
FORCE:

# ------------------------------------------- host OpenGL ES 3 render check
# The APK cannot be run here: no device, no emulator, no KVM. So the scenes and
# the shader tree are instead rendered through the host's own OpenGL ES 3 driver
# (Mesa, software rasteriser) by the small harness in android/host/ - the same
# scene translation units and the same shaders the APK ships, at the ES 3.0 API
# level it targets. It says nothing about the Android activity/EGL glue, which
# only a device can exercise, and it is where the listing's screenshots come
# from (host-png below).
#
# -DELECTROBENCH_VITA: the scenes' Vita branch is the one that calls the port's
# VitaLog/PortShotTrace hooks, which android/host/host_main.cxx provides for the
# host. An empty asset prefix makes the scenes' "shaders/..." lookups read the
# checkout instead of the console's app0: mount, which is why both targets below
# run from the directory that holds the shader tree.
HOST_CXX ?= g++
HOST_INCLUDES := -I$(ROOT)/android/include -I$(ROOT)/vita/include -I$(ROOT)/vita/lib
HOST_CFLAGS := -std=c++17 -O2 -pthread $(HOST_INCLUDES) \
               -DELECTROBENCH_VITA -DELECTROBENCH_ASSET_PREFIX='""'
HOST_CWD := $(ROOT)/vita
HOST_SRCS := $(ROOT)/android/host/host_main.cxx $(ROOT)/android/host/host_sdl.cxx \
             $(ROOT)/vita/src/jobs.cxx $(ROOT)/vita/src/scene1.cxx $(ROOT)/vita/src/scene2.cxx
HOST_BIN := $(BUILD)/host/host_bench
# The moment each scene is captured at, in benchmark seconds.
#   * Scene 1 counts real seconds, so its capture waits out one full orbit lap
#     (26 s: forward, turn left, restore) and shoots the reference pose.
#   * Scene 2 counts simulated seconds and advances them by at most 0.1 per
#     frame, so anything but the opening frame would need hundreds of frames on
#     a software rasteriser. 0.0 is the pose it starts the benchmark on.
HOST_SHOT_SECONDS_S1 ?= 26.0
HOST_SHOT_SECONDS_S2 ?= 0.0
HOST_SHOT_ENV := LIBGL_ALWAYS_SOFTWARE=1 ELECTROBENCH_HOST_SIZE=1366x768

$(HOST_BIN): $(HOST_SRCS) $(SHADERS)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_CFLAGS) -o $@ $(HOST_SRCS) -lEGL -lGLESv2

host-render: $(HOST_BIN)
	@for scene in 1 2; do \
	    pp="$(BUILD)/host/scene$$scene.ppm"; \
	    case $$scene in \
	        1) secs=$(HOST_SHOT_SECONDS_S1);; \
	        *) secs=$(HOST_SHOT_SECONDS_S2);; \
	    esac; \
	    echo "--- scene $$scene at t=$$secs"; \
	    if command -v xvfb-run >/dev/null 2>&1; then \
	        cd $(HOST_CWD) && $(HOST_SHOT_ENV) xvfb-run -a -s "-screen 0 1366x768x24" \
	            $(HOST_BIN) $$scene $$pp $$secs; \
	    else \
	        cd $(HOST_CWD) && $(HOST_SHOT_ENV) $(HOST_BIN) $$scene $$pp $$secs; \
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
	    cd $(HOST_CWD) && LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 640x480x24" \
	        $(HOST_BIN) --check-shaders; \
	else \
	    cd $(HOST_CWD) && LIBGL_ALWAYS_SOFTWARE=1 $(HOST_BIN) --check-shaders; \
	fi

# The two phone screenshots the store listing shows: scene 1 is the first one,
# scene 2 the second. Frames are rendered anew first, so the listing can never
# lag what the scenes actually draw.
SCREENSHOT_DIR := $(ROOT)/fastlane/metadata/android/en-US/images/phoneScreenshots
host-png: host-render
	@mkdir -p $(SCREENSHOT_DIR)
	python3 $(ROOT)/buildtools/ppm_to_png.py $(BUILD)/host/scene1.ppm $(SCREENSHOT_DIR)/1.png
	python3 $(ROOT)/buildtools/ppm_to_png.py $(BUILD)/host/scene2.ppm $(SCREENSHOT_DIR)/2.png
	@ls -l $(SCREENSHOT_DIR)/*.png


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

$(BUILD)/res/mipmap-%/ic_launcher.png: $(ROOT)/android/make_android_icon.py $(ROOT)/buildtools/make_icons.py
	@mkdir -p $(dir $@)
	@rm -rf $(STALE_RES)
	python3 $< $@ --size $(ICON_SIZE_$*)

# ----------------------------------------------------------------------- debug
# A throwaway signing identity. 
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
# Staging, aapt2 and zipalign in one rule; signing is separate so the store path
# can stop before it.
$(ALIGNED): FORCE $(SO_FILES) $(RES) $(SHADERS) $(ROOT)/android/AndroidManifest.xml
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
	    --version-code $(VERSION_CODE) --version-name $(VERSION_NAME) $(BUILD)/res.zip
	cd $(STAGE) && zip -q -0 -r -X $(BUILD)/app-unsigned.apk lib
	$(ZIPALIGN) -p -f 4 $(BUILD)/app-unsigned.apk $@
	@echo "aligned APK: $@ ($$(stat -c%s $@) bytes)"

unsigned: $(UNSIGNED)
$(UNSIGNED): $(ALIGNED)
	@cp $< $@
	@echo "unsigned APK: $@ ($$(stat -c%s $@) bytes)"

$(APK): $(ALIGNED) $(KEYSTORE)
	$(APKSIGNER) sign --ks $(KEYSTORE) --ks-pass pass:android --key-pass pass:android \
	    --ks-key-alias androiddebugkey --min-sdk-version $(MIN_SDK) \
	    --v4-signing-enabled false --out $@ $(ALIGNED)
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
