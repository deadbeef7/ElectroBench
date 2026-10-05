"""Is the TV aerial's foot standing on the roof, or hanging over the sky?

Sky here sits at luma ~172 and every object surface at ~92, so an absolute
midpoint threshold separates them cleanly -- but only 80 levels apart, which is
exactly why the threshold sweeps kept reporting "no artefact" on frames that
visibly had one. Classification is global, not per-row percentiles (a
percentile split classifies a uniform sky row as object). The decisive output
is: for the shaft column, the row where the object run ENDS and whether sky
comes back underneath it.
"""
import sys
import numpy as np
from PIL import Image


def load(path):
    return np.asarray(Image.open(path).convert("RGB")).astype(np.float64)


def main():
    new_p, old_p = sys.argv[1], sys.argv[2]
    x0, x1, y0, y1 = (int(v) for v in sys.argv[3:7])
    imgs = [("OLD", load(old_p)), ("NEW", load(new_p))]
    maps = {}
    lo_all, hi_all = [], []
    for _, img in imgs:
        sub = img[y0:y1, x0:x1]
        l = sub @ np.array([0.299, 0.587, 0.114])
        lo_all.append(np.percentile(l, 2))
        hi_all.append(np.percentile(l, 98))
    thr = 0.5 * (max(lo_all) + min(hi_all))
    print("sky/object luma split: object<=%.0f  sky>=%.0f  threshold %.1f"
          % (max(lo_all), min(hi_all), thr))
    for name, img in imgs:
        sub = img[y0:y1, x0:x1]
        lum = sub @ np.array([0.299, 0.587, 0.114])
        maps[name] = lum > thr

    hdr = "     " + "".join(str((x0 + i) // 10 % 10) for i in range(x1 - x0))
    hdr2 = "     " + "".join(str((x0 + i) % 10) for i in range(x1 - x0))
    for name, sky in maps.items():
        print("\n%s  sky='#' object=' '   x%d-%d y%d-%d" % (name, x0, x1, y0, y1))
        print(hdr)
        print(hdr2)
        for j in range(sky.shape[0]):
            print("%4d %s" % (y0 + j, "".join("#" if o else " " for o in sky[j])))

    # For each image: the sky-free columns in the band, and how far down each
    # one runs before sky reappears underneath it.
    print("\nfoot analysis (per sky-free column, deepest contiguous object run)")
    for name, sky in maps.items():
        cols = []
        for i in range(sky.shape[1]):
            col = sky[:, i]
            j = 0
            deepest = 0
            while j < len(col):
                if col[j]:
                    k = j
                    while k < len(col) and col[k]:
                        k += 1
                    deepest = max(deepest, k - j)
                    j = k
                else:
                    j += 1
            cols.append(deepest)
        arr = np.array(cols)
        cand = [x0 + i for i in range(sky.shape[1])
                if 20 <= arr[i] < sky.shape[0] - 2]
        print("%s columns whose SKYWARD object run is 20-79 px tall: %s" %
              (name, cand))


main()