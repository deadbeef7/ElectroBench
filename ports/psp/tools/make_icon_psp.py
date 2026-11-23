#!/usr/bin/env python3
"""PSP ICON0 (144x80, palettised) -- see buildtools/make_icons.py.

The PSP draws this in the XMB's game column, so it is a wide crop of the same
mark the Vita and Android builds use rather than a second drawing. 8-bit
palette PNG is what every other PBP icon uses and what pspsdk's pack-pbp
expects for ICON0.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "buildtools"))
import make_icons  # noqa: E402

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "ICON0.PNG"
    rgba = make_icons.render(144, 80, wide=True)
    n, colours = make_icons.write_indexed(out, 144, 80, rgba)
    print("wrote %s (144x80, %d bytes, %d-colour palette)" % (out, n, colours))
