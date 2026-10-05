"""Blotch vs glitter: within-block vs between-block variance.

The reported P24 defect was large FLAT patches ("tan amoebas"), not fine
speckle. A block-step metric cannot tell those apart -- both are just
"neighbouring pixels differ". The discriminator is the variance split:

  blotch  -> high BETWEEN-block variance, LOW within-block std (flat plates)
  glitter -> high WITHIN-block std, comparatively low between-block

If the shader change turns flat plates into textured water, within-block std
rises against between-block variance. Report both, and the ratio.

Usage: p24flat.py <dir-with-before-*.ppm-and-after-*.ppm>
"""
import sys
import numpy as np

LUM = np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
PHASES = (32, 36, 40)


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


def split(a, k=32):
    lum = a @ LUM
    hy = horizon(lum)
    h, w = lum.shape
    sub = lum[hy:hy + 192, int(w * 0.25):int(w * 0.75)]
    hh = sub.shape[0] // k * k
    ww = sub.shape[1] // k * k
    blocks = sub[:hh, :ww].reshape(hh // k, ww // k, k, k)
    within = float(blocks.reshape(-1, k * k).std(axis=1).mean())
    means = blocks.mean(axis=(2, 3))
    between = float(means.std())
    return within, between, float(sub.mean())


def main():
    d = sys.argv[1]
    for k in (16, 32):
        res = {}
        for tag in ("before", "after"):
            per = [split(load("%s/%s-%d.ppm" % (d, tag, t)), k) for t in PHASES]
            res[tag] = [float(np.mean([p[i] for p in per])) for i in range(3)]
        b, a = res["before"], res["after"]
        wb, bb = 100 * b[0] / b[2], 100 * b[1] / b[2]
        wa, ba = 100 * a[0] / a[2], 100 * a[1] / a[2]
        print("block %2d px   (%% of band mean luma)" % k)
        print("           within-block std      between-block std    ratio b/w")
        print("  before  %20.2f      %20.2f      %11.2f" % (wb, bb, bb / wb))
        print("  after   %20.2f      %20.2f      %11.2f" % (wa, ba, ba / wa))
        print("  delta   %+19.1f%%      %+19.1f%%      %+10.1f%%"
              % ((wa / wb - 1) * 100, (ba / bb - 1) * 100,
                 ((ba / wa) / (bb / wb) - 1) * 100))
        print()


main()