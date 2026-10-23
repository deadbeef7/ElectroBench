#!/usr/bin/env python3
"""Remove a packaged app only when its embedded shaders differ from source.

This is called by Make at parse time *only when the platform toolchain exists*.
It works around equal checkout mtimes without forcing expensive full rebuilds on
normal `make` invocations. It intentionally does not touch an artifact if the
archive cannot be read; `check_artifacts.py` reports that condition separately.
"""
from __future__ import annotations

import sys
import zipfile
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: invalidate_stale_package.py <archive> <asset-prefix>", file=sys.stderr)
        return 2
    archive_path = Path(sys.argv[1])
    prefix = sys.argv[2]
    root = Path(__file__).resolve().parents[1]
    shaders = sorted((root / "shaders").glob("*/*.glsl"))
    if not archive_path.is_file() or not shaders:
        return 0
    try:
        with zipfile.ZipFile(archive_path) as zf:
            names = set(zf.namelist())
            for shader in shaders:
                packed = prefix + shader.relative_to(root).as_posix()
                if packed not in names or zf.read(packed) != shader.read_bytes():
                    archive_path.unlink()
                    print(f"stale shader package removed; rebuilding {archive_path.name}", file=sys.stderr)
                    return 0
    except (OSError, zipfile.BadZipFile, KeyError):
        # Do not delete an unreadable archive automatically.
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())

