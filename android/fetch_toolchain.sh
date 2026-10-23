#!/bin/sh
# ===========================================================================
# Fetch the three Google packages android/Makefile builds against, and lay them
# out the way it expects:
#
#   PREFIX/ndk/android-ndk-r26d          (NDK, ~2.1 GB unpacked)
#   PREFIX/sdk/build-tools/34.0.0        (aapt2, zipalign, apksigner, d8)
#   PREFIX/sdk/platforms/android-34      (android.jar, for aapt2 link)
#
# Usage:  sh android/fetch_toolchain.sh [PREFIX]
#         PREFIX defaults to ./build-android/toolchain
#
# Then build with:
#   make -f android/Makefile \
#        ANDROID_NDK_HOME=$PREFIX/ndk/android-ndk-r26d \
#        ANDROID_SDK_ROOT=$PREFIX/sdk
#
# The versions are pinned because the APK in this repository was produced with
# exactly them: NDK r26d (clang 17), build-tools 34.0.0, platform android-34.
# A JDK (17 or newer) must be on PATH for apksigner and keytool; it is not
# downloaded here.
# ===========================================================================
set -eu

PREFIX="${1:-$PWD/build-android/toolchain}"
DL="$PREFIX/dl"
NDK_VERSION=r26d
BUILD_TOOLS_VERSION=34.0.0
PLATFORM=android-34

REPO=https://dl.google.com/android/repository
NDK_ZIP=android-ndk-$NDK_VERSION-linux.zip
BT_ZIP=build-tools_r34-linux.zip
PLATFORM_ZIP=platform-34-ext7_r03.zip

mkdir -p "$DL" "$PREFIX/sdk/build-tools" "$PREFIX/sdk/platforms"

fetch() {
  url="$REPO/$1"
  out="$DL/$1"
  if [ -s "$out" ]; then
    echo "have $1"
  else
    echo "fetching $1"
    curl -fL --retry 3 -o "$out.part" "$url"
    mv "$out.part" "$out"
  fi
}

fetch "$NDK_ZIP"
fetch "$BT_ZIP"
fetch "$PLATFORM_ZIP"

if [ ! -d "$PREFIX/ndk/android-ndk-$NDK_VERSION" ]; then
  echo "unpacking $NDK_ZIP (this is the slow one)"
  mkdir -p "$PREFIX/ndk"
  unzip -q "$DL/$NDK_ZIP" -d "$PREFIX/ndk"
fi

# build-tools archives unpack into a directory named after the platform
# codename (android-14 for r34), so they are renamed to the version the Makefile
# and every SDK layout expect.
if [ ! -d "$PREFIX/sdk/build-tools/$BUILD_TOOLS_VERSION" ]; then
  tmp="$PREFIX/sdk/build-tools/.unpack"
  rm -rf "$tmp"
  mkdir -p "$tmp"
  unzip -q "$DL/$BT_ZIP" -d "$tmp"
  mv "$tmp"/* "$PREFIX/sdk/build-tools/$BUILD_TOOLS_VERSION"
  rm -rf "$tmp"
fi

if [ ! -f "$PREFIX/sdk/platforms/$PLATFORM/android.jar" ]; then
  tmp="$PREFIX/sdk/platforms/.unpack"
  rm -rf "$tmp"
  mkdir -p "$tmp"
  unzip -q "$DL/$PLATFORM_ZIP" -d "$tmp"
  mv "$tmp/$PLATFORM" "$PREFIX/sdk/platforms/$PLATFORM" 2>/dev/null || mv "$tmp"/* "$PREFIX/sdk/platforms/"
  rm -rf "$tmp"
fi

echo
echo "ready. Build with:"
echo "  make -f android/Makefile \\"
echo "       ANDROID_NDK_HOME=$PREFIX/ndk/android-ndk-$NDK_VERSION \\"
echo "       ANDROID_SDK_ROOT=$PREFIX/sdk"
