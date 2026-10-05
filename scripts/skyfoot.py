"""Global scan for unanchored fragments: object runs with SKY underneath them.

For every column of the frame this walks down and finds maximal sky-free runs.
A run whose bottom edge is followed by several rows of SKY is a fragment whose
support is not visible -- the exact signature of a mast whose foot is occluded
or floating. Runs that bottom out on other geometry (a roof, a wall, the ground)
are anchored and are excluded.
"""
import sys
import numpy as np
from PIL import Image


def load(path):
    return np.asarray(Image.open(path).convert("RGB")).astype(np.float64)


def scan(img, thr, min_h, sky_under):
    lum = img @ np.array([0.299, 0.587, 0.114])
    obj = lum <= thr
    h, w = obj.shape
    frags = []
    for x in range(w):
        col = obj[:, x]
        y = 0
        while y < h:
            if col[y]:
                k = y
                while k < h and col[k]:
                    k += 1
                hh = k - y
                below_sky = 0
                m = k
                while m < h and not col[m] and below_sky < 6:
                    below_sky += 1
                    m += 1
                if (min_h <= hh <= 90 and y > 0 and below_sky >= sky_under
                        and m < h and not col[k]):
                    frags.append((x, y, k - 1, hh))
                y = k
            else:
                y += 1
    return frags


def main():
    thr = float(sys.argv[1])
    for path in sys.argv[2:]:
        img = load(path)
        frags = scan(img, thr, 15, 4)
        # group adjacent columns with the same top/bottom into blobs
        blobs = {}
        for (x, y0, y1, hh) in frags:
            blobs.setdefault((y0, y1), []).append(x)
        out = []
        for (y0, y1), xs in sorted(blobs.items()):
            xs = sorted(xs)
            run = [xs[0]]
            for v in xs[1:]:
                if v - run[-1] <= 2:
                    run.append(v)
                else:
                    out.append((run[0], run[-1], y0, y1))
                    run = [v]
            out.append((run[0], run[-1], y0, y1))
        px = sum((b - a + 1) * (y1 - y0 + 1) for a, b, y0, y1 in out)
        print("\n%s  %d fragments, %d blobs, %d px"
              % (path, len(frags), len(out), px))
        for a, b, y0, y1 in sorted(out, key=lambda t: -(t[1] - t[0]) * (t[3] - t[2])):
            print("   x%d-%d (%d wide)  y%d-%d (%d tall)"
                  % (a, b, b - a + 1, y0, y1, y1 - y0 + 1))


main()