#!/usr/bin/env python3
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_icons  # noqa: E402

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "icon0.png"
    rgba = make_icons.render(128, 128, wide=False)
    n, colours = make_icons.write_indexed(out, 128, 128, rgba)
    print("wrote %s (128x128, %d bytes, %d-colour palette)" % (out, n, colours))
