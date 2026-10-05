"""Residual noise in the sunlit sea, averaged over matched A/B phases.

Answers the follow-up directly: after P24 raised the texture inside each plate,
does the sun column still read as grain -- and did P24 make it worse?

A single before/after frame pair cannot answer this for scene 2: the camera
breathes (radius 42+/-10, eye 7.5+/-2.2, yaw and pitch all animated), so two
frames are different views as well as different shaders, and the per-band mean
luma of a mis-matched pair swings by 70%. Every number here is a mean over the
same three phases (t = 32/36/40) on both sides, which is the only comparison in
this repo that is valid for scene 2.

  isolated bright px  P23's own metric: a pixel more than 0.06 above its own
                     15x15 background. A dot with no neighbours. This is the
                     definition "pixellated" was measured with in BUILD-P23
                     (17.44% of the water there), so it is directly comparable.
  lap1 / mean        high-frequency energy at 1 px, normalised by local mean.
  sky control        the same on sky rows, which contain no water.

Usage: seanoise3p.py <dir-with-before-*.ppm-and-after-*.ppm>
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


def boxmean(x, bg=15):
    pad = bg // 2
    p = np.pad(x, pad, mode="edge")
    cs = np.cumsum(np.cumsum(p, axis=0), axis=1)
    cs = np.pad(cs, ((1, 0), (1, 0)))
    h, w = x.shape
    ii, jj = np.meshgrid(np.arange(h), np.arange(w), indexing="ij")
    return ((cs[ii + bg, jj + bg] - cs[ii, jj + bg] -
             cs[ii + bg, jj] + cs[ii, jj]) / (bg * bg))


def measure(path):
    a = load(path)
    lum = a @ LUM
    hy = horizon(lum)
    h, w = lum.shape
    water = lum[hy:, :]
    k = max(8, w // 10)
    hot = np.argsort(water.mean(axis=0))[::-1][:k]
    hot.sort()
    col = lum[hy:, hot]
    lap = (np.abs(2 * col[1:-1, 1:-1] - col[:-2, 1:-1] - col[2:, 1:-1]) +
           np.abs(2 * col[1:-1, 1:-1] - col[1:-1, :-2] - col[1:-1, 2:])).mean()
    bg = boxmean(lum[hy:, :])
    iso = (lum[hy:, :] > bg + 0.06).mean()
    sky = lum[: hy // 2]
    sklap = (np.abs(2 * sky[1:-1, 1:-1] - sky[:-2, 1:-1] - sky[2:, 1:-1]) +
             np.abs(2 * sky[1:-1, 1:-1] - sky[1:-1, :-2] - sky[1:-1, 2:])).mean()
    return {"iso": float(iso) * 100, "lap": float(lap),
            "laprel": float(lap) / max(float(col.mean()), 1e-6) * 100,
            "colmean": float(col.mean()), "skyrel": float(sklap) /
            max(float(sky.mean()), 1e-6) * 100}


def main():
    d = sys.argv[1]
    res = {}
    for tag in ("before", "after"):
        per = [measure("%s/%s-%d.ppm" % (d, tag, t)) for t in PHASES]
        res[tag] = {k: (float(np.mean([p[k] for p in per])),
                         float(np.min([p[k] for p in per])),
                         float(np.max([p[k] for p in per]))) for k in per[0]}
    b, a = res["before"], res["after"]
    print("mean over t=%s, %d phases, 760x427, sun column = brightest 1/10 cols"
          % (",".join(map(str, PHASES)), len(PHASES)))
    print("%-26s %18s %18s %8s" % ("metric", "before [min-max]", "after [min-max]", "delta"))
    for k, lab in (("iso", "isolated bright px"),
                   ("lap", "lap1 (abs)"),
                   ("laprel", "lap1 / mean"),
                   ("colmean", "sun column mean luma"),
                   ("skyrel", "sky lap1 / mean (control)")):
        print("%-26s %6.3f [%5.2f-%5.2f] %6.3f [%5.2f-%5.2f] %+7.1f%%"
              % (lab, b[k][0], b[k][1], b[k][2], a[k][0], a[k][1], a[k][2],
                 (a[k][0] / b[k][0] - 1) * 100 if b[k][0] else float("nan")))


main()