# ElectroBench cross-port audit status

## Findings

1. `ElectroBench-android.apk` shipped an older `assets/shaders/ps14/sea_frag.glsl`
   (22,008 bytes) than the current source shader. The source carries the later
   horizon-continuity and distance-faded foam corrections; the packaged APK did
   not. This was a confirmed stale-release artifact.
2. All 13 shader files inside `ElectroBench-vita.vpk` were byte-identical to the
   current `shaders/` source tree.
3. Host-side PSP math, wave packing, cloud sampling and baked-shading checks
   pass in this environment.

## Changes in this audit

- Added `buildtools/check_artifacts.py`, a content-based checker for the shader
  files embedded in the APK and VPK. It detects stale archive contents even when
  file timestamps are equal.
- Added `buildtools/invalidate_stale_package.py` and guarded Makefile hooks:
  when the relevant platform packaging toolchain is present, a package with
  stale embedded shaders is invalidated so Make rebuilds it instead of trusting
  equal timestamps. The hook does not delete artifacts when the toolchain is
  missing.
- Added `check-artifacts` targets to the root and Android Makefiles.
- Made the APK target phony-forced (`FORCE`) so a ZIP-normalised checkout can
  never leave the archive's shaders behind the tree.
- Corrected the documented Android toolchain bootstrap path
  (`android/fetch_toolchain.sh`).

## Verification performed

- `buildtools/test_psp_math.cxx`: PASS.
- `buildtools/test_psp_skycloud.cxx`: PASS.
- `buildtools/test_psp_shadetail.cxx`: PASS (carriageway 141,129,125, spread 16,
  luma 0.52).
- `make check` and `make -f android/Makefile check`: PASS.
- `make -f android/Makefile host-shaders`: 8 linked, 0 failures.
- `make check-artifacts`: PASS, 13/13 shaders in both the VPK and the APK.
- `buildtools/check_codegen.sh`: PASS (PSP VFPU 17 quad instructions; Vita NEON
  358 and 1213 instructions).
- Archives repacked from this tree: `ElectroBench-vita.vpk` (886,021 bytes, clean
  Vita rebuild), `ElectroBench-android.apk` (3,463,132 bytes, three ABIs,
  `apksigner verify` OK) and `psp/EBOOT.PBP` rebuilt with pspdev.
