#!/usr/bin/env python3
"""Check that shipped APK/VPK shader assets match the source tree.

Run from any directory:
    python3 buildtools/check_artifacts.py

This catches a subtle release failure: checked-out files commonly have the same
mtime, so Make may consider a prebuilt APK/VPK up to date even when an embedded
shader is older than the shader in the source tree. It checks archive contents,
not timestamps.
"""
from __future__ import annotations

import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHADERS = sorted((ROOT / "shaders").glob("*/*.glsl"))
ARTIFACTS = (
    (ROOT / "ElectroBench-vita.vpk", ""),
    (ROOT / "ElectroBench-android.apk", "assets/"),
)


def main() -> int:
    failures = 0
    if not SHADERS:
        print("FAIL: no source shaders found")
        return 2

    for artifact, prefix in ARTIFACTS:
        if not artifact.is_file():
            print(f"SKIP: {artifact.name} is not present")
            continue
        print(f"--- {artifact.name}")
        try:
            with zipfile.ZipFile(artifact) as archive:
                names = set(archive.namelist())
                for shader in SHADERS:
                    rel = shader.relative_to(ROOT).as_posix()
                    packed = prefix + rel
                    if packed not in names:
                        print(f"FAIL: missing {packed}")
                        failures += 1
                        continue
                    source_bytes = shader.read_bytes()
                    packed_bytes = archive.read(packed)
                    if source_bytes != packed_bytes:
                        print(f"FAIL: stale shader {packed} (source {len(source_bytes)} B, artifact {len(packed_bytes)} B)")
                        failures += 1
                    else:
                        print(f"OK:   {packed}")
        except (OSError, zipfile.BadZipFile) as exc:
            print(f"FAIL: cannot inspect {artifact.name}: {exc}")
            failures += 1

    if failures:
        print(f"ARTIFACT_SHADER_CHECK_FAIL: {failures} problem(s)")
        print("Rebuild the affected package with its platform toolchain, then rerun this check.")
        return 1
    print("ARTIFACT_SHADER_CHECK_OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())

