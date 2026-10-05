"""P25 A/B: did narrowing the sunlit gate actually narrow the lit zone?

Compares a directory of "n<T>.ppm" (narrowed) against a directory of
"after-<T>.ppm" (previous) over the same three phases. Reports the lit-zone
WIDTH and the water luma, because narrowing a gate trades width against
brightness and both have to move together to be a real change rather than a
dimmer version of the same thing.

Usage: p25agg.py <narrow-dir> <prev-dir>
"""
import sys
import numpy as np
from PIL import Image

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


def measure(path):
    a = load(path)
    lum = a @ LUM
    h, w = lum.shape
    hy = horizon(lum)
    water = lum[hy:, :]
    thr = np.percentile(water, 75)
    lit = water > thr
    fc = lit.mean(axis=0) > 0.5
    best = cur = 0
    for v in fc:
        cur = cur + 1 if v else 0
        best = max(best, cur)
    sky = lum[: hy // 2]
    return {"litband": best / w * 100, "litfrac": lit.mean() * 100,
            "water": float(water.mean()), "sky": float(sky.mean())}


def main():
    per = {}
    for tag, d, fmt in (("before(P24)", sys.argv[2], "after-%d.ppm"),
                        ("after (P25)", sys.argv[1], "n%d.ppm")):
        vals = [measure("%s/%s" % (d, fmt % t)) for t in PHASES]
        per[tag] = {k: float(np.mean([v[k] for v in vals])) for k in vals[0]}
    b, a = per["before(P24)"], per["after (P25)"]
    print("mean over t=%s" % ",".join(map(str, PHASES)))
    print("%-24s %10s %10s %8s" % ("metric", "P24", "P25", "delta"))
    for k in ("litband", "litfrac", "water", "sky"):
        print("%-24s %10.3f %10.3f %+7.1f%%"
              % (k, b[k], a[k], (a[k] / b[k] - 1) * 100))


main()