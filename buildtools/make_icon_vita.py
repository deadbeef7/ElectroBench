#!/usr/bin/env python3
"""Vita LiveArea icon (128x128, palettised) -- see buildtools/make_icons.py.

Kept as its own file because the VPK rule names it (and the icon is a VITA
requirement: 128x128, 8-bit palette, or the package promoter rejects the
install with 0x8010113D). The drawing itself is shared with the PSP and
Android builds so the three platforms cannot ship three different logos.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_icons  # noqa: E402

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "icon0.png"
    rgba = make_icons.render(128, 128, wide=False)
    n, colours = make_icons.write_indexed(out, 128, 128, rgba)
    print("wrote %s (128x128, %d bytes, %d-colour palette)" % (out, n, colours))
