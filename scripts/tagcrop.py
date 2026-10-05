"""Print the material class of every pixel in a crop of a tag render.

One char per pixel, one line per pixel row, so a 90x80 box is readable as text.
The tag render paints each material a flat colour (see object_frag.glsl), so
this is exact -- no threshold to tune and no sky/leaf confusion.

Usage: tagcrop.py IMG X0 Y0 X1 Y1 [xstep ystep]
"""
import sys

import numpy as np

from treeline import PAL, NAMES, load, classify

CH = {"SKY": ".", "CABLE": "C", "CERAMIC": "c", "METAL": "M", "LEAF": "L",
      "STEEL": "S", "CONCRETE": "N", "KERB": "K", "WALL": "W", "ROOF": "R",
      "GLASS": "G", "SHADOW": "h", "GLOW": "g", "OTHER": "o"}


def main():
    img = load(sys.argv[1])
    x0, y0, x1, y1 = (int(v) for v in sys.argv[2:6])
    xs = int(sys.argv[6]) if len(sys.argv) > 6 else 1
    ys = int(sys.argv[7]) if len(sys.argv) > 7 else 1
    cls = classify(img)
    print("crop x%d-%d y%d-%d   step %dx%d" % (x0, x1, y0, y1, xs, ys))
    hdr = "".join(str((x // 10) % 10) for x in range(x0, x1 + 1, xs))
    print("     " + hdr)
    hdr = "".join(str(x % 10) for x in range(x0, x1 + 1, xs))
    print("     " + hdr)
    for y in range(y0, y1 + 1, ys):
        row = "".join(CH[NAMES[v]] for v in cls[y, x0:x1 + 1:xs])
        print("%4d %s" % (y, row))


if __name__ == "__main__":
    main()