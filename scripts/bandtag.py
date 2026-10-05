"""Read a six-band tag render: which term paints the flat plates?

The tag render puts one shading term in each sixth of the frame width. For
each band this measures

  plate   fraction of water pixels in the top 10% of the term's own range --
          a hard-thresholded mask drives this toward 1.0, a smooth one low
  edge    mean |d/dx|, the hard-boundary density the eye reads as "camo"
  lagL    mean |v(x+L) - v(x)| along x, in units of the term's own stddev.
          A term whose structure is BIG stays correlated at 64 px; one that
          is glitter decays to ~1.0 almost immediately.

Usage: bandtag.py <ppm>
"""
import sys
import numpy as np

LUM = np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
NAMES = ["0 foam", "1 crest", "2 warmSlope", "3 sunDiffuse", "4 chaos", "5 final"]


def load(p):
    with open(p, "rb") as f:
        assert f.readline().strip() == b"P6"
        w, h = map(int, f.readline().split())
        f.readline()
        d = np.frombuffer(f.read(w * h * 3), dtype=np.uint8)
    return d.reshape(h, w, 3).astype(np.float32) / 255.0


def horizon(lum):
    col = lum.mean(axis=1)
    cs = np.convolve(col, np.ones(5) / 5.0, mode="same")
    d = np.diff(cs[: int(lum.shape[0] * 0.70)])
    return int(np.argmin(d)) + 1


def main():
    a = load(sys.argv[1])
    lum = a @ LUM
    hy = horizon(lum)
    w = a.shape[1]
    bw = w // 6
    print("horizon y=%d   %d bands of %d px   water rows %d..%d"
          % (hy, 6, bw, hy, a.shape[0]))
    print("%-12s %7s %7s %8s %8s %8s %8s"
          % ("band", "mean", "std", "plate", "edge", "lag16", "lag64"))
    for i in range(6):
        x0, x1 = i * bw, (i + 1) * bw
        sub = lum[hy + 4:, x0:x1]          # water only
        sd = sub.std() + 1e-6
        rng = sub.max() - sub.min() + 1e-6
        plate = float((sub > sub.max() - 0.10 * rng).mean())
        edge = float(np.abs(np.diff(sub, axis=1)).mean() / sd)
        lags = []
        for L in (16, 64):
            lags.append(float(np.abs(sub[:, L:] - sub[:, :-L]).mean() / sd))
        print("%-12s %7.4f %7.4f %8.3f %8.3f %8.3f %8.3f"
              % (NAMES[i], sub.mean(), sd, plate, edge, lags[0], lags[1]))
    print("\nlag values are mean |delta| / std: ~1.0 means no structure left at "
          "that lag, i.e. features smaller than the lag.")


main()