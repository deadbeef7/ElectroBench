"""Isolated sky fragments above the horizon: narrow, sky-backed, sky-sided.

The general 'sky underneath' scan is dominated by ground-level clutter, so this
narrows to what the artifact actually is: a thin vertical fragment in open sky,
well above the horizon, with sky on BOTH sides within a few pixels and several
rows of sky directly beneath its foot. Anchored hardware fails the last test --
its foot bottoms out on the roof, so the pixels under it are roof.
"""
import sys
import numpy as np
from PIL import Image


def load(path):
    return np.asarray(Image.open(path).convert("RGB")).astype(np.float64)


def main():
    thr, ytop, ybot, side = (float(sys.argv[1]), int(sys.argv[2]),
                             int(sys.argv[3]), int(sys.argv[4]))
    for path in sys.argv[5:]:
        img = load(path)
        lum = img @ np.array([0.299, 0.587, 0.114])
        obj = lum <= thr
        h, w = obj.shape
        rows = []
        for x in range(side, w - side):
            col = obj[:, x]
            y = ytop
            while y < ybot:
                if not col[y]:
                    y += 1
                    continue
                k = y
                while k < ybot and col[k]:
                    k += 1
                hh = k - y
                if hh >= 15:
                    gap = 0
                    m = k
                    while m < ybot and not col[m] and gap < 5:
                        gap += 1
                        m += 1
                    left = all(not obj[yy, x - 1 - d] for d in range(side)
                               for yy in (y, y + hh // 2, k - 1))
                    right = all(not obj[yy, x + 1 + d] for d in range(side)
                                for yy in (y, y + hh // 2, k - 1))
                    if gap >= 4 and left and right:
                        rows.append((x, y, k - 1, hh))
                y = k
        # group columns whose runs overlap vertically
        rows.sort()
        groups = []
        for r in rows:
            placed = False
            for g in groups:
                if (min(g[1], r[1]) <= max(g[2], r[2]) and
                        r[0] - g[3] <= 3):
                    g[0] = min(g[0], r[0])
                    g[1] = min(g[1], r[1])
                    g[2] = max(g[2], r[2])
                    g[3] = r[0]
                    placed = True
                    break
            if not placed:
                groups.append([r[0], r[1], r[2], r[0]])
        print("\n%s: %d isolated sky fragments in y%d-%d"
              % (path, len(groups), ytop, ybot))
        for a, y0, y1, b in sorted(groups, key=lambda g: -((g[3] - g[0] + 1) *
                                                            (g[2] - g[1] + 1)))[:18]:
            print("   x%d-%d (%d wide)  y%d-%d (%d tall)  %d px"
                  % (a, b, b - a + 1, y0, y1, y1 - y0 + 1,
                     (b - a + 1) * (y1 - y0 + 1)))


main()