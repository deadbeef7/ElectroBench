"""List every sky-free object run in a row band, as bounding boxes.

Used to answer 'what exactly is the 2x54 px dark thing at x1549 y330-383',
and to diff that list between two renders of the same frame.
"""
import sys
import numpy as np
from PIL import Image


def load(path):
    return np.asarray(Image.open(path).convert("RGB")).astype(np.float64)


def runs(path, thr, ytop, ybot, minh):
    lum = load(path) @ np.array([0.299, 0.587, 0.114])
    obj = lum <= thr
    h, w = obj.shape
    per_col = []
    for x in range(w):
        col = obj[ytop:ybot, x]
        y = 0
        colruns = []
        while y < len(col):
            if col[y]:
                k = y
                while k < len(col) and col[k]:
                    k += 1
                if k - y >= minh:
                    colruns.append((y + ytop, k - 1 + ytop))
                y = k
            else:
                y += 1
        per_col.append(colruns)
    boxes = []
    used = [set() for _ in range(w)]
    for x in range(w):
        for i, (a, b) in enumerate(per_col[x]):
            if i in used[x]:
                continue
            # extend right while a run overlaps vertically
            bx0, bx1, by0, by1 = x, x, a, b
            used[x].add(i)
            xx = x + 1
            while xx < w:
                hit = None
                for j, (c, d) in enumerate(per_col[xx]):
                    if j not in used[xx] and min(c, by1) >= max(c, d) - 2 and \
                       min(d, by1) >= max(c, d) - 2:
                        hit = (j, c, d)
                        break
                if hit is None:
                    break
                used[xx].add(hit[0])
                by0, by1 = min(by0, hit[1]), max(by1, hit[2])
                bx1 = xx
                xx += 1
            boxes.append((bx0, bx1, by0, by1))
    return boxes


def main():
    thr, ytop, ybot, minh = (float(sys.argv[1]), int(sys.argv[2]),
                             int(sys.argv[3]), int(sys.argv[4]))
    res = {}
    for p in sys.argv[5:]:
        b = runs(p, thr, ytop, ybot, minh)
        res[p] = b
        print("\n%s: %d runs (height>=%d) in y%d-%d" % (p, len(b), minh, ytop, ybot))
        for bx0, bx1, by0, by1 in b:
            print("   x%4d-%4d (%2d wide)  y%4d-%4d (%2d tall)  %4d px"
                  % (bx0, bx1, bx1 - bx0 + 1, by0, by1, by1 - by0 + 1,
                     (bx1 - bx0 + 1) * (by1 - by0 + 1)))


main()