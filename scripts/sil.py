"""Silhouette 'look': sky vs object, 2x4 pixels per character, side by side.

Chafa cannot resolve a 2 px mast, but a binary silhouette at 2x4 px per
character can: the mast becomes a 1-char column and its foot lands on the roof
edge visibly. Two renders of the same frame are printed next to each other so
before/after is one glance.
"""
import sys
import numpy as np
from PIL import Image


def load(p):
    return np.asarray(Image.open(p).convert("RGB")).astype(np.float64)


def main():
    x0, x1, y0, y1 = (int(v) for v in sys.argv[1:5])
    paths = sys.argv[5:]
    cols = (x1 - x0) // 2
    rows = (y1 - y0) // 4
    print("sky=white object=black  region x%d-%d y%d-%d  (%dx%d chars per panel)"
          % (x0, x1, y0, y1, cols, rows))
    thr = None
    for p in paths:
        lum = load(p)[y0:y1, x0:x1] @ np.array([0.299, 0.587, 0.114])
        t = 0.5 * (np.percentile(lum, 2) + np.percentile(lum, 98))
        thr = t if thr is None else max(thr, t)
    print("threshold %.1f (region sky/object split)" % thr)
    for j in range(rows):
        line = []
        for p in paths:
            lum = load(p)[y0:y1, x0:x1] @ np.array([0.299, 0.587, 0.114])
            blk = lum[j * 4:j * 4 + 4, ::2]
            sky = blk.mean(axis=0) > thr
            line.append("".join(" " if s else "#" for s in sky))
        print(" | ".join(line))
    print("^ " + " | ".join(" " * (cols - 12) + p.split("/")[-1] for p in paths))


main()