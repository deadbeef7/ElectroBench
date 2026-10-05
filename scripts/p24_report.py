"""Aggregate the three phases per variant: mean, spread and the delta.

Scene 2 is not pixel-reproducible on a software rasteriser, so a single phase
proves nothing -- the before set alone swings from 11.2 to 16.7 at the 64 px
scale across t=32/36/40. Everything here is reported as a mean over the three
phases with the range beside it, and as a before/after delta.
"""
import sys

import numpy as np

from multiscale import SIZES, load


def metrics(p):
    a = load(p)
    h, w, _ = a.shape
    lum = a @ np.array([0.2126, 0.7152, 0.0722], np.float32)
    col = np.convolve(lum.mean(axis=1), np.ones(5) / 5.0, mode="same")
    hy = int(np.argmin(np.diff(col[:int(h * 0.70)]))) + 1
    wat = lum[hy:]
    cs = wat.mean(axis=0)
    k = max(8, w // 10)
    hot = np.zeros(w, bool)
    hot[np.argsort(cs)[::-1][:k]] = True
    wat = wat[:, hot]
    wm = float(wat.mean())
    out = {"colmean": wm, "watermean": float(lum[hy:].mean()),
           "skymean": float(lum[:hy].mean())}
    for bs in SIZES:
        ny, nx = wat.shape[0] // bs, wat.shape[1] // bs
        b = wat[:ny * bs, :nx * bs].reshape(ny, bs, nx, bs).mean(axis=(1, 3))
        st = np.concatenate([np.abs(np.diff(b, axis=1)).ravel(),
                             np.abs(np.diff(b, axis=0)).ravel()])
        out[bs] = float(st.mean()) / wm * 100.0
    r, gg, bb = a[:, :, 0], a[:, :, 1], a[:, :, 2]
    out["bluish"] = float(100.0 * (((bb - r > 0.04) & (bb - gg > 0.02)) &
                                   np.arange(h)[:, None] >= hy).sum()
                          / max((h - hy) * w, 1))
    return out


def main():
    groups = {}
    for p in sys.argv[1:]:
        groups.setdefault(p.split("/")[-1].rsplit("-", 1)[0], []).append(p)
    res = {g: [metrics(p) for p in ps] for g, ps in groups.items()}
    for g, ms in res.items():
        print("\n%s  (n=%d phases)" % (g, len(ms)))
        for key in ["colmean", "watermean", "skymean"] + list(SIZES) + ["bluish"]:
            v = [m[key] for m in ms]
            print("   %-10s mean %10.4f   range %.4f..%.4f"
                  % (key, np.mean(v), min(v), max(v)))

    names = list(res)
    if len(names) == 2:
        print("\n%-10s %12s %12s %10s" % ("metric", names[0], names[1], "delta"))
        for key in ["colmean", "watermean", "skymean"] + list(SIZES) + ["bluish"]:
            a = float(np.mean([m[key] for m in res[names[0]]]))
            b = float(np.mean([m[key] for m in res[names[1]]]))
            print("%-10s %12.4f %12.4f %9.1f%%"
                  % (key, a, b, 100.0 * (b - a) / a if a else 0.0))


main()