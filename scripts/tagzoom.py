"""Print a per-material class map for a crop of the tag render.

Legend: . sky   R roof   W wall   M metal   C cable   c ceramic   o other
"""
import sys
import numpy as np
from PIL import Image

CH = {0: "M", 1: "R", 2: "W", 3: "C", 4: "c", 5: "o", 6: "."}


def classify(img):
    q = (img // 60).astype(np.int32)
    out = np.full(q.shape[:2], 6, dtype=np.int32)
    for col, i in (((0, 255, 0), 0), ((255, 0, 0), 1), ((0, 0, 255), 2),
                   ((255, 0, 255), 3), ((0, 255, 255), 4)):
        r, g, b = col
        out[(q[:, :, 0] == r // 60) & (q[:, :, 1] == g // 60) &
            (q[:, :, 2] == b // 60)] = i
    out[(q[:, :, 0] == 0) & (q[:, :, 1] == 0) & (q[:, :, 2] == 0)] = 5
    return out


def main():
    img = np.asarray(Image.open(sys.argv[1]).convert("RGB")).astype(np.int32)
    cls = classify(img)
    x0, x1, y0, y1 = (int(v) for v in sys.argv[2:6])
    print("     " + "".join(str((x0 + i) // 10 % 10) for i in range(x1 - x0)))
    print("     " + "".join(str((x0 + i) % 10) for i in range(x1 - x0)))
    for y in range(y0, y1):
        print("%4d %s" % (y, "".join(CH[int(v)] for v in cls[y, x0:x1])))


main()