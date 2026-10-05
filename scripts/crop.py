#!/usr/bin/env python3
"""Print a native-resolution ASCII map of a crop, '#' where the pixel is
darker than the local sky (geometry), with 2-level shading by darkness."""
import sys
import numpy as np
from PIL import Image

path, x0, y0, w, h = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
a = np.asarray(Image.open(path).convert("RGB")).astype(np.int16)
# local sky baseline: median of a wide band at the same row
base = np.median(a, axis=1, keepdims=True)
d = (base - a).sum(axis=2)
crop = d[y0:y0+h, x0:x0+w]
rows = np.median(base[y0:y0+h], axis=1, keepdims=True)
d2 = (rows - a[y0:y0+h, x0:x0+w]).sum(axis=2)
print(f"crop {path} x{x0}..{x0+w} y{y0}..{y0+h}")
hdr = "     " + "".join(str((x0+c)//10 % 10) for c in range(w))
hdr2 = "     " + "".join(str((x0+c) % 10) for c in range(w))
print(hdr); print(hdr2)
for r in range(h):
    line = ""
    for c in range(w):
        v = d2[r, c]
        line += "#" if v > 170 else ("+" if v > 90 else ("-" if v > 45 else ("." if v > 15 else " ")))
    print(f"{y0+r:4d} |{line}|")
