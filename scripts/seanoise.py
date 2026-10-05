"""Is the sunlit water still NOISY?

The P23 complaint was pixellation, and P24 was about the opposite defect (flat
plates). This asks the narrower question directly: after P24 raised the texture
inside each plate, does the sun column now read as grain?

Measured at the shipped 1280x720, so the numbers are in the pixels the user
actually sees. Noise metrics used:

  isolated bright px  one pixel >0.06 above its own 15x15 background: a dot with
                     no neighbours. The definition P23 fixed; must not come back.
  lap1               mean |laplacian| at 1 px: raw high-frequency energy.
  hf ratio           lap1 normalised by the block's own mean luma, so a global
                     brightness change cannot masquerade as more noise.
  sky control        the same lap1 on the sky, which has no water in it. A real
                     change must move the sea and not the sky.

Usage: seanoise.py <before.png> <after.png>
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


def isolated(a, thr=0.06, bg=15):
    """fraction of pixels standing alone above their own local background"""
    lum = a @ LUM
    k = np.ones((bg, bg), dtype=np.float32) / (bg * bg)
    pad = bg // 2
    p = np.pad(lum, pad, mode="edge")
    csum = np.cumsum(np.cumsum(p, axis=0), axis=1)
    csum = np.pad(csum, ((1, 0), (1, 0)))
    n = lum.shape[0], lum.shape[1]
    ii, jj = np.meshgrid(np.arange(n[0]), np.arange(n[1]), indexing="ij")
    y0, x0 = ii, jj
    y1, x1 = ii + bg, jj + bg
    local = (csum[y1, x1] - csum[y0, x1] - csum[y1, x0] + csum[y0, x0]) / (bg * bg)
    return float((lum > local + thr).mean()), lum, local


def stats(path, tag):
    a = load(path)
    lum = a @ LUM
    hy = horizon(lum)
    h, w = lum.shape
    # the sun column: brightest water columns, from just under the horizon down
    y0 = hy + 4
    water = lum[y0:, :]
    colmean = water.mean(axis=0)
    keep = np.argsort(colmean)[-int(w * 0.15):]
    sub = lum[y0:, np.sort(keep)]
    lap = (np.abs(2 * sub[1:-1, 1:-1] - sub[:-2, 1:-1] - sub[2:, 1:-1]) +
           np.abs(2 * sub[1:-1, 1:-1] - sub[1:-1, :-2] - sub[1:-1, 2:]))
    iso_all, _, _ = isolated(a)
    # per-row-band noise, to see whether it is near-field or far-field
    bands = []
    bh = max(32, (h - y0) // 4)
    for g in range(4):
        b0 = y0 + g * bh
        b1 = min(h, b0 + bh)
        if b1 - b0 < 24:
            break
        s = lum[b0:b1, np.sort(keep)]
        lp = (np.abs(2 * s[1:-1, 1:-1] - s[:-2, 1:-1] - s[2:, 1:-1]) +
              np.abs(2 * s[1:-1, 1:-1] - s[1:-1, :-2] - s[1:-1, 2:]))
        bands.append((float(s.mean()), float(lp.mean())))
    sky = lum[: hy // 2]
    sklap = (np.abs(2 * sky[1:-1, 1:-1] - sky[:-2, 1:-1] - sky[2:, 1:-1]) +
             np.abs(2 * sky[1:-1, 1:-1] - sky[1:-1, :-2] - sky[1:-1, 2:]))
    return {
        "iso_all": iso_all * 100,
        "colmean": float(sub.mean()),
        "lap1": float(lap.mean()),
        "lap1_rel": float(lap.mean()) / max(float(sub.mean()), 1e-6) * 100,
        "skylap": float(sklap.mean()),
        "skylap_rel": float(sklap.mean()) / max(float(sky.mean()), 1e-6) * 100,
        "bands": bands,
    }


def main():
    b = stats(sys.argv[1], "before")
    a = stats(sys.argv[2], "after")
    print("%-34s %10s %10s %9s" % ("metric @1280x720", "before", "after", "delta"))
    for k, lab, pct in (("iso_all", "isolated bright px (whole frame)", True),
                        ("colmean", "sun column mean luma", False),
                        ("lap1", "sun column lap1 (abs)", False),
                        ("lap1_rel", "sun column lap1 / mean", True),
                        ("skylap", "sky lap1 (abs)", False),
                        ("skylap_rel", "sky lap1 / mean (control)", True)):
        d = (a[k] / b[k] - 1) * 100 if b[k] else float("nan")
        print("%-34s %10.4f %10.4f %+8.1f%%" % (lab, b[k], a[k], d))
    if "bands" in b:
        print("\nper-band lap1 / band mean, near row first")
        for i, (bm, bv) in enumerate(b["bands"]):
            am, av = a["bands"][i]
            print("  band %d  mean %5.4f -> %5.4f   lap1/mean %6.2f -> %6.2f  %+6.1f%%"
                  % (i, bm, am, 100 * bv / bm, 100 * av / am,
                     ((av / am) / (bv / bm) - 1) * 100))


main()