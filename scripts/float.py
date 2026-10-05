"""Find floating fragments: thin sky-free objects with SKY underneath.

At the real 1280x720 window size, a "flying cylinder" is a sky-free run whose
BOTTOM EDGE is followed by open sky -- i.e. nothing under it holds it up. Runs
that bottom out on a roof, a wall or the ground are anchored and are excluded.

For every column: walk down, take maximal sky-free runs, and keep the ones that
are laterally isolated (sky within `side` px either side) and have `gap` rows
of sky directly beneath. Adjacent columns with overlapping runs merge into one
blob, so a mast plus its crossbars is reported as a single object with a box.

Usage: float.py IMG [IMG ...] -- thr ytop ybot minh gap side
"""
import sys
import numpy as np
from PIL import Image


def load(p):
    with open(p, "rb") as f:
        assert f.readline().strip() == b"P6"
        w, h = map(int, f.readline().split())
        f.readline()
        d = np.frombuffer(f.read(w * h * 3), dtype=np.uint8)
    return d.reshape(h, w, 3)


def main():
    args = sys.argv[1:]
    i = args.index("--")
    paths, prm = args[:i], args[i + 1:]
    thr, ytop, ybot, minh, gap, side = (float(prm[0]), int(prm[1]), int(prm[2]),
                                        int(prm[3]), int(prm[4]), int(prm[5]))
    for p in paths:
        img = load(p).astype(np.float64)
        h, w = img.shape[:2]
        lum = img @ np.array([0.299, 0.587, 0.114])
        sky = lum > thr
        colruns = []
        for x in range(w):
            col = ~sky[:, x]
            y, cr = ytop, []
            while y < ybot:
                if col[y]:
                    k = y
                    while k < ybot and col[k]:
                        k += 1
                    cr.append((y, k - 1))
                    y = k
                else:
                    y += 1
            colruns.append(cr)
        keep = []
        for x in range(side, w - side):
            for (a, b) in colruns[x]:
                if b - a + 1 < minh:
                    continue
                m, g = b + 1, 0
                while m < ybot and sky[m, x] and g < gap:
                    g += 1
                    m += 1
                if g < gap:
                    continue
                mid = (a + b) // 2
                ok = True
                for d in range(1, side + 1):
                    if (~sky[[a, mid, b], x - d]).any() or \
                       (~sky[[a, mid, b], x + d]).any():
                        ok = False
                        break
                if ok:
                    keep.append((x, a, b))
        # merge columns whose runs overlap vertically
        boxes = []
        for (x, a, b) in keep:
            hit = None
            for i2, bx in enumerate(boxes):
                if bx[3] >= x - 1 and min(bx[1], a) <= max(bx[2], b) + 2 and \
                   abs(bx[1] - a) <= 12 and abs(bx[2] - b) <= 12:
                    hit = i2
                    break
            if hit is None:
                boxes.append([x, a, b, x])
            else:
                bx = boxes[hit]
                bx[0], bx[3] = min(bx[0], x), max(bx[3], x)
                bx[1], bx[2] = min(bx[1], a), max(bx[2], b)
        print("\n%s  %dx%d  %d floating fragments (min h%d, gap %d, side %d)"
              % (p, w, h, len(boxes), minh, gap, side))
        tot = 0
        for bx0, by0, by1, bx1 in sorted(boxes, key=lambda b: -(b[3] - b[0] + 1) *
                                         (b[2] - b[1] + 1)):
            px = (bx1 - bx0 + 1) * (by1 - by0 + 1)
            tot += px
            print("   x%4d-%4d (%2d w)  y%4d-%4d (%3d tall)  %4d px"
                  % (bx0, bx1, bx1 - bx0 + 1, by0, by1, by1 - by0 + 1, px))
        print("   total %d px" % tot)


main()