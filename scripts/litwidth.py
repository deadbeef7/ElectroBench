"""How wide is the sunlit zone, and how big are its features?

The report is that the sunlit patches are TOO BIG. Two different things get
called "too big" and they need different fixes:

  WIDTH   how much of the sea is lit at all -- a gate that is too broad
          (warmGate = pow(sunAlign, 3) falls off very slowly) lights the whole
          frame instead of a column under the sun
  FEATURE the size of the individual lit patches -- the wave/footprint scale

So measure both, as fractions of the frame, so resolutions can be compared
directly rather than in pixels.

Usage: litwidth.py <image> [label]
"""
import sys
import numpy as np
from PIL import Image

LUM = np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)


def load(p):
    return np.asarray(Image.open(p).convert("RGB"), dtype=np.float32) / 255.0


def horizon(lum):
    col = lum.mean(axis=1)
    cs = np.convolve(col, np.ones(5) / 5.0, mode="same")
    d = np.diff(cs[: int(lum.shape[0] * 0.70)])
    return int(np.argmin(d)) + 1


def main():
    a = load(sys.argv[1])
    label = sys.argv[2] if len(sys.argv) > 2 else sys.argv[1]
    lum = a @ LUM
    h, w = lum.shape
    hy = horizon(lum)
    water = lum[hy:, :]
    # "lit" = clearly above the water's own dark background, not just brighter
    thr = np.percentile(water, 75)
    lit = water > thr
    print("%-28s %4dx%-4d  horizon y=%d" % (label, w, h, hy))

    frac_cols = lit.mean(axis=0)
    wide = frac_cols > 0.5
    # longest run of columns where most of the water is lit
    best = cur = 0
    for v in wide:
        cur = cur + 1 if v else 0
        best = max(best, cur)
    print("   lit (top quartile of water)      %5.1f%% of all water" % (lit.mean() * 100))
    print("   continuous lit band              %5.1f%% of frame width" % (best / w * 100))
    print("   water mean luma                  %.4f" % water.mean())

    # feature size of the lit pattern: lag correlation along x, in FRACTIONS
    # of frame width so two resolutions are directly comparable
    prof = np.where(wide)[0]
    if prof.size:
        print("   lit band spans x = %d..%d of %d" % (prof.min(), prof.max(), w))
    row = lit.mean(axis=0)
    for frac in (0.02, 0.05, 0.10):
        L = max(2, int(w * frac))
        # direct lag difference: np.diff(a, n=N) is an N-fold recursive
        # difference and blows up numerically for large N on this data
        d = float(np.abs(row[L:] - row[:-L]).mean())
        print("   |delta| of lit-fraction over %4.0f%% of width (%3d px)  %.4f"
              % (frac * 100, L, d))


main()