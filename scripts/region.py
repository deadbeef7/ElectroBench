"""ASCII class map of a crop, with the sky/object split stated.

Same idea as aerial_geom but for any region and any image: it prints an
occupancy map (sky vs object) plus the luma of the sky and the darkest surface
in the crop, so the threshold being used can be judged rather than assumed.
"""
import sys
import numpy as np
from PIL import Image


def load(p):
    with open(p, "rb") as f:
        if f.readline().strip() == b"P6":
            w, h = map(int, f.readline().split())
            f.readline()
            d = np.frombuffer(f.read(w * h * 3), dtype=np.uint8)
            return d.reshape(h, w, 3)
    return np.asarray(Image.open(p).convert("RGB"))


def main():
    p = sys.argv[1]
    x0, x1, y0, y1 = (int(v) for v in sys.argv[2:6])
    img = load(p).astype(np.float64)
    lum = img @ np.array([0.299, 0.587, 0.114])
    sub = lum[y0:y1, x0:x1]
    lo, hi = np.percentile(sub, 2), np.percentile(sub, 98)
    thr = 0.5 * (lo + hi)
    print("%s x%d-%d y%d-%d   crop object p2=%.0f sky p98=%.0f  threshold %.0f"
          % (p, x0, x1, y0, y1, lo, hi, thr))
    print("     " + "".join(str((x0 + i) // 10 % 10) for i in range(x1 - x0)))
    print("     " + "".join(str((x0 + i) % 10) for i in range(x1 - x0)))
    for j in range(sub.shape[0]):
        row = "".join(" " if v > thr else "#" for v in sub[j])
        print("%4d %s" % (y0 + j, row))


main()