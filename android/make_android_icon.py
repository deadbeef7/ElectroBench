#!/usr/bin/env python3
"""Android launcher icon (RGBA PNG at a requested density) -- see
buildtools/make_icons.py.

The launcher picks a bitmap by density, so the Makefile runs this once per
mipmap directory (48/72/96/144/192) instead of shipping one 256x256 image that
every device then rescales. The drawing is the shared four-colour mark.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "buildtools"))
import make_icons  # noqa: E402

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "ic_launcher.png"
    size = 256
    if "--size" in sys.argv:
        size = int(sys.argv[sys.argv.index("--size") + 1])
    rgba = make_icons.render(size, size, wide=False)
    n = make_icons.write_rgba(out, size, size, rgba)
    print("wrote %s (%dx%d, %d bytes, RGBA)" % (out, size, size, n))
