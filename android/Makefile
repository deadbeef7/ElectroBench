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
VERSION_NAME ?= 1.0

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
            $(ROOT)/vita/src/scene2.cxx \
            $(ROOT)/vita/src/scene4.cxx
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
