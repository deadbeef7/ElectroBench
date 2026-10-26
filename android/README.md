# ElectroBench for Android — packaging notes

This folder builds the Android port: scenes 1 and 2 from the ElectroBench
renderer, compiled against OpenGL ES 3 as a `NativeActivity` app. There is no
Java source, no `classes.dex` and no Gradle project — `make` drives the NDK
compiler, `aapt2` and `apksigner` directly, and the manifest declares
`android:hasCode="false"`.

Everything needed to build is in this repository; the build downloads nothing.
The only external inputs are the Google Android NDK / SDK packages, pinned
below.

## Toolchain

| package        | pinned version         |
| -------------- | ---------------------- |
| NDK            | 26.3.11579264 (`r26d`) |
| build-tools    | 34.0.0                 |
| platform jar   | android-34             |
| min / target   | 24 / 34                |
| ABIs           | `arm64-v8a`, `armeabi-v7a`, `x86_64` |

`sh android/fetch_toolchain.sh` downloads them into
`build-android/toolchain/` for local work. If you already have an SDK, point
the Makefile at it:

```sh
make -C android \
     ANDROID_NDK_HOME=/opt/android/ndk \
     ANDROID_SDK_ROOT=/opt/android/sdk
```

`NDK=`/`SDK=` on the command line and the `ANDROID_NDK`/`ANDROID_HOME` names
that F-Droid's build server exports are both accepted; `make -C android doctor`
prints which paths and binaries were resolved.

## Targets

| target     | output                                          | use                     |
| ---------- | ----------------------------------------------- | ----------------------- |
| `all`,`apk`| `ElectroBench-android.apk` (repo root, signed)   | sideloading by hand     |
| `unsigned` | `build-android/ElectroBench-<version>-unsigned.apk` | store submission    |
| `doctor`   | resolved toolchain paths                         | build problems          |

Both targets run the same staging → `aapt2 link` → `zipalign` step; the signed
target then signs the result with a throwaway debug key
(`build-android/debug.keystore`). The unsigned target stops before signing
because a store (F-Droid) signs the APK with its own key.

## Versioning

`VERSION_CODE` and `VERSION_NAME` at the top of `android/Makefile` are the only
place the version is written. A release bumps them together with:

1. a new `fastlane/metadata/android/en-US/changelogs/<versionCode>.txt`,
2. the `Builds:` entry in `fdroid/metadata/org.deadbeef7.electrobench.yml`,
3. a git tag on the release commit.

`versionCode` must increase for every published build; F-Droid orders updates
by it and ignores `versionName` when deciding which APK is newer.

## Cutting a release

```sh
# 1. bump VERSION_CODE / VERSION_NAME in android/Makefile, then
# 2. add fastlane/metadata/android/en-US/changelogs/<versionCode>.txt
# 3. build and check the artifact the store will take
make -C android unsigned && \
  aapt2 dump badging build-android/ElectroBench-*-unsigned.apk | head -1
# 4. commit, tag (v<version>), push both the branch and the tag
# 5. copy fdroid/metadata/org.deadbeef7.electrobench.yml into your fdroiddata
#    fork as metadata/org.deadbeef7.electrobench.yml, add the Builds entry for
#    the new version, and set its `commit:` to `git rev-parse v<version>`.
```

The `commit:` in the in-repo copy names the release commit it describes, which
is necessarily one commit older than the copy itself: a commit cannot contain
its own hash. The copy in fdroiddata is the one F-Droid builds from, so it is
the copy that has to be right; the in-repo one is kept so the recipe is
versioned next to the code it builds.

Pushing the tag is not optional. The recipe reads the version out of
`android/Makefile` at each tag (`UpdateCheckData`) because no tag here carries
`android/AndroidManifest.xml` - the Android port is newer than every tag - so
`fdroid checkupdates` can only see a release once a tag containing `android/`
exists. Before that it fails with "Couldn't find any version information".
`AutoUpdateMode: Version` then appends the next Builds entry by itself.

## F-Droid

The recipe lives in `fdroid/metadata/org.deadbeef7.electrobench.yml` and is
ready to be copied into [fdroiddata](https://gitlab.com/fdroid/fdroiddata) as
`metadata/org.deadbeef7.electrobench.yml` (fork → commit → merge request).
fdroiddata is the source of truth for the build recipe; this copy is kept in
the repository so the recipe is versioned next to the code it builds.

What the recipe relies on, and what this folder guarantees for it:

* **No network at build time.** `make -C android unsigned` only compiles the
  checked-in sources; the SDK/NDK come from the build server via
  `ndk: 26.3.11579264` and the `$$SDK$$` / `$$NDK$$` substitutions.
* **A single unsigned APK**, at `build-android/ElectroBench-*-unsigned.apk`
  (`output:` glob). Nothing else matching `*.apk` is produced by that target,
  and the tracked sideload APK is deleted by F-Droid's scanner before the
  build runs.
* **A free license**: GPL-3.0-only (`/LICENSE`), with no proprietary
  dependencies — the only bundled code is lodepng (zlib license) and the
  scene/renderer sources in this repository.
* **No permissions** in the manifest: no network, no storage granted by the
  system, no ads and no analytics. There are no anti-features to declare.
* **Listing metadata** from `fastlane/metadata/android/en-US/`: title, short
  and full description, icon, two phone screenshots and per-version
  changelogs. `short_description.txt` is capped at 80 characters by F-Droid
  (it is the app's summary in the client), the full description at 4000, and
  the icon must stay 512×512. Images are PNGs from the port's own GLES3 render
  path (1366×768): `make -C android host-png` re-renders both from the current
  source through the host driver, so the listing cannot lag the scenes.
  Replace them with device captures whenever newer ones exist, because
  F-Droid reads them out of the repository at the commit it builds.

### Checking the recipe locally

`fdroidserver` is the only extra tool the recipe needs; `aapt2`, `apksigner` and
`zipalign` come from the SDK build-tools that `make` already uses. Install it
into a throwaway venv under the gitignored `build-android/`:

```sh
python3 -m venv build-android/fdroid-venv
build-android/fdroid-venv/bin/pip install fdroidserver
mkdir -p build-android/fdroid-check/metadata build-android/fdroid-check/config
cp fdroid/metadata/*.yml build-android/fdroid-check/metadata/

# fdroid lint validates Categories against the taxonomy in fdroiddata's
# config/categories.yml, not against a list built into fdroidserver, so it
# reports every category as invalid when that file is missing. Take the real
# one and drop the `icon:` entries it carries (the PNGs live in fdroiddata).
curl -fsSL -o build-android/fdroid-check/config/categories.yml \
  https://gitlab.com/fdroid/fdroiddata/-/raw/master/config/categories.yml
cd build-android/fdroid-check
../../build-android/fdroid-venv/bin/python - <<'PY'
import yaml
p = 'config/categories.yml'
d = yaml.safe_load(open(p))
for v in d.values():
    v.pop('icon', None)
yaml.safe_dump(d, open(p, 'w'), sort_keys=True)
PY
# fdroidserver only scans build-tools directories with a numeric name, so the
# `android-14` alias this SDK uses for 34.0.0 is invisible to it; hand it
# apksigner on PATH instead (the same quirk the Makefile's BUILD_TOOLS works
# around).
export PATH="/opt/android/sdk/build-tools/android-14:$PATH"
../../build-android/fdroid-venv/bin/fdroid readmeta
../../build-android/fdroid-venv/bin/fdroid lint
cd - >/dev/null
```

Both commands are silent on success. `readmeta` parses the YAML the way the
build server does; `lint` is what fdroiddata's CI runs on the merge request.
`fdroid build` itself is not runnable here — it needs the F-Droid build server
VM — so the compile step is still `make -C android unsigned`, and the artifact
it produces is checked with `make -C android doctor` plus
`aapt2 dump badging` / `apksigner verify`.

F-Droid builds the APK from source and signs it with its own key, so the
artifact published by F-Droid is not byte-identical to `ElectroBench-android.apk`
in the repository root. Reproducible-build verification is not set up.

## Runtime notes

The app unpacks `assets/shaders/**` into its files directory on first launch
and runs one scene after the other, 45 s each. Results, frame captures and the
port log are written under
`/sdcard/Android/data/org.deadbeef7.electrobench/files/`; an optional
`electrobench.txt` there selects which scenes run (see the header comment in
`jni/android_main.cxx`).
