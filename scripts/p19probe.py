#!/usr/bin/env python3
"""Scene-4 realism probe. Untracked local tooling.

Measures the defects that read as "CG" in a dusk corridor:
  * silhouette contrast  -- how far each material sits below its local sky
  * road texture         -- local stddev of the carriageway (flat == paper)
  * clipped glints       -- census of the hard-clipped specular band
  * flat-region census   -- fraction of the frame with no local structure
Usage: python3 scripts/p19probe.py frame.ppm
"""
import sys
import numpy as np
from PIL import Image


def load(path):
    im = Image.open(path).convert("RGB")
    return np.asarray(im).astype(np.float32) / 255.0


def lum(a):
    return a[..., 0] * 0.2126 + a[..., 1] * 0.7152 + a[..., 2] * 0.0722


def boxstd(L, r=4):
    """Local stddev on a centred (2r+1)^2 box, via a summed-area table.

    Returns NaN in the r-px border, where the box is not centred.
    """
    H, W = L.shape

    def integral(A):
        I = np.zeros((H + 1, W + 1), np.float64)
        I[1:, 1:] = np.cumsum(np.cumsum(A.astype(np.float64), 0), 1)
        return I

    I, I2 = integral(L), integral(L * L)

    def sums(I):
        y0 = np.arange(H)[:, None]
        x0 = np.arange(W)[None, :]
        y1, x1 = y0 + 2 * r + 1, x0 + 2 * r + 1
        y1 = np.minimum(y1, H)
        x1 = np.minimum(x1, W)
        return (I[y1, x1] - I[y0, x1] - I[y1, x0] + I[y0, x0])

    n = float((2 * r + 1) ** 2)
    m, m2 = sums(I) / n, sums(I2) / n
    sd = np.sqrt(np.maximum(m2 - m * m, 0.0))
    # mask the border band that could not be centred
    bad = np.zeros((H, W), bool)
    bad[:r, :] = bad[-r:, :] = bad[:, :r] = bad[:, -r:] = True
    sd[bad] = np.nan
    return sd


def main():
    p = sys.argv[1]
    a = load(p)
    H, W, _ = a.shape
    L = lum(a)
    sd = boxstd(L, 4)

    print(f"== {p}  ({W}x{H}) ==")
    print(f"luma  mean={L.mean():.3f} p05={np.percentile(L,5):.3f} p50={np.percentile(L,50):.3f} "
          f"p95={np.percentile(L,95):.3f} p99={np.percentile(L,99):.3f} max={L.max():.3f}")
    print(f"clip  >0.97={100*(L>0.97).mean():.3f}%  >0.99={100*(L>0.99).mean():.3f}%  "
          f"<0.03={100*(L<0.03).mean():.3f}%")

    # ---- flat-region census: a real photo has structure almost everywhere ----
    sdc = sd[~np.isnan(sd)]
    for thr in (0.004, 0.008, 0.015):
        flat = 100 * (sdc < thr).mean()
        print(f"flat   local-std<{thr:.3f}: {flat:.2f}% of frame")

    # ---- horizon split: sky above, ground below -----------------------------
    rowmed = np.median(L, axis=1)
    horiz = int(np.argmin(np.abs(rowmed - np.median(rowmed))))
    print(f"horizon row (median-luma inflect) ~ y={horiz}")

    sky = L[: max(1, horiz - 4)]
    gnd = L[horiz + 4 :]
    print(f"sky    mean={sky.mean():.3f}   ground mean={gnd.mean():.3f}   "
          f"ratio={sky.mean()/max(gnd.mean(),1e-4):.2f}")

    # ---- road band: the lower third is carriageway + verges ------------------
    rb = L[int(H * 0.68) :, :]
    sdb = sd[int(H * 0.68) :, :]
    sdb = sdb[~np.isnan(sdb)]
    print(f"road   mean={rb.mean():.3f} p50={np.percentile(rb,50):.3f} "
          f"local-std p50={np.percentile(sdb,50):.4f} p95={np.percentile(sdb,95):.4f}")

    # ---- silhouette contrast: dark pixels vs the sky right above them -------
    # "how much below local sky" is the number that decides whether the wire
    # tangle reads as objects or as stains.
    diffs = []
    for y in range(int(H * 0.06), horiz - 2, 4):
        row = L[y]
        for x in range(4, W - 4, 8):
            skyref = float(np.median(row[max(0, x - 40) : x + 41]))
            if row[x] < skyref - 0.02:
                diffs.append(skyref - row[x])
    if diffs:
        d = np.array(diffs)
        print(f"silh   n={len(d)} deficit p10={np.percentile(d,10):.3f} "
              f"p50={np.percentile(d,50):.3f} p90={np.percentile(d,90):.3f}")

    # ---- clipped-glint census: where does the specular hit the wall? --------
    hot = L > 0.93
    if hot.any():
        ys, xs = np.nonzero(hot)
        print(f"glint  {100*hot.mean():.3f}% px  bbox x[{xs.min()}-{xs.max()}] "
              f"y[{ys.min()}-{ys.max()}]  medianRGB="
              f"{np.median(a[hot],axis=0).round(3)}")


if __name__ == "__main__":
    main()