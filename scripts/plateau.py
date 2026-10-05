"""Is the sunlit zone a SATURATED PLATEAU or genuine shading?

A bounded roll-off (sp = spR / (1 + spR * k)) protects against clipping, but
where the lobe is wide enough that spR >> 1/k across a wide area, that rational
form returns an almost CONSTANT value. The result is a large flat plate with a
sharp knee at its border -- exactly a "hard-edged tan amoeba". A real
specular falloff instead keeps a gradient all the way down.

So: in the sun column, measure how much of the bright area is sitting in a
narrow luma spike (plateau) versus spread across a ramp (gradient), and how
hard the boundary is.

Usage: plateau.py <image>
"""
import sys
import numpy as np
from PIL import Image

LUM = np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)


def load(p):
    if p.endswith(".png"):
        return np.asarray(Image.open(p).convert("RGB"), dtype=np.float32) / 255.0
    with open(p, "rb") as f:
        assert f.readline().strip() == b"P6"
        w, h = map(int, f.readline().split())
        f.readline()
        d = np.frombuffer(f.read(w * h * 3), dtype=np.uint8)
    return d.reshape(h, w, 3).astype(np.float32) / 255.0


def main():
    a = load(sys.argv[1])
    lum = a @ LUM
    h, w = lum.shape
    col = lum.mean(axis=1)
    cs = np.convolve(col, np.ones(5) / 5.0, mode="same")
    d = np.diff(cs[: int(h * 0.70)])
    hy = int(np.argmin(d)) + 1
    y0 = hy + (h - hy) // 12
    y1 = y0 + (h - hy) // 3
    sub = lum[y0:y1]
    print("image %dx%d  horizon y=%d  sun column rows %d..%d" % (w, h, hy, y0, y1))

    bright = sub[sub > np.percentile(sub, 70)]
    print("\nsun column luma: mean %.4f  p50 %.4f  p90 %.4f  p99 %.4f  max %.4f"
          % (sub.mean(), np.percentile(sub, 50), np.percentile(sub, 90),
             np.percentile(sub, 99), sub.max()))

    # how tightly packed is the bright population? a plateau = narrow spike
    lo, hi = np.percentile(sub, 70), sub.max()
    span = max(hi - lo, 1e-6)
    for frac in (0.02, 0.05, 0.10):
        inside = float((bright > hi - frac * span).mean())
        print("  bright pixels within %4.0f%% of the column's top value: %6.2f%%"
              % (frac * 100, inside * 100))

    # gradient inside the lit area vs across its border
    gy, gx = np.gradient(sub)
    gm = np.hypot(gx, gy)
    lit = sub > np.percentile(sub, 70)
    print("\nmean |grad| inside the lit region      %.5f" % gm[lit].mean())
    print("mean |grad| over the whole column       %.5f" % gm.mean())
    er = gm > np.percentile(gm, 99)
    print("pixels in the top 1%% of |grad|          %6.2f%% of column"
          % (er.mean() * 100))
    # the lit region's own internal spread: a plate has almost none
    v = bright[bright > hi - 0.10 * span]
    print("\nlit pixels (top 10%% of range): n=%d  mean %.4f  std %.4f  p10 %.4f p90 %.4f"
          % (v.size, v.mean(), v.std(), np.percentile(v, 10), np.percentile(v, 90)))


main()